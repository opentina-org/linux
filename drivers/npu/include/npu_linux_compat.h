/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Compatibility shims for Allwinner NPU drivers on newer kernels.
 */
#ifndef _NPU_LINUX_COMPAT_H_
#define _NPU_LINUX_COMPAT_H_

#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/version.h>

#ifndef nth_page
#define nth_page(page, n)	((page) + (n))
#endif

#ifndef MAX_ORDER
#define MAX_ORDER		MAX_PAGE_ORDER
#endif

/* x86-only page attribute helpers; no-ops on other arches. */
#ifndef set_pages_array_uc
static inline int set_pages_array_uc(struct page **pages, int addrinarray)
{
	return 0;
}
#endif
#ifndef set_pages_array_wc
static inline int set_pages_array_wc(struct page **pages, int addrinarray)
{
	return 0;
}
#endif
#ifndef set_pages_array_wb
static inline int set_pages_array_wb(struct page **pages, int addrinarray)
{
	return 0;
}
#endif

/*
 * follow_pfn() was removed; wrap follow_pfnmap_* for legacy callers.
 */
#ifndef follow_pfn
static inline int follow_pfn(struct vm_area_struct *vma, unsigned long address,
			     unsigned long *pfn)
{
	struct follow_pfnmap_args args = {
		.vma = vma,
		.address = address,
	};
	int ret;

	ret = follow_pfnmap_start(&args);
	if (ret)
		return ret;
	*pfn = args.pfn;
	follow_pfnmap_end(&args);
	return 0;
}
#endif

#endif /* _NPU_LINUX_COMPAT_H_ */
