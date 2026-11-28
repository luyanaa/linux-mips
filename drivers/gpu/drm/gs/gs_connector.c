// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer DRM connector
 *
 * Copyright (C) 2020 Fredrik Noring
 */

#include <linux/device.h>
#include <linux/kernel.h>

#include <drm/drm.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include <drm/drm_connector.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_atomic_state_helper.h>

#include "gs_connector.h"

static const struct drm_display_mode standard_modes[] = {
	/* PAL */
	{
		.name = "640x256@50",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 13500,
		.hdisplay = 640,
		.hsync_start = 701,
		.hsync_end = 764,
		.htotal = 864,
		.vdisplay = 256,
		.vsync_start = 278,
		.vsync_end = 280,
		.vtotal = 314,
		.flags = DRM_MODE_FLAG_BCAST,
	},
	{
		.name = "720x288@50",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 13500,
		.hdisplay = 720,
		.hsync_start = 731,
		.hsync_end = 794,
		.htotal = 864,
		.vdisplay = 288,
		.vsync_start = 291,
		.vsync_end = 294,
		.vtotal = 313,
		.flags = DRM_MODE_FLAG_BCAST,
	},
	{
		.name = "640x512@50i",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 13500,
		.hdisplay = 640,
		.hsync_start = 701,
		.hsync_end = 764,
		.htotal = 864,
		.vdisplay = 512,
		.vsync_start = 553,
		.vsync_end = 558,
		.vtotal = 625,
		.flags = DRM_MODE_FLAG_BCAST | DRM_MODE_FLAG_INTERLACE,
	},
	{
		.name = "720x576@50i",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 13500,
		.hdisplay = 720,
		.hsync_start = 731,
		.hsync_end = 794,
		.htotal = 864,
		.vdisplay = 576,
		.vsync_start = 581,
		.vsync_end = 586,
		.vtotal = 625,
		.flags = DRM_MODE_FLAG_BCAST | DRM_MODE_FLAG_INTERLACE,
	},
	{
		.name = "720x576@50",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 27000,
		.hdisplay = 720,
		.hsync_start = 731,
		.hsync_end = 794,
		.htotal = 864,
		.vdisplay = 576,
		.vsync_start = 581,
		.vsync_end = 586,
		.vtotal = 625,
		.flags = DRM_MODE_FLAG_BCAST,
	},
	{
		.name = "1280x720@50",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 74250,
		.hdisplay = 1280,
		.hsync_start = 1680,
		.hsync_end = 1760,
		.htotal = 1980,
		.vdisplay = 720,
		.vsync_start = 726,
		.vsync_end = 731,
		.vtotal = 750,
		.flags = DRM_MODE_FLAG_BCAST,
	},
	{
		.name = "1920x1080@50i",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 74250,
		.hdisplay = 1920,
		.hsync_start = 2404,
		.hsync_end = 2492,
		.htotal = 2640,
		.vdisplay = 1080,
		.vsync_start = 1084,
		.vsync_end = 1089,
		.vtotal = 1125,
		.flags = DRM_MODE_FLAG_BCAST | DRM_MODE_FLAG_INTERLACE,
	},
	{
		.name = "1920x1080@50",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 148500,
		.hdisplay = 1920,
		.hsync_start = 2404,
		.hsync_end = 2492,
		.htotal = 2640,
		.vdisplay = 1080,
		.vsync_start = 1084,
		.vsync_end = 1089,
		.vtotal = 1125,
		.flags = DRM_MODE_FLAG_BCAST,
	},
	{
		.name = "576x460@50i",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 13500,
		.hdisplay = 576,
		.hsync_start = 659,
		.hsync_end = 722,
		.htotal = 864,
		.vdisplay = 460,
		.vsync_start = 523,
		.vsync_end = 528,
		.vtotal = 625,
		.flags = DRM_MODE_FLAG_BCAST | DRM_MODE_FLAG_INTERLACE,
	},
	{
		.name = "576x460@50",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 27000,
		.hdisplay = 576,
		.hsync_start = 659,
		.hsync_end = 722,
		.htotal = 864,
		.vdisplay = 460,
		.vsync_start = 523,
		.vsync_end = 528,
		.vtotal = 625,
		.flags = DRM_MODE_FLAG_BCAST,
	},
	{
		.name = "1124x644@50",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 74250,
		.hdisplay = 1124,
		.hsync_start = 1602,
		.hsync_end = 1682,
		.htotal = 1980,
		.vdisplay = 644,
		.vsync_start = 688,
		.vsync_end = 693,
		.vtotal = 750,
		.flags = DRM_MODE_FLAG_BCAST,
	},
	{
		.name = "1688x964@50i",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 74250,
		.hdisplay = 1688,
		.hsync_start = 2288,
		.hsync_end = 2376,
		.htotal = 2640,
		.vdisplay = 964,
		.vsync_start = 1026,
		.vsync_end = 1031,
		.vtotal = 1125,
		.flags = DRM_MODE_FLAG_BCAST | DRM_MODE_FLAG_INTERLACE,
	},
	{
		.name = "1688x964@50",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 148500,
		.hdisplay = 1688,
		.hsync_start = 2288,
		.hsync_end = 2376,
		.htotal = 2640,
		.vdisplay = 964,
		.vsync_start = 1026,
		.vsync_end = 1031,
		.vtotal = 1125,
		.flags = DRM_MODE_FLAG_BCAST,
	},
	/* NTSC */
	{
		.name = "640x224@60",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 13500,
		.hdisplay = 640,
		.hsync_start = 700,
		.hsync_end = 763,
		.htotal = 858,
		.vdisplay = 224,
		.vsync_start = 238,
		.vsync_end = 241,
		.vtotal = 263,
		.flags = DRM_MODE_FLAG_BCAST,
	},
	{
		.name = "720x240@60",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 13500,
		.hdisplay = 720,
		.hsync_start = 737,
		.hsync_end = 800,
		.htotal = 858,
		.vdisplay = 240,
		.vsync_start = 245,
		.vsync_end = 248,
		.vtotal = 263,
		.flags = DRM_MODE_FLAG_BCAST,
	},
	{
		.name = "640x448@60i",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 13500,
		.hdisplay = 640,
		.hsync_start = 700,
		.hsync_end = 763,
		.htotal = 858,
		.vdisplay = 448,
		.vsync_start = 475,
		.vsync_end = 481,
		.vtotal = 525,
		.flags = DRM_MODE_FLAG_BCAST | DRM_MODE_FLAG_INTERLACE,
	},
	{
		.name = "720x480@60i",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 13500,
		.hdisplay = 720,
		.hsync_start = 737,
		.hsync_end = 800,
		.htotal = 858,
		.vdisplay = 480,
		.vsync_start = 489,
		.vsync_end = 495,
		.vtotal = 525,
		.flags = DRM_MODE_FLAG_BCAST | DRM_MODE_FLAG_INTERLACE,
	},
	{
		.name = "720x480@60",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 27000,
		.hdisplay = 720,
		.hsync_start = 737,
		.hsync_end = 800,
		.htotal = 858,
		.vdisplay = 480,
		.vsync_start = 489,
		.vsync_end = 495,
		.vtotal = 525,
		.flags = DRM_MODE_FLAG_BCAST,
	},
	{
		.name = "1280x720@60",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 74178,
		.hdisplay = 1280,
		.hsync_start = 1350,
		.hsync_end = 1430,
		.htotal = 1650,
		.vdisplay = 720,
		.vsync_start = 726,
		.vsync_end = 731,
		.vtotal = 750,
		.flags = DRM_MODE_FLAG_BCAST,
	},
	{
		.name = "1920x1080@60i",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 74178,
		.hdisplay = 1920,
		.hsync_start = 1964,
		.hsync_end = 2052,
		.htotal = 2200,
		.vdisplay = 1080,
		.vsync_start = 1084,
		.vsync_end = 1089,
		.vtotal = 1125,
		.flags = DRM_MODE_FLAG_BCAST | DRM_MODE_FLAG_INTERLACE,
	},
	{
		.name = "1920x1080@60",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 148346,
		.hdisplay = 1920,
		.hsync_start = 1964,
		.hsync_end = 2052,
		.htotal = 2200,
		.vdisplay = 1080,
		.vsync_start = 1084,
		.vsync_end = 1089,
		.vtotal = 1125,
		.flags = DRM_MODE_FLAG_BCAST,
	},
	{
		.name = "576x384@60i",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 13500,
		.hdisplay = 576,
		.hsync_start = 665,
		.hsync_end = 728,
		.htotal = 858,
		.vdisplay = 384,
		.vsync_start = 441,
		.vsync_end = 447,
		.vtotal = 525,
		.flags = DRM_MODE_FLAG_BCAST | DRM_MODE_FLAG_INTERLACE,
	},
	{
		.name = "576x384@60",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 27000,
		.hdisplay = 576,
		.hsync_start = 665,
		.hsync_end = 728,
		.htotal = 858,
		.vdisplay = 384,
		.vsync_start = 441,
		.vsync_end = 447,
		.vtotal = 525,
		.flags = DRM_MODE_FLAG_BCAST,
	},
	{
		.name = "1124x644@60",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 74178,
		.hdisplay = 1124,
		.hsync_start = 1272,
		.hsync_end = 1352,
		.htotal = 1650,
		.vdisplay = 644,
		.vsync_start = 688,
		.vsync_end = 693,
		.vtotal = 750,
		.flags = DRM_MODE_FLAG_BCAST,
	},
	{
		.name = "1688x964@60i",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 74178,
		.hdisplay = 1688,
		.hsync_start = 1848,
		.hsync_end = 1936,
		.htotal = 2200,
		.vdisplay = 964,
		.vsync_start = 1026,
		.vsync_end = 1031,
		.vtotal = 1125,
		.flags = DRM_MODE_FLAG_BCAST | DRM_MODE_FLAG_INTERLACE,
	},
	{
		.name = "1688x964@60",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 148346,
		.hdisplay = 1688,
		.hsync_start = 1848,
		.hsync_end = 1936,
		.htotal = 2200,
		.vdisplay = 964,
		.vsync_start = 1026,
		.vsync_end = 1031,
		.vtotal = 1125,
		.flags = DRM_MODE_FLAG_BCAST,
	},
	/* VESA */
	{
		.name = "640x480@60",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 25200,
		.hdisplay = 640,
		.hsync_start = 656,
		.hsync_end = 752,
		.htotal = 800,
		.vdisplay = 480,
		.vsync_start = 490,
		.vsync_end = 492,
		.vtotal = 525,
	},
	{
		.name = "640x480@75",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 31500,
		.hdisplay = 640,
		.hsync_start = 656,
		.hsync_end = 720,
		.htotal = 840,
		.vdisplay = 480,
		.vsync_start = 481,
		.vsync_end = 484,
		.vtotal = 500,
	},
	{
		.name = "800x600@60",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 40000,
		.hdisplay = 800,
		.hsync_start = 840,
		.hsync_end = 968,
		.htotal = 1056,
		.vdisplay = 600,
		.vsync_start = 601,
		.vsync_end = 605,
		.vtotal = 628,
		.flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC,
	},
	{
		.name = "800x600@75",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 49500,
		.hdisplay = 800,
		.hsync_start = 816,
		.hsync_end = 896,
		.htotal = 1056,
		.vdisplay = 600,
		.vsync_start = 601,
		.vsync_end = 604,
		.vtotal = 625,
		.flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC,
	},
	{
		.name = "1024x768@60",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 65003,
		.hdisplay = 1024,
		.hsync_start = 1048,
		.hsync_end = 1184,
		.htotal = 1344,
		.vdisplay = 768,
		.vsync_start = 771,
		.vsync_end = 777,
		.vtotal = 806,
	},
	{
		.name = "1024x768@75",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 78802,
		.hdisplay = 1024,
		.hsync_start = 1040,
		.hsync_end = 1136,
		.htotal = 1312,
		.vdisplay = 768,
		.vsync_start = 769,
		.vsync_end = 772,
		.vtotal = 800,
		.flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC,
	},
	{
		.name = "1280x1024@60",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 108003,
		.hdisplay = 1280,
		.hsync_start = 1328,
		.hsync_end = 1440,
		.htotal = 1688,
		.vdisplay = 1024,
		.vsync_start = 1025,
		.vsync_end = 1028,
		.vtotal = 1066,
		.flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC,
	},
	{
		.name = "1280x1024@75",
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = 135007,
		.hdisplay = 1280,
		.hsync_start = 1296,
		.hsync_end = 1440,
		.htotal = 1688,
		.vdisplay = 1024,
		.vsync_start = 1025,
		.vsync_end = 1028,
		.vtotal = 1066,
		.flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC,
	},
};

