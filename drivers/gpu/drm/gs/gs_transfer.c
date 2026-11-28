// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer scanout uploads
 *
 * Copyright (C) 2026 Fredrik Noring
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>

#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_print.h>

#include <asm/mach-ps2/gif.h>
#include <asm/mach-ps2/gs-registers.h>

#include <uapi/asm/gs.h>

#include "gs_drm.h"
#include "gs_gem.h"
#include "gs_transfer.h"
#include "gs_vram.h"

#define GS_UPLOAD_MAX_QUADS 240

/**
 * gs_fourcc_to_psm - GS pixel storage mode for a DRM fourcc format
 * @fourcc: DRM fourcc format
 * @psm: resulting GS pixel storage mode
 *
 * Return: 0 on success, otherwise a negative error number
 */
int gs_fourcc_to_psm(u32 fourcc, enum gs_psm *psm)
{
	switch (fourcc) {
	case DRM_FORMAT_ABGR1555:
	case DRM_FORMAT_XBGR1555:
		*psm = gs_psm_ct16;
		return 0;
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_XRGB8888:	/* swizzled by gs_upload_fb() */
		*psm = gs_psm_ct32;
		return 0;
	default:
		return -EINVAL;
	}
}

static int gs_fb_psm(const struct drm_framebuffer *fb, enum gs_psm *psm)
{
	return gs_fourcc_to_psm(fb->format->format, psm);
}

static u32 gs_fb_bpp(const struct drm_framebuffer *fb)
{
	return fb->format->cpp[0] * 8;
}

/*
 * GS PSMCT32 stores A:B:G:R, which is DRM_FORMAT_ABGR8888. fbdev asks for
 * DRM_FORMAT_XRGB8888, so swap red and blue while uploading its console
 * buffer instead of exposing a BGR console.
 */
static bool gs_fb_swizzle_rb(const struct drm_framebuffer *fb)
{
	return fb->format->format == DRM_FORMAT_XRGB8888;
}

static void gs_swizzle_rb32(u8 *dst, const u8 *src, size_t len)
{
	size_t i;

	for (i = 0; i + 4 <= len; i += 4) {
		dst[i + 0] = src[i + 2];
		dst[i + 1] = src[i + 1];
		dst[i + 2] = src[i + 0];
		dst[i + 3] = 0xff;
	}
}

/**
 * gs_fb_width - TRXREG aligned upload width
 * @fb: framebuffer to upload
 * @psm: pixel storage mode
 *
 * TRXREG width must be a multiple of 2 for PSMCT32 and a multiple of 4 for
 * PSMCT16.
 *
 * Return: upload width in pixels
 */
static u32 gs_fb_width(const struct drm_framebuffer *fb, enum gs_psm psm)
{
	const u32 align = psm == gs_psm_ct16 ? 4 : 2;

	return fb->width & ~(align - 1);
}

