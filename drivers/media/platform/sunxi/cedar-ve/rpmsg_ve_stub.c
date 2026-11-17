/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Stub AMP/rpmsg helpers when CONFIG_RPMSG is disabled.
 */
#include <linux/types.h>
#include "rpmsg_ve.h"

void amp_ve_init(void *ops)
{
	(void)ops;
}

void amp_ve_exit(void)
{
}
