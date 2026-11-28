// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer DRM ioctls
 *
 * Copyright (C) 2026 Fredrik Noring
 */

#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/types.h>

#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_ioctl.h>

#include <uapi/asm/gs.h>
#include <uapi/drm/gs_drm.h>

static int gs_get_param_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	struct drm_gs_get_param *args = data;

	switch (args->param) {
	case DRM_GS_PARAM_VRAM_SIZE:
		args->value = GS_MEMORY_SIZE;
		return 0;
	case DRM_GS_PARAM_NUM_PORTS:
		args->value = 2;
		return 0;
	default:
		return -EINVAL;
	}
}

static int gs_wait_bo_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file_priv)
{
	struct drm_gs_wait_bo *args = data;
	struct drm_gem_object *obj;
	long ret;

	obj = drm_gem_object_lookup(file_priv, args->handle);
	if (!obj)
		return -ENOENT;

	ret = dma_resv_wait_timeout(obj->resv, DMA_RESV_USAGE_WRITE, true,
				    nsecs_to_jiffies(args->timeout_ns));
	drm_gem_object_put(obj);

	return ret > 0 ? 0 : -ETIMEDOUT;
}

static int gs_submit_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	/* Validated command submission is not implemented yet. */
	return -ENOSYS;
}

const struct drm_ioctl_desc gs_ioctls[] = {
	DRM_IOCTL_DEF_DRV(GS_GET_PARAM, gs_get_param_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(GS_WAIT_BO, gs_wait_bo_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(GS_SUBMIT, gs_submit_ioctl, DRM_RENDER_ALLOW),
};

