// SPDX-License-Identifier: GPL-2.0
/*
 * Allwinner Sun60i (A733) CPUFreq nvmem driver
 *
 * Reads the DVFS code from SID and selects the appropriate OPP table.
 *
 * Copyright (c) 2025 Allwinner Technology Co.,Ltd.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/clk.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "sunxi-dsufreq.h"

#define DVFS_SID_OFFSET		0x4c
#define DCXO_CLK_26M		26000000

static struct platform_device *cpufreq_dt_pdev;
static char sun60i_opp_prop[16];
static bool sun60i_opp_prop_valid;

const char *sun60i_cpufreq_get_opp_prop(void)
{
	return sun60i_opp_prop_valid ? sun60i_opp_prop : NULL;
}
EXPORT_SYMBOL_GPL(sun60i_cpufreq_get_opp_prop);

bool sun60i_cpufreq_is_26m_crystal(void)
{
	return sun60i_opp_prop_valid &&
	       !strncmp(sun60i_opp_prop, "26m-", 4);
}
EXPORT_SYMBOL_GPL(sun60i_cpufreq_is_26m_crystal);

static int sun60i_read_dvfs_code(struct nvmem_device *nvmem, u32 *code)
{
	ssize_t len;
	u32 word;

	len = nvmem_device_read(nvmem, DVFS_SID_OFFSET, sizeof(word), &word);
	if (len < 0)
		return len;

	*code = (word >> 16) & 0xff;
	if (!*code)
		*code = (word >> 8) & 0xff;

	return 0;
}

static int sun60i_match_vf_table(u32 combi, u32 *index)
{
	struct device_node *np;
	int count, i, ret;
	u32 key, val;

	np = of_find_node_by_name(NULL, "vf_mapping_table");
	if (!np)
		return -ENOENT;

	count = of_property_count_u32_elems(np, "table");
	if (count <= 0) {
		of_node_put(np);
		return -EINVAL;
	}

	for (i = 0; i < count / 2; i++) {
		ret = of_property_read_u32_index(np, "table", i * 2, &key);
		if (ret)
			goto out;

		if (key != combi)
			continue;

		ret = of_property_read_u32_index(np, "table", i * 2 + 1, &val);
		if (ret)
			goto out;

		*index = val;
		ret = 0;
		goto out;
	}

	*index = 0x0100;
	ret = 0;
out:
	of_node_put(np);
	return ret;
}

static unsigned long sun60i_cpu_boot_rate(void)
{
	struct device *cpu_dev;
	struct clk *clk;
	unsigned long rate;

	cpu_dev = get_cpu_device(0);
	if (cpu_dev) {
		clk = clk_get(cpu_dev, NULL);
		if (!IS_ERR(clk)) {
			rate = clk_get_rate(clk);
			clk_put(clk);
			if (rate)
				return rate;
		}
	}

	clk = clk_get(NULL, "pll-cpu-l");
	if (IS_ERR(clk))
		return 0;

	rate = clk_get_rate(clk);
	clk_put(clk);

	return rate;
}

/*
 * Choose vfXXXX vs 26m-vfXXXX from the OPP node that matches the boot
 * frequency. Fixed "dcxo" parents in DT are not a reliable crystal strap.
 */
static int sun60i_select_opp_prop(struct device *dev, u32 index,
				  char *name, size_t size)
{
	struct device_node *cpu_np, *opp_np, *opp;
	unsigned long boot_rate = sun60i_cpu_boot_rate();
	char prop_24[20], prop_26[24];
	u64 opp_hz;
	u32 volt_24 = 0, volt_26 = 0;
	bool found = false;

	snprintf(prop_24, sizeof(prop_24), "opp-microvolt-vf%04x", index);
	snprintf(prop_26, sizeof(prop_26), "opp-microvolt-26m-vf%04x", index);

	cpu_np = of_cpu_device_node_get(0);
	if (!cpu_np)
		goto fallback;

	opp_np = of_parse_phandle(cpu_np, "operating-points-v2", 0);
	of_node_put(cpu_np);
	if (!opp_np)
		goto fallback;

	for_each_available_child_of_node(opp_np, opp) {
		if (of_property_read_u64(opp, "opp-hz", &opp_hz))
			continue;

		if (boot_rate && opp_hz == boot_rate) {
			of_property_read_u32(opp, prop_24, &volt_24);
			of_property_read_u32(opp, prop_26, &volt_26);
			found = true;
			of_node_put(opp);
			break;
		}
	}
	of_node_put(opp_np);

	if (found && volt_26 && !volt_24) {
		snprintf(name, size, "26m-vf%04x", index);
		dev_info(dev,
			 "using OPP property %s (dvfs index 0x%x boot=%lu Hz)\n",
			 name, index, boot_rate);
		return 0;
	}

	if (found && volt_24) {
		snprintf(name, size, "vf%04x", index);
		dev_info(dev,
			 "using OPP property %s (dvfs index 0x%x boot=%lu Hz)\n",
			 name, index, boot_rate);
		return 0;
	}

	/* Boot rate is not in the 24M table, but exists with 26M voltage. */
	if (found && volt_26) {
		snprintf(name, size, "26m-vf%04x", index);
		dev_info(dev,
			 "using OPP property %s (dvfs index 0x%x boot=%lu Hz)\n",
			 name, index, boot_rate);
		return 0;
	}

fallback:
	/*
	 * No OPP match (or 0V for both): use known 26M-only boot points,
	 * else follow the RTC dcxo clock rate.
	 */
	if (boot_rate == 416000000 || boot_rate == 728000000 ||
	    boot_rate == 780000000 || boot_rate == 1014000000 ||
	    boot_rate == 1092000000) {
		snprintf(name, size, "26m-vf%04x", index);
	} else {
		struct clk *dcxo = clk_get(NULL, "dcxo");
		unsigned long dcxo_rate = 24000000;

		if (!IS_ERR(dcxo)) {
			dcxo_rate = clk_get_rate(dcxo) ?: 24000000;
			clk_put(dcxo);
		}

		if (dcxo_rate == DCXO_CLK_26M)
			snprintf(name, size, "26m-vf%04x", index);
		else
			snprintf(name, size, "vf%04x", index);
	}

	dev_info(dev, "using OPP property %s (dvfs index 0x%x boot=%lu Hz, fallback)\n",
		 name, index, boot_rate);

	return 0;
}

