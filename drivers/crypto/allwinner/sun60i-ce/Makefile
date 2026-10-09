# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved.
#
# Allwinner sun60i-ce — layout mirrors sun8i-ce (upstream crypto_engine driver).

obj-$(CONFIG_CRYPTO_DEV_SUN60I_CE) += sun60i-ce.o
sun60i-ce-y += sun60i-ce-core.o sun60i-ce-cipher.o
sun60i-ce-$(CONFIG_CRYPTO_DEV_SUN60I_CE_HASH) += sun60i-ce-hash.o
sun60i-ce-$(CONFIG_CRYPTO_DEV_SUN60I_CE_PRNG) += sun60i-ce-prng.o
sun60i-ce-$(CONFIG_CRYPTO_DEV_SUN60I_CE_TRNG) += sun60i-ce-trng.o
