/* SPDX-License-Identifier: GPL-2.0-or-later WITH Linux-syscall-note */
/*
 * Userspace API for Allwinner cedar-ve (A733 / sun60iw2).
 * Aligned with Tina bsp/include/uapi/linux/cedar_ve_uapi.h so prebuilt
 * libVE.so (ioctl 0x804 = IOCTL_GET_VE_TOP_REG_OFFSET) works.
 */
#ifndef _UAPI_LINUX_CEDAR_VE_UAPI_H_
#define _UAPI_LINUX_CEDAR_VE_UAPI_H_

#include <linux/types.h>

enum IOCTL_CMD {
	IOCTL_UNKOWN = 0x100,
	IOCTL_GET_ENV_INFO,
	IOCTL_WAIT_VE_DE,
	IOCTL_WAIT_VE_EN,
	IOCTL_RESET_VE,
	IOCTL_ENABLE_VE,
	IOCTL_DISABLE_VE,
	IOCTL_SET_VE_FREQ,

	IOCTL_CONFIG_AVS2 = 0x200,
	IOCTL_GETVALUE_AVS2,
	IOCTL_PAUSE_AVS2,
	IOCTL_START_AVS2,
	IOCTL_RESET_AVS2,
	IOCTL_ADJUST_AVS2,
	IOCTL_ENGINE_REQ,
	IOCTL_ENGINE_REL,
	IOCTL_ENGINE_CHECK_DELAY,
	IOCTL_GET_IC_VER,
	IOCTL_ADJUST_AVS2_ABS,
	IOCTL_FLUSH_CACHE,
	IOCTL_SET_REFCOUNT,
	IOCTL_FLUSH_CACHE_ALL,
	IOCTL_TEST_VERSION,

	IOCTL_GET_LOCK = 0x310,
	IOCTL_RELEASE_LOCK,

	IOCTL_SET_VOL = 0x400,

	IOCTL_WAIT_JPEG_DEC = 0x500,
	IOCTL_GET_REFCOUNT,

	IOCTL_GET_IOMMU_ADDR,
	IOCTL_FREE_IOMMU_ADDR,

	IOCTL_MAP_DMA_BUF,
	IOCTL_UNMAP_DMA_BUF,

	IOCTL_FLUSH_CACHE_RANGE = 0x506,

	IOCTL_SET_PROC_INFO,
	IOCTL_STOP_PROC_INFO,
	IOCTL_COPY_PROC_INFO,

	IOCTL_SET_DRAM_HIGH_CHANNAL = 0x600,

	IOCTL_PROC_INFO_COPY = 0x610,
	IOCTL_PROC_INFO_STOP,

	IOCTL_POWER_SETUP = 0x700,
	IOCTL_POWER_SHUTDOWN,

	IOCTL_GET_VE_DEFAULT_FREQ = 0x710,
	IOCTL_UPDATE_CASE_LOAD_PARAM = 0x711,

	IOCTL_ALLOC_PAGES_BUF = 0x720,
	IOCTL_REC_PAGES_BUF,
	IOCTL_FREE_PAGES_BUF,

	IOCTL_VE_MODE = 0x800,
	IOCTL_WAIT_VCU_ENC,			/* 0x801 */
	IOCTL_GET_CSI_ONLINE_INFO,		/* 0x802 */
	IOCTL_CLEAR_EN_INT_FLAG,		/* 0x803 */
	/* Prebuilt libVE.so issues raw ioctl(fd, 0x804, 0) — must stay 0x804. */
	IOCTL_GET_VE_TOP_REG_OFFSET = 0x804,
	IOCTL_WAIT_VCU_DEC,			/* 0x805 */

	IOCTL_RV_STOP = 0x900,

	IOCTL_INVALID_CACHE_RANGE = 0x917,
};

enum VE_DECODER_FORMAT {
	VE_DECODER_FORMAT_H264 = 0,
	VE_DECODER_FORMAT_H265,
	VE_DECODER_FORMAT_VP9,
	VE_DECODER_FORMAT_OTHER,
	VE_DECODER_FORMAT_MAX,
};

struct cedarv_env_infomation {
	unsigned int phymem_start;
	int phymem_total_size;
	__u64 address_macc;
};

struct cedarv_env_infomation_compat {
	unsigned int phymem_start;
	int phymem_total_size;
	__u64 address_macc;
	unsigned int from_kernel;
};

struct user_iommu_param {
	int fd;
	unsigned int iommu_addr;
};

struct dma_buf_param {
	int fd;
	unsigned int phy_addr;
};

struct cache_range {
	__u64 start;
	__u64 end;
};

struct VE_PROC_INFO {
	unsigned char channel_id;
	unsigned int proc_info_len;
};

struct ve_case_load_param {
	int width;
	int height;
	int frame_rate;
	int codec_format;
	int is_remove_cur_param;
	int thread_channel_id;
};

#endif /* _UAPI_LINUX_CEDAR_VE_UAPI_H_ */
