// SPDX-License-Identifier: GPL-2.0
/*
 * Allwinner DSU frequency scaling for sun60i (A733)
 *
 * DSU clock tracks max(little, big) CPU rate at ~3/4, ordered around
 * cpufreq-dt OPP changes via sunxi_set_dsufreq_cb().
 *
 * Based on Allwinner BSP sunxi-dsufreq for sun60iw2.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/slab.h>

#include "sunxi-dsufreq.h"

#define MAX_NAME_LEN		16
#define POLICY_NUM		2
#define BIG_CORE_THRESHOLD_EXT	1488000000UL
#define DSU_MIN_FREQ_THRESHOLD	200000000UL
#define DSU_MAX_FREQ_THRESHOLD	2000000000UL
#define CLUS_CTRL_REG0		0x10
#define DCXO_CLK_26M		26000000UL
#define DCXO_CLK_24M		24000000UL

enum dsufreq_scaling_direction {
	DSUFREQ_NOT_SCALING = 0,
	DSUFREQ_SCALING_DOWN,
	DSUFREQ_SCALING_UP,
	DSUFREQ_SCALING_EXTEND,
};

struct dsu_opp_entry {
	unsigned long freq_hz;
	unsigned long volt_uv;
};

struct sunxi_dsufreq_dev {
	struct device *dev;
	struct clk *clk;
	void __iomem *clus_ctrl;
	char opp_prop[MAX_NAME_LEN];
	struct dsu_opp_entry *opp;
	int opp_count;
	int opp_token;
	unsigned long max_f_by_min_v;
	unsigned long prev_freq;
	unsigned long cur_freq;
	unsigned long next_freq;
	unsigned long little_core_cur_freq;
	unsigned long big_core_cur_freq;
	unsigned long dsu_clock_rate;
	enum dsufreq_scaling_direction scaling_direction;
	unsigned int policy_cnt;
	struct cpufreq_policy *policy[POLICY_NUM];
};

static struct sunxi_dsufreq_dev *dsufreq_dev;

static int set_dsu_clk(struct sunxi_dsufreq_dev *df, unsigned long freq)
{
	int ret;

	if (freq < DSU_MIN_FREQ_THRESHOLD || freq > DSU_MAX_FREQ_THRESHOLD)
		dev_warn(df->dev, "abnormal dsu freq %lu KHz\n", freq / 1000);

	ret = clk_set_rate(df->clk, freq);
	if (ret)
		dev_err(df->dev, "failed to set dsu rate %lu: %d\n", freq, ret);

	return ret;
}

static unsigned long get_dsu_max_freq_by_cur_volt(struct sunxi_dsufreq_dev *df,
						 unsigned long little_next)
{
	struct device *cpu_dev = get_cpu_device(df->policy[0]->cpu);
	struct dev_pm_opp *opp;
	unsigned long freq = little_next;
	unsigned long volt;
	int i, idx;

	opp = dev_pm_opp_find_freq_floor(cpu_dev, &freq);
	if (IS_ERR(opp))
		return df->max_f_by_min_v;

	volt = dev_pm_opp_get_voltage(opp);
	dev_pm_opp_put(opp);

	idx = df->opp_count - 1;
	for (i = 0; i < df->opp_count; i++) {
		if (volt == df->opp[idx - i].volt_uv)
			return df->opp[idx - i].freq_hz;
	}

	return df->max_f_by_min_v;
}

static unsigned long snap_dsu_freq(struct sunxi_dsufreq_dev *df,
				   unsigned long freq)
{
	unsigned long step = (df->dsu_clock_rate == DCXO_CLK_26M) ?
			     DCXO_CLK_26M : DCXO_CLK_24M;

	if (freq % step)
		freq -= freq % step;

	return freq;
}

static unsigned long calc_dsu_next_freq(struct sunxi_dsufreq_dev *df,
					struct cpufreq_policy *policy,
					unsigned long freq,
					unsigned long *big_next)
{
	unsigned long max_next, little_next, dsu_next, dsu_max;
	static bool inited;

	*big_next = 0;

	if (df->policy_cnt == 1) {
		little_next = freq;
		*big_next = 0;
		max_next = freq;
	} else {
		if (!inited) {
			df->little_core_cur_freq =
				(unsigned long)df->policy[0]->cur * 1000;
			df->big_core_cur_freq =
				(unsigned long)df->policy[1]->cur * 1000;
			inited = true;
		}

		if (policy == df->policy[0]) {
			max_next = max(freq, df->big_core_cur_freq);
			little_next = freq;
			*big_next = df->big_core_cur_freq;
		} else {
			max_next = max(freq, df->little_core_cur_freq);
			little_next = df->little_core_cur_freq;
			*big_next = freq;
		}
	}

	dsu_next = snap_dsu_freq(df, (max_next * 3) / 4);
	dsu_max = get_dsu_max_freq_by_cur_volt(df, little_next);

	return min(dsu_next, dsu_max);
}

static int set_dsufreq_scaling_down(struct cpufreq_policy *policy,
				    unsigned long freq)
{
	struct sunxi_dsufreq_dev *df = dsufreq_dev;
	unsigned long big_next = 0;

	if (!df)
		return -ENODEV;

	df->next_freq = calc_dsu_next_freq(df, policy, freq, &big_next);

	if (big_next >= BIG_CORE_THRESHOLD_EXT &&
	    df->next_freq < df->max_f_by_min_v) {
		df->scaling_direction = DSUFREQ_SCALING_EXTEND;
		df->next_freq = df->max_f_by_min_v;
		set_dsu_clk(df, df->next_freq);
		df->prev_freq = df->cur_freq;
		df->cur_freq = df->next_freq;
	} else if (df->cur_freq == df->next_freq) {
		df->scaling_direction = DSUFREQ_NOT_SCALING;
	} else if (df->cur_freq > df->next_freq) {
		df->scaling_direction = DSUFREQ_SCALING_DOWN;
		set_dsu_clk(df, df->next_freq);
		df->prev_freq = df->cur_freq;
		df->cur_freq = df->next_freq;
	} else {
		df->scaling_direction = DSUFREQ_SCALING_UP;
	}

	return 0;
}

static int set_dsufreq_scaling_up(struct cpufreq_policy *policy,
				  unsigned long freq, int set_opp_fail)
{
	struct sunxi_dsufreq_dev *df = dsufreq_dev;

	if (!df)
		return -ENODEV;

	if (df->scaling_direction == DSUFREQ_SCALING_DOWN ||
	    df->scaling_direction == DSUFREQ_SCALING_EXTEND) {
		if (set_opp_fail) {
			set_dsu_clk(df, df->prev_freq);
			df->cur_freq = df->prev_freq;
		}
	} else if (df->scaling_direction == DSUFREQ_SCALING_UP) {
		set_dsu_clk(df, df->next_freq);
		df->prev_freq = df->cur_freq;
		df->cur_freq = df->next_freq;
	}

	if (!set_opp_fail) {
		if (policy == df->policy[0])
			df->little_core_cur_freq = freq;
		else
			df->big_core_cur_freq = freq;
	}

	return 0;
}

static ssize_t scaling_available_frequencies_show(const struct class *class,
						  const struct class_attribute *attr,
						  char *buf)
{
	struct sunxi_dsufreq_dev *df = dsufreq_dev;
	ssize_t count = 0;
	int i;

	if (!df)
		return -ENODEV;

	for (i = 0; i < df->opp_count; i++)
		count += sysfs_emit_at(buf, count, "%luKHz@%lumV ",
				       df->opp[i].freq_hz / 1000,
				       df->opp[i].volt_uv / 1000);

	count += sysfs_emit_at(buf, count, "\n");
	return count;
}
static CLASS_ATTR_RO(scaling_available_frequencies);

static ssize_t scaling_cur_freq_show(const struct class *class,
				     const struct class_attribute *attr,
				     char *buf)
{
	if (!dsufreq_dev)
		return -ENODEV;

	return sysfs_emit(buf, "%lu\n", dsufreq_dev->cur_freq / 1000);
}
static CLASS_ATTR_RO(scaling_cur_freq);

static ssize_t dsu_cooling_show(const struct class *class,
				const struct class_attribute *attr,
				char *buf)
{
	if (!dsufreq_dev || !dsufreq_dev->clus_ctrl)
		return -ENODEV;

	return sysfs_emit(buf, "%x\n",
			  readl(dsufreq_dev->clus_ctrl + CLUS_CTRL_REG0));
}

static ssize_t dsu_cooling_store(const struct class *class,
				 const struct class_attribute *attr,
				 const char *buf, size_t count)
{
	unsigned int value, reg;
	int ret;

	if (!dsufreq_dev || !dsufreq_dev->clus_ctrl)
		return -ENODEV;

	ret = kstrtouint(buf, 0, &value);
	if (ret)
		return ret;

	reg = readl(dsufreq_dev->clus_ctrl + CLUS_CTRL_REG0);
	if (value)
		reg |= 0x1ff0;
	else
		reg &= ~0x1ff0;
	writel(reg, dsufreq_dev->clus_ctrl + CLUS_CTRL_REG0);

	return count;
}
static CLASS_ATTR_RW(dsu_cooling);

static struct attribute *dsufreq_class_attrs[] = {
	&class_attr_scaling_available_frequencies.attr,
	&class_attr_scaling_cur_freq.attr,
	&class_attr_dsu_cooling.attr,
	NULL,
};
ATTRIBUTE_GROUPS(dsufreq_class);

static struct class dsufreq_class = {
	.name = "dsufreq",
	.class_groups = dsufreq_class_groups,
};

static int dsu_init_freq_table(struct sunxi_dsufreq_dev *df)
{
	struct device *dev = df->dev;
	struct device *cpu_dev;
	struct dev_pm_opp_config config = {};
	struct dev_pm_opp *opp;
	const char *prop;
	unsigned long freq, min_volt, min_freq, l_min_volt;
	int i, idx, ret;

	prop = sun60i_cpufreq_get_opp_prop();
	if (!prop)
		return -EPROBE_DEFER;

	strscpy(df->opp_prop, prop, sizeof(df->opp_prop));
	df->dsu_clock_rate = sun60i_cpufreq_is_26m_crystal() ?
			     DCXO_CLK_26M : DCXO_CLK_24M;

	config.prop_name = df->opp_prop;
	ret = dev_pm_opp_set_config(dev, &config);
	if (ret < 0) {
		dev_err(dev, "failed to set OPP prop %s: %d\n", df->opp_prop, ret);
		return ret;
	}
	df->opp_token = ret;

	ret = devm_pm_opp_of_add_table(dev);
	if (ret < 0) {
		dev_err(dev, "failed to add OPP table: %d\n", ret);
		goto clear_config;
	}

	df->opp_count = dev_pm_opp_get_opp_count(dev);
	if (df->opp_count <= 0) {
		ret = df->opp_count ? df->opp_count : -EINVAL;
		goto clear_config;
	}

	df->opp = devm_kmalloc_array(dev, df->opp_count,
				     sizeof(*df->opp), GFP_KERNEL);
	if (!df->opp) {
		ret = -ENOMEM;
		goto clear_config;
	}

	idx = df->opp_count - 1;
	for (i = 0, freq = ULONG_MAX; i < df->opp_count; i++, freq--) {
		opp = dev_pm_opp_find_freq_floor(dev, &freq);
		if (IS_ERR(opp)) {
			ret = PTR_ERR(opp);
			goto clear_config;
		}
		df->opp[idx - i].freq_hz = freq;
		df->opp[idx - i].volt_uv = dev_pm_opp_get_voltage(opp);
		dev_pm_opp_put(opp);
	}

	cpu_dev = get_cpu_device(df->policy[0]->cpu);
	min_freq = (unsigned long)df->policy[0]->cpuinfo.min_freq * 1000;
	opp = dev_pm_opp_find_freq_floor(cpu_dev, &min_freq);
	if (IS_ERR(opp)) {
		ret = PTR_ERR(opp);
		goto clear_config;
	}
	l_min_volt = dev_pm_opp_get_voltage(opp);
	dev_pm_opp_put(opp);

	min_volt = df->opp[0].volt_uv;
	if (min_volt > l_min_volt) {
		dev_err(dev, "dsu min volt %lu > little min %lu\n",
			min_volt, l_min_volt);
		ret = -EINVAL;
		goto clear_config;
	}

	df->max_f_by_min_v = df->opp[0].freq_hz;
	for (i = 1; i < df->opp_count; i++) {
		if (min_volt != df->opp[i].volt_uv) {
			df->max_f_by_min_v = df->opp[i - 1].freq_hz;
			break;
		}
	}

	dev_info(dev, "opp prop %s, %d OPPs, max@minV=%lu Hz\n",
		 df->opp_prop, df->opp_count, df->max_f_by_min_v);
	return 0;

clear_config:
	dev_pm_opp_clear_config(df->opp_token);
	df->opp_token = 0;
	return ret;
}

static int sunxi_dsufreq_probe(struct platform_device *pdev)
{
	struct sunxi_dsufreq_dev *df;
	struct device *dev = &pdev->dev;
	struct cpufreq_policy *policy, *prev = NULL;
	struct resource *res;
	unsigned int cpu;
	int ret;

	df = devm_kzalloc(dev, sizeof(*df), GFP_KERNEL);
	if (!df)
		return -ENOMEM;

	for_each_possible_cpu(cpu) {
		policy = cpufreq_cpu_get(cpu);
		if (!policy)
			return -EPROBE_DEFER;

		if (policy != prev) {
			if (df->policy_cnt >= POLICY_NUM) {
				cpufreq_cpu_put(policy);
				dev_err(dev, "too many cpufreq policies\n");
				ret = -EINVAL;
				goto put_policies;
			}
			df->policy[df->policy_cnt++] = policy;
			prev = policy;
		} else {
			cpufreq_cpu_put(policy);
		}
	}

	df->dev = dev;
	df->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(df->clk)) {
		ret = PTR_ERR(df->clk);
		dev_err_probe(dev, ret, "failed to get dsu clock\n");
		goto put_policies;
	}

	dsufreq_dev = df;

	ret = dsu_init_freq_table(df);
	if (ret)
		goto clear_global;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res) {
		df->clus_ctrl = devm_ioremap_resource(dev, res);
		if (IS_ERR(df->clus_ctrl)) {
			dev_warn(dev, "clus ctrl map failed, cooling sysfs disabled\n");
			df->clus_ctrl = NULL;
		}
	}

	df->cur_freq = clk_get_rate(df->clk);
	platform_set_drvdata(pdev, df);

	sunxi_set_dsufreq_cb(set_dsufreq_scaling_down, set_dsufreq_scaling_up);

	ret = class_register(&dsufreq_class);
	if (ret) {
		dev_err(dev, "failed to register dsufreq class: %d\n", ret);
		goto clear_cb;
	}

	return 0;

clear_cb:
	sunxi_set_dsufreq_cb(NULL, NULL);
	if (df->opp_token)
		dev_pm_opp_clear_config(df->opp_token);
clear_global:
	dsufreq_dev = NULL;
put_policies:
	while (df->policy_cnt)
		cpufreq_cpu_put(df->policy[--df->policy_cnt]);
	return ret;
}

static void sunxi_dsufreq_remove(struct platform_device *pdev)
{
	struct sunxi_dsufreq_dev *df = platform_get_drvdata(pdev);

	class_unregister(&dsufreq_class);
	sunxi_set_dsufreq_cb(NULL, NULL);

	if (df->opp_token)
		dev_pm_opp_clear_config(df->opp_token);

	while (df->policy_cnt)
		cpufreq_cpu_put(df->policy[--df->policy_cnt]);

	dsufreq_dev = NULL;
}

static const struct of_device_id sunxi_dsufreq_of_match[] = {
	{ .compatible = "allwinner,dsufreq" },
	{ .compatible = "allwinner,sun60i-dsufreq" },
	{ }
};
MODULE_DEVICE_TABLE(of, sunxi_dsufreq_of_match);

static struct platform_driver sunxi_dsufreq_driver = {
	.probe = sunxi_dsufreq_probe,
	.remove = sunxi_dsufreq_remove,
	.driver = {
		.name = "sunxi-dsufreq",
		.of_match_table = sunxi_dsufreq_of_match,
	},
};
module_platform_driver(sunxi_dsufreq_driver);

MODULE_DESCRIPTION("Allwinner DSU frequency scaling");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Allwinner Technology Co.,Ltd.");
