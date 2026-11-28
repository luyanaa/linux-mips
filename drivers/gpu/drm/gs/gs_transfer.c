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

#include <asm/mach-ps2/gif.h>
#include <asm/mach-ps2/gs-registers.h>

#include <uapi/asm/gs.h>

#include "gs_drm.h"
#include "gs_gem.h"
#include "gs_tile.h"
#include "gs_transfer.h"

#define GS_UPLOAD_MAX_QUADS 240

static u32 gs_fb_psm(const struct drm_framebuffer *fb)
{
	switch (fb->format->format) {
	case DRM_FORMAT_RGBA5551:
	case DRM_FORMAT_RGB565:
		return gs_psm_ct16;
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_BGRA8888:
	default:
		return gs_psm_ct32;
	}
}

static u32 gs_fb_bpp(const struct drm_framebuffer *fb)
{
	return fb->format->cpp[0] * 8;
}

static u32 gs_vram_page_addr(u32 offset)
{
	return offset / 2048;
}

int gs_emit_upload(struct gs_device *gs,
	struct drm_framebuffer *fb, u32 y, u32 x, u32 width,
	const u8 *src, size_t len)
{
	union package *base = (union package *)gs->ps2par.package.buffer;
	union package *package = base;
	struct gs_gem_object *obj = to_gs_gem_obj(fb->obj[0]);
	const u32 psm = gs_fb_psm(fb);
	const u32 qwc = len / sizeof(base->gif);
	size_t i;

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
			.dbp = gs_vram_page_addr(gs_vram_block_offset(obj->vram)),
			.dbw = fb->width / GS_FB_PAGE_WIDTH,
			.dpsm = psm,
			.sbp = 0,
			.sbw = fb->width / GS_FB_PAGE_WIDTH,
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
	int err;

	if (!gs || !fb || !fb->obj[0])
		return -EINVAL;

	obj = to_gs_gem_obj(fb->obj[0]);
	shmem = &obj->base;

	if (!obj->vram) {
		obj->vram = gs_vram_alloc(&gs->vram,
					  fb->pitches[0] * fb->height);
		if (IS_ERR(obj->vram)) {
			err = PTR_ERR(obj->vram);
			obj->vram = NULL;
			return err;
		}
	}

	bpp = gs_fb_bpp(fb);
	width = fb->width & ~3u;	/* TRXREG width alignment */
	height = fb->height;
	rowbytes = width * bpp / 8;

	err = shmem->base.funcs->vmap(&shmem->base, &map);
	if (err)
		return err;

	for (y = 0; y < height; y++) {
		const u8 *row = map.vaddr + y * fb->pitches[0];

		for (x = 0; x < rowbytes; x += GS_UPLOAD_MAX_QUADS * 16) {
			size_t len = min_t(size_t,
				GS_UPLOAD_MAX_QUADS * 16, rowbytes - x);

			/* Keep whole quadwords and whole pixels. */
			len &= ~(size_t)(16 * bpp / 8 - 1);
			if (!len)
				continue;

			err = gs_emit_upload(gs, fb, y, 0, len * 8 / bpp,
					      row + x, len);
			if (err)
				goto out;
		}
	}

out:
	shmem->base.funcs->vunmap(&shmem->base, &map);
	return err;
}

void gs_set_scanout(struct gs_device *gs, struct drm_framebuffer *fb)
{
	struct gs_gem_object *obj;

	if (!gs || !fb || !fb->obj[0])
		return;

	obj = to_gs_gem_obj(fb->obj[0]);
	if (!obj->vram)
		return;

	GS_WRITE_DISPFB1(
		.fbp = gs_vram_page_addr(gs_vram_block_offset(obj->vram)),
		.fbw = fb->width / GS_FB_PAGE_WIDTH,
		.psm = gs_fb_psm(fb),
		.dbx = 0,
		.dby = 0
	);
}

