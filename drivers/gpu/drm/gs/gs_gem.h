// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer DRM resources
 *
 * Copyright (C) 2026 Fredrik Noring
 */

#ifndef GS_GEM_H
#define GS_GEM_H

#include <linux/types.h>

#include <drm/drm_device.h>
#include <drm/drm_gem_shmem_helper.h>

#include <uapi/asm/gs.h>

#include "gs_vram.h"

struct drm_gem_object;

/**
 * struct gs_resource - user-space VRAM resource
 * @valid: resource has allocated GS local memory
 * @psm: pixel storage mode
 * @width: width in pixels
 * @height: height in pixels
 * @tbw: texture base width in 64-pixel units
 * @data: GS local-memory block for the pixels
 * @clut: optional GS local-memory block for the CLUT
 * @clut_psm: CLUT pixel storage mode
 * @clut_entries: number of CLUT entries
 */
struct gs_resource {
	bool valid;
	enum gs_psm psm;
	u32 width;
	u32 height;
	u32 tbw;
	struct gs_vram_block *data;
	struct gs_vram_block *clut;
	enum gs_psm clut_psm;
	u32 clut_entries;
};

struct gs_gem_object {
	struct drm_gem_shmem_object base;
	struct gs_vram_block *vram;
	struct gs_resource res;
};

static inline struct gs_gem_object *to_gs_gem_obj(struct drm_gem_object *obj)
{
	return container_of(obj, struct gs_gem_object, base.base);
}

struct drm_gem_object *gs_gem_create_object(struct drm_device *dev, size_t size);

#endif /* GS_GEM_H */
