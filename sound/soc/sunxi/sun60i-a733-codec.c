// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * sun60i-a733-codec.c - Allwinner A733 (sun60iw2) audio codec driver
 *
 * The A733 SoC does not have an internal analog audio codec.
 * This driver provides a minimal ASoC codec component that exposes
 * digital-only playback and capture streams, suitable for use with
 * the on-chip I2S controllers when an external codec is not present
 * or for loopback / HDMI-audio passthrough scenarios.
 *
 * Copyright (c) 2025 Allwinner Technology Co.,Ltd.
 *
 * Based on driver_ref snd_sunxi_codec_hdmi.c / snd_sunxi_codec_av.c
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>

#define DRV_NAME	"sun60i-a733-codec"

struct sun60i_a733_codec {
	unsigned int fmt;
};

static int sun60i_a733_codec_hw_params(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *params,
				       struct snd_soc_dai *dai)
{
	return 0;
}

static int sun60i_a733_codec_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	return 0;
}

static const struct snd_soc_dai_ops sun60i_a733_codec_dai_ops = {
	.hw_params	= sun60i_a733_codec_hw_params,
	.set_fmt	= sun60i_a733_codec_set_fmt,
};

static struct snd_soc_dai_driver sun60i_a733_codec_dai = {
	.name = DRV_NAME,
	.playback = {
		.stream_name	= "Playback",
		.channels_min	= 1,
		.channels_max	= 8,
		.rates		= SNDRV_PCM_RATE_8000_192000,
		.formats	= SNDRV_PCM_FMTBIT_S16_LE |
				  SNDRV_PCM_FMTBIT_S20_3LE |
				  SNDRV_PCM_FMTBIT_S24_LE |
				  SNDRV_PCM_FMTBIT_S32_LE,
	},
	.capture = {
		.stream_name	= "Capture",
		.channels_min	= 1,
		.channels_max	= 8,
		.rates		= SNDRV_PCM_RATE_8000_192000,
		.formats	= SNDRV_PCM_FMTBIT_S16_LE |
				  SNDRV_PCM_FMTBIT_S20_3LE |
				  SNDRV_PCM_FMTBIT_S24_LE |
				  SNDRV_PCM_FMTBIT_S32_LE,
	},
	.ops = &sun60i_a733_codec_dai_ops,
	.symmetric_rate = 1,
};

static const struct snd_soc_dapm_widget sun60i_a733_codec_widgets[] = {
	SND_SOC_DAPM_OUTPUT("LINEOUT"),
	SND_SOC_DAPM_INPUT("LINEIN"),
};

static const struct snd_soc_dapm_route sun60i_a733_codec_routes[] = {
	{ "LINEOUT",	NULL,	"Playback" },
	{ "Capture",	NULL,	"LINEIN" },
};

static const struct snd_soc_component_driver sun60i_a733_codec_component = {
	.name			= DRV_NAME,
	.dapm_widgets		= sun60i_a733_codec_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(sun60i_a733_codec_widgets),
	.dapm_routes		= sun60i_a733_codec_routes,
	.num_dapm_routes	= ARRAY_SIZE(sun60i_a733_codec_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static int sun60i_a733_codec_probe(struct platform_device *pdev)
{
	struct sun60i_a733_codec *codec;

	codec = devm_kzalloc(&pdev->dev, sizeof(*codec), GFP_KERNEL);
	if (!codec)
		return -ENOMEM;

	platform_set_drvdata(pdev, codec);

	return devm_snd_soc_register_component(&pdev->dev,
					       &sun60i_a733_codec_component,
					       &sun60i_a733_codec_dai, 1);
}

static const struct of_device_id sun60i_a733_codec_of_match[] = {
	{ .compatible = "allwinner,sun60i-a733-codec" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sun60i_a733_codec_of_match);

static struct platform_driver sun60i_a733_codec_driver = {
	.driver = {
		.name		= DRV_NAME,
		.of_match_table	= sun60i_a733_codec_of_match,
	},
	.probe = sun60i_a733_codec_probe,
};
module_platform_driver(sun60i_a733_codec_driver);

MODULE_AUTHOR("Allwinner Technology Co.,Ltd.");
MODULE_DESCRIPTION("Allwinner A733 (sun60iw2) ASoC codec driver");
MODULE_LICENSE("GPL");
