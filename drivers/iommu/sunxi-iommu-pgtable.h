/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * Allwinner's pgtable controler
 *
 * Copyright (c) 2023, ouyangkun <ouyangkun@allwinnertech.com>
 *
 */
#ifndef __SUNXI_IOMMU_PGTALBE__
#define __SUNXI_IOMMU_PGTALBE__
#include <linux/iommu.h>
#include <linux/dma-mapping.h>

#define SUNXI_PHYS_OFFSET 0x40000000UL

#define IOMMU_VA_BITS 34

#define IOMMU_PD_SHIFT 22
#define IOMMU_PD_MASK (~((1ULL << IOMMU_PD_SHIFT) - 1))

#define IOMMU_PT_SHIFT 12
#define IOMMU_PT_MASK (~((1ULL << IOMMU_PT_SHIFT) - 1))

#define SPAGE_SIZE (1 << IOMMU_PT_SHIFT)
#define SPD_SIZE (1 << IOMMU_PD_SHIFT)
#define SPAGE_ALIGN(addr) ALIGN(addr, SPAGE_SIZE)
#define SPDE_ALIGN(addr) ALIGN(addr, SPD_SIZE)

/*
 * cpu phy 0x0000 0000 ~ 0x4000 0000 is reserved for IO access,
 * iommu phy in between 0x0000 0000 ~ 0x4000 0000 should not used
 * as cpu phy directly, move this address space beyond iommu
 * phy max, so iommu phys 0x0000 0000 ~ 0x4000 0000 shoule be
 * iommu_phy_max + 0x0000 0000 ~ iommu_phy_max + 0x4000 0000(as
 * spec said)
 */

static inline dma_addr_t iommu_phy_to_cpu_phy(dma_addr_t iommu_phy)
{
	return iommu_phy < SUNXI_PHYS_OFFSET ?
		       iommu_phy + (1ULL << IOMMU_VA_BITS) :
		       iommu_phy;
}

static inline dma_addr_t cpu_phy_to_iommu_phy(dma_addr_t cpu_phy)
{
	return cpu_phy > (1ULL << IOMMU_VA_BITS) ?
		       cpu_phy - (1ULL << IOMMU_VA_BITS) :
		       cpu_phy;
}

int sunxi_pgtable_prepare_l1_tables(unsigned int *pgtable,
				    dma_addr_t iova_start, dma_addr_t iova_end,
				    int prot);
int sunxi_pgtable_prepare_l2_tables(unsigned int *pgtable,
				    dma_addr_t iova_start, dma_addr_t iova_end,
				    phys_addr_t paddr, int prot);
int sunxi_pgtable_delete_l2_tables(unsigned int *pgtable, dma_addr_t iova_start,
				   dma_addr_t iova_end);
phys_addr_t sunxi_pgtable_iova_to_phys(unsigned int *pgtable, dma_addr_t iova);
int sunxi_pgtable_invalid_helper(unsigned int *pgtable, dma_addr_t iova);
void sunxi_pgtable_clear(unsigned int *pgtable);
unsigned int *sunxi_pgtable_alloc(void);
void sunxi_pgtable_free(unsigned int *pgtable);
ssize_t sunxi_pgtable_dump(unsigned int *pgtable, ssize_t len, char *buf,
			   size_t buf_len, bool for_sysfs_show);
struct kmem_cache *sunxi_pgtable_alloc_pte_cache(void);
void sunxi_pgtable_free_pte_cache(struct kmem_cache *iopte_cache);
void sunxi_pgtable_set_dma_dev(struct device *dma_dev);
#endif
