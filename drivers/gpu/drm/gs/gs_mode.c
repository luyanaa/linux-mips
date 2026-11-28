// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer DRM mode configuration
 *
 * Copyright (C) 2020 Fredrik Noring
 */

#include <linux/kernel.h>

#include <drm/drm.h>
#include <drm/drm_mode_config.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_atomic_helper.h>

#include <uapi/asm/gs.h>	/* FIXME */

#include "gs_mode.h"
#include "gs_transfer.h"
#include "gs_vram.h"

/**
 * gs_pixel_format_fits - does the given format fit in GS local memory?
 * @width: width of framebuffer in pixels
 * @height: height of framebuffer in pixels
 * @pixel_format: fourcc code pixel format
 *
 * GS memory is addressed as a grid of 8 KiB pages, so the byte size alone is
 * not a sufficient check: the last page row of a buffer occupies whole pages
 * and cannot be shared.
 *
 * Return: %true if the resolution fits, otherwise %false
 */
static bool gs_pixel_format_fits(const u32 width, const u32 height,
				 const u32 pixel_format)
{
	enum gs_psm psm;
	u32 pages;

	if (gs_fourcc_to_psm(pixel_format, &psm))
		return false;

	pages = gs_vram_pages(psm, width, height);

	return pages && pages <= GS_PAGE_COUNT;
}

static struct drm_framebuffer *gs_fb_create(
	struct drm_device *dev, struct drm_file *file_priv,
	const struct drm_format_info *format,
	const struct drm_mode_fb_cmd2 *mode_cmd)
{
	printk("drm: %s\n", __func__);		/* FIXME */

	if (!gs_pixel_format_fits(mode_cmd->width, mode_cmd->height,
			mode_cmd->pixel_format))
		return ERR_PTR(-EINVAL);

	/* FIXME: See drm_fb_helper_alloc_fbi in udlfb_create */

	return drm_gem_fb_create_with_dirty(dev, file_priv, format, mode_cmd);	/* FIXME */
}

static const struct drm_mode_config_funcs gs_mode_config_funcs = {
	.fb_create = gs_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static const struct drm_mode_config_helper_funcs gs_mode_config_helpers = {
	.atomic_commit_tail = drm_atomic_helper_commit_tail_rpm,
};

void gs_mode_config_init(struct drm_device *dev)
{
	drm_mode_config_init(dev);

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.max_width = 2048;
	dev->mode_config.max_height = 2048;
	dev->mode_config.preferred_depth = 32;
	dev->mode_config.prefer_shadow = 0;

	dev->mode_config.funcs = &gs_mode_config_funcs;
	dev->mode_config.helper_private = &gs_mode_config_helpers;
}