int gs_emit_upload(struct gs_device *gs,
	struct drm_framebuffer *fb, u32 y, u32 x, u32 width,
	const u8 *src, size_t len)
{
	union package *base = (union package *)gs->package.buffer;
	union package *package = base;
	struct gs_gem_object *obj = to_gs_gem_obj(fb->obj[0]);
	const u32 fbw = DIV_ROUND_UP(fb->width, GS_FB_PAGE_WIDTH);
	const u32 qwc = len / sizeof(base->gif);
	enum gs_psm psm;
	size_t i;

	if (gs_fb_psm(fb, &psm))
		return -EINVAL;

	if (!qwc || qwc > GS_UPLOAD_MAX_QUADS)
		return -EINVAL;

	GIF_PACKAGE_TAG(package) {
		.flg = gif_packed_mode,
		.reg0 = gif_reg_ad,
		.nreg = 1,
		.nloop = 4
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_bitbltbuf,
		.data.bitbltbuf = {
			.dbp = gs_vram_block_block(obj->vram),
			.dbw = fbw,
			.dpsm = psm,
			.sbp = 0,
			.sbw = fbw,
			.spsm = psm
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_trxpos,
		.data.trxpos = {
			.dsax = x,
			.dsay = y
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_trxreg,
		.data.trxreg = {
			.rrw = width,
			.rrh = 1
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_trxdir,
		.data.trxdir = { .xdir = gs_trxdir_host_to_local }
	};

	GIF_PACKAGE_TAG(package) {
		.flg = gif_image_mode,
		.nloop = qwc,
		.eop = 1
	};

	for (i = 0; i < qwc; i++)
		memcpy(package[i].gif.image, src + i * sizeof(base->gif),
		       sizeof(base->gif));
	package += qwc;

	return gif_submit((union gif_data *)base, package - base);
}

int gs_upload_fb(struct gs_device *gs, struct drm_framebuffer *fb)
{
	struct gs_gem_object *obj;
	struct drm_gem_shmem_object *shmem;
	struct iosys_map map;
	u32 width, height, bpp, rowbytes, x, y;
	enum gs_psm psm;
	bool swizzle;
	u8 *tmp = NULL;
	int err;

	if (!gs || !fb || !fb->obj[0])
		return -EINVAL;

	err = gs_fb_psm(fb, &psm);
	if (err)
		return err;

	obj = to_gs_gem_obj(fb->obj[0]);
	shmem = &obj->base;

	if (!obj->vram) {
		obj->vram = gs_vram_alloc(&gs->vram, psm,
					  fb->width, fb->height);
		if (IS_ERR(obj->vram)) {
			err = PTR_ERR(obj->vram);
			obj->vram = NULL;
			return err;
		}
	} else if (!gs_vram_block_fits(obj->vram, psm,
				       fb->width, fb->height)) {
		DRM_ERROR("VRAM reservation is too small for %ux%u PSM %u\n",
			  fb->width, fb->height, psm);
		return -EINVAL;
	}

	bpp = gs_fb_bpp(fb);
	width = gs_fb_width(fb, psm);
	height = fb->height;
	rowbytes = width * bpp / 8;

	err = shmem->base.funcs->vmap(&shmem->base, &map);
	if (err)
		return err;

	swizzle = gs_fb_swizzle_rb(fb);
	if (swizzle) {
		tmp = kmalloc(GS_UPLOAD_MAX_QUADS * 16, GFP_KERNEL);
		if (!tmp) {
			err = -ENOMEM;
			goto out_unmap;
		}
	}

	/* The GIF staging page is shared with validated submissions. */
	mutex_lock(&gs->submit_lock);

	for (y = 0; y < height; y++) {
		const u8 *row = map.vaddr + y * fb->pitches[0];

		for (x = 0; x < rowbytes; x += GS_UPLOAD_MAX_QUADS * 16) {
			size_t len = min_t(size_t,
				GS_UPLOAD_MAX_QUADS * 16, rowbytes - x);

			/* Keep whole quadwords and whole pixels. */
			len &= ~(size_t)(16 * bpp / 8 - 1);
			if (!len)
				continue;

			if (swizzle) {
				gs_swizzle_rb32(tmp, row + x, len);
				err = gs_emit_upload(gs, fb, y, x * 8 / bpp,
						     len * 8 / bpp, tmp, len);
			} else {
				err = gs_emit_upload(gs, fb, y, x * 8 / bpp,
						     len * 8 / bpp, row + x, len);
			}
			if (err)
				goto out;
		}
	}

out:
	mutex_unlock(&gs->submit_lock);
out_unmap:
	kfree(tmp);
	shmem->base.funcs->vunmap(&shmem->base, &map);
	return err;
}

void gs_set_scanout(struct gs_device *gs, struct drm_framebuffer *fb)
{
	struct gs_gem_object *obj;
	enum gs_psm psm;

	if (!gs || !fb || !fb->obj[0])
		return;

	if (gs_fb_psm(fb, &psm))
		return;

	obj = to_gs_gem_obj(fb->obj[0]);
	if (!obj->vram)
		return;

	GS_WRITE_DISPFB1(
		.fbp = gs_vram_block_page(obj->vram),
		.fbw = DIV_ROUND_UP(fb->width, GS_FB_PAGE_WIDTH),
		.psm = psm,
		.dbx = 0,
		.dby = 0
	);
}