static int sun60i_get_vf_prop_name(struct device *dev, struct nvmem_device *nvmem,
				   char *name, size_t size)
{
	u32 dvfs = 0, index = 0x0100;
	int ret;

	ret = sun60i_read_dvfs_code(nvmem, &dvfs);
	if (ret) {
		dev_warn(dev, "failed to read DVFS code, using default VF table\n");
		dvfs = 0;
	}

	ret = sun60i_match_vf_table(dvfs, &index);
	if (ret)
		return ret;

	return sun60i_select_opp_prop(dev, index, name, size);
}

static int sun60i_cpufreq_nvmem_probe(struct platform_device *pdev)
{
	struct nvmem_device *nvmem;
	struct dev_pm_opp_config config = {};
	char prop_name[16];
	int *opp_tokens;
	unsigned int cpu;
	int ret;

	nvmem = devm_nvmem_device_get(&pdev->dev, NULL);
	if (IS_ERR(nvmem))
		return dev_err_probe(&pdev->dev, PTR_ERR(nvmem),
				     "failed to get SID nvmem\n");

	ret = sun60i_get_vf_prop_name(&pdev->dev, nvmem, prop_name,
				      sizeof(prop_name));
	if (ret)
		return ret;

	strscpy(sun60i_opp_prop, prop_name, sizeof(sun60i_opp_prop));
	sun60i_opp_prop_valid = true;

	opp_tokens = kcalloc(num_possible_cpus(), sizeof(*opp_tokens),
			     GFP_KERNEL);
	if (!opp_tokens)
		return -ENOMEM;

	config.prop_name = prop_name;

	for_each_present_cpu(cpu) {
		struct device *cpu_dev = get_cpu_device(cpu);

		if (!cpu_dev) {
			ret = -ENODEV;
			goto free_opp;
		}

		ret = dev_pm_opp_set_config(cpu_dev, &config);
		if (ret < 0)
			goto free_opp;

		opp_tokens[cpu] = ret;
	}

	cpufreq_dt_pdev = platform_device_register_simple("cpufreq-dt", -1,
							  NULL, 0);
	if (IS_ERR(cpufreq_dt_pdev)) {
		ret = PTR_ERR(cpufreq_dt_pdev);
		goto free_opp;
	}

	platform_set_drvdata(pdev, opp_tokens);
	return 0;

free_opp:
	for_each_present_cpu(cpu)
		dev_pm_opp_clear_config(opp_tokens[cpu]);
	kfree(opp_tokens);

	return ret;
}

static void sun60i_cpufreq_nvmem_remove(struct platform_device *pdev)
{
	int *opp_tokens = platform_get_drvdata(pdev);
	unsigned int cpu;

	platform_device_unregister(cpufreq_dt_pdev);

	for_each_present_cpu(cpu)
		dev_pm_opp_clear_config(opp_tokens[cpu]);

	sun60i_opp_prop_valid = false;
	kfree(opp_tokens);
}

static const struct of_device_id sun60i_cpufreq_of_match[] = {
	{ .compatible = "allwinner,sun60i-cpufreq-nvmem" },
	{}
};
MODULE_DEVICE_TABLE(of, sun60i_cpufreq_of_match);

static struct platform_driver sun60i_cpufreq_driver = {
	.probe = sun60i_cpufreq_nvmem_probe,
	.remove = sun60i_cpufreq_nvmem_remove,
	.driver = {
		.name = "sun60i-cpufreq-nvmem",
		.of_match_table = sun60i_cpufreq_of_match,
	},
};
module_platform_driver(sun60i_cpufreq_driver);

MODULE_DESCRIPTION("Allwinner A733 cpufreq nvmem driver");
MODULE_LICENSE("GPL");
