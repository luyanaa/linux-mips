// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer DRM GEM objects
 *
 * Copyright (C) 2026 Fredrik Noring
 */

#include <drm/drm_device.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_shmem_helper.h>

#include "gs_drm.h"
#include "gs_gem.h"

static void gs_gem_free(struct drm_gem_object *obj)
{
	struct gs_device *gs = obj->dev->dev_private;
	struct gs_gem_object *gs_obj = to_gs_gem_obj(obj);

	if (gs && gs_obj->vram)
		gs_vram_free(&gs->vram, gs_obj->vram);

	if (gs && gs_obj->res.valid) {
		if (gs_obj->res.data)
			gs_vram_free(&gs->vram, gs_obj->res.data);
		if (gs_obj->res.clut)
			gs_vram_free(&gs->vram, gs_obj->res.clut);
		gs_obj->res.valid = false;
	}

	drm_gem_shmem_free(&gs_obj->base);
}

static const struct drm_gem_object_funcs gs_gem_funcs = {
	.free = gs_gem_free,
	.print_info = drm_gem_shmem_object_print_info,
	.pin = drm_gem_shmem_object_pin,
	.unpin = drm_gem_shmem_object_unpin,
	.get_sg_table = drm_gem_shmem_object_get_sg_table,
	.vmap = drm_gem_shmem_object_vmap,
	.vunmap = drm_gem_shmem_object_vunmap,
	.mmap = drm_gem_shmem_object_mmap,
	.vm_ops = &drm_gem_shmem_vm_ops,
};

struct drm_gem_object *gs_gem_create_object(struct drm_device *dev, size_t size)
{
	struct gs_gem_object *obj;

	obj = kzalloc_obj(*obj);
	if (!obj)
		return ERR_PTR(-ENOMEM);

	obj->base.base.funcs = &gs_gem_funcs;

	return &obj->base.base;
}