static int rename_connector(const char *name, struct drm_connector *connector)
{
	char *s = kasprintf(GFP_KERNEL, name);

	if (!s)
		return -ENOMEM;

	kfree(connector->name);
	connector->name = s;

	return 0;
}

static int gs_get_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	int count = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(standard_modes); i++) {
		struct drm_display_mode *mode =
			drm_mode_duplicate(dev, &standard_modes[i]);

		if (mode != NULL) {
			drm_mode_probed_add(connector, mode);
			count++;
		} else
			dev_dbg(dev->dev, "Failed to duplicate mode\n");
	}

	drm_set_preferred_mode(connector, 576, 460);	/* FIXME: PAL */

	return count;
}

static const struct drm_connector_helper_funcs gs_connector_helper_funcs = {
	.get_modes = gs_get_modes,
};

static const struct drm_connector_funcs gs_connector_funcs = {
	.fill_modes		= drm_helper_probe_single_connector_modes, // FIXME: Skip helper and fill in modes directly?
	.destroy		= drm_connector_cleanup,
	.reset			= drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
};

int gs_connector_init(struct drm_device *dev, struct drm_connector *connector)
{
	int err;

	printk("drm: %s\n", __func__);

	err = drm_connector_init(dev, connector,
		&gs_connector_funcs, DRM_MODE_CONNECTOR_TV);
	if (err)
		goto err_connector_init;

	err = rename_connector("AV-MULTI-OUT", connector);
	if (err) {
		dev_dbg(dev->dev, "Connector rename failed with %d\n", err);
		err = 0;
	}

	connector->interlace_allowed = true;
	connector->doublescan_allowed = true;

	/*
	 * FIXME: Indicate the following subconnectors?
	 *
	 * DRM_MODE_SUBCONNECTOR_Composite
	 * DRM_MODE_SUBCONNECTOR_SVIDEO
	 * DRM_MODE_SUBCONNECTOR_SCART
	 * DRM_MODE_SUBCONNECTOR_VGA
	 * DRM_MODE_SUBCONNECTOR_Component
	 * DRM_MODE_SUBCONNECTOR_DTerminal
	 */

	drm_connector_helper_add(connector, &gs_connector_helper_funcs);

	return 0;

err_connector_init:
	return err;
}
