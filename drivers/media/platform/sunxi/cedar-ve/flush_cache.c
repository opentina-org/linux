/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Portable cache maintenance for cedar-ve on arm64 (Linux 6.18+).
 * Replaces vendor flush_cache.S which depends on removed asm-generic/export.h.
 */
#include <linux/types.h>
#include <asm/cacheflush.h>
#include "cedar_ve.h"

void cedar_dma_flush_range(const void *start, const void *end)
{
	unsigned long s = (unsigned long)start;
	unsigned long e = (unsigned long)end;

	if (e <= s)
		return;

	dcache_clean_inval_poc(s, e);
}
