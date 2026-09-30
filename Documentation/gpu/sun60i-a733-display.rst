.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved.

=============================
Allwinner A733 display output
=============================

The A733 DE352 mixer routes its output to TCON-TV and the DesignWare HDMI
controller through the Video Output 1 timing-controller top block. The
driver exposes one primary plane and an ARGB8888 cursor, with one mixer
output active at a time.

HDMI board setup
================

Include ``sun60i-a733-display.dtsi`` and enable ``de_drm``, ``mixer0``,
``tcon_top``, ``tcon_tv`` and ``hdmi`` in the board description. The DRM
master uses the reserved display CMA pool for DMA allocations. HDMI audio
is connected to I2S3 through the board's simple-audio-card description.

Panel paths are separate from the HDMI pipeline and require their own
controller, PHY and board-specific panel descriptions.
