/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SUNXI_DSUFREQ_H__
#define __SUNXI_DSUFREQ_H__

#include <linux/cpufreq.h>

void sunxi_set_dsufreq_cb(int (*scaling_down_cb)(struct cpufreq_policy *,
						 unsigned long),
			  int (*scaling_up_cb)(struct cpufreq_policy *,
					       unsigned long, int));

const char *sun60i_cpufreq_get_opp_prop(void);
bool sun60i_cpufreq_is_26m_crystal(void);

#endif /* __SUNXI_DSUFREQ_H__ */
