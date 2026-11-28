// SPDX-License-Identifier: GPL-2.0
#ifndef GS_GEM_H
#define GS_GEM_H

#include <linux/types.h>

#include <drm/drm_device.h>
#include <drm/drm_gem_shmem_helper.h>

#include "gs_vram.h"

struct drm_gem_object;

struct gs_gem_object {
	struct drm_gem_shmem_object base;
	struct gs_vram_block *vram;
};

static inline struct gs_gem_object *to_gs_gem_obj(struct drm_gem_object *obj)
{
	return container_of(obj, struct gs_gem_object, base.base);
}

struct drm_gem_object *gs_gem_create_object(struct drm_device *dev, size_t size);

#endif /* GS_GEM_H */

