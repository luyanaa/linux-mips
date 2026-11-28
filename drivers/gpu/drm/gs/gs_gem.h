// SPDX-License-Identifier: GPL-2.0
#ifndef GS_GEM_H
#define GS_GEM_H

#include <linux/types.h>

struct drm_device;
struct drm_gem_object;

struct drm_gem_object *gs_gem_create_object(struct drm_device *dev, size_t size);

#endif /* GS_GEM_H */

