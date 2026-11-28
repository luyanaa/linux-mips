// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer DRM GEM objects
 *
 * Copyright (C) 2026 Fredrik Noring
 */

#include <drm/drm_device.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_shmem_helper.h>

#include "gs_gem.h"

struct drm_gem_object *gs_gem_create_object(struct drm_device *dev, size_t size)
{
	struct drm_gem_shmem_object *obj;

	obj = kzalloc_obj(*obj);
	if (!obj)
		return ERR_PTR(-ENOMEM);

	return &obj->base;
}

