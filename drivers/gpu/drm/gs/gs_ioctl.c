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

#include <drm/drm_fourcc.h>

#include "gs_cmd.h"
#include "gs_drm.h"
#include "gs_gem.h"
#include "gs_vram.h"

static int gs_get_param_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	struct drm_gs_get_param *args = data;

	switch (args->param) {
	case DRM_GS_PARAM_VRAM_SIZE:
		args->value = GS_MEMORY_SIZE;
		return 0;
	case DRM_GS_PARAM_VRAM_FREE: {
		struct gs_device *gs = dev->dev_private;

		args->value = gs_vram_free_size(&gs->vram);
		return 0;
	}
	case DRM_GS_PARAM_NUM_PORTS:
		args->value = 2;
		return 0;
	case DRM_GS_PARAM_SCANOUT_WIDTH: {
		struct gs_device *gs = dev->dev_private;

		args->value = gs->scanout ? gs->scanout->width : 0;
		return 0;
	}
	case DRM_GS_PARAM_SCANOUT_HEIGHT: {
		struct gs_device *gs = dev->dev_private;

		args->value = gs->scanout ? gs->scanout->height : 0;
		return 0;
	}
	case DRM_GS_PARAM_SCANOUT_FOURCC: {
		struct gs_device *gs = dev->dev_private;

		args->value = gs->scanout ?
			gs->scanout->format->format : 0;
		return 0;
	}
	case DRM_GS_PARAM_VU1:
		args->value = 0;	/* VU1 submission not implemented */
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

static int gs_resource_create_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file_priv)
{
	struct gs_device *gs = dev->dev_private;
	struct drm_gs_resource_create *args = data;
	struct drm_gem_object *obj;
	struct gs_gem_object *gs_obj;
	struct gs_vram_block *data_block, *clut_block = NULL;
	enum gs_psm psm = args->psm;
	u32 tbw, clut_width = 0, clut_height = 0;
	int err;

	if (args->flags)
		return -EINVAL;

	if (!gs_vram_pages(psm, args->width, args->height))
		return -EINVAL;

	tbw = args->tbw ? args->tbw : DIV_ROUND_UP(args->width,
						  GS_FB_PAGE_WIDTH);
	if (tbw > 63)
		return -EINVAL;

	if (args->clut_entries) {
		if (args->clut_psm != gs_psm_ct32 &&
		    args->clut_psm != gs_psm_ct16)
			return -EINVAL;
		if (!gs_vram_pages(args->clut_psm, 64, 1))
			return -EINVAL;
		clut_width = 64;
		clut_height = DIV_ROUND_UP(args->clut_entries, 64);
	}

	obj = drm_gem_object_lookup(file_priv, args->bo_handle);
	if (!obj)
		return -ENOENT;

	gs_obj = to_gs_gem_obj(obj);

	mutex_lock(&gs->submit_lock);
	if (gs_obj->res.valid) {
		err = -EEXIST;
		goto out;
	}

	data_block = gs_vram_alloc(&gs->vram, psm, args->width, args->height);
	if (IS_ERR(data_block)) {
		err = PTR_ERR(data_block);
		goto out;
	}

	if (clut_height) {
		clut_block = gs_vram_alloc(&gs->vram, args->clut_psm,
					   clut_width, clut_height);
		if (IS_ERR(clut_block)) {
			err = PTR_ERR(clut_block);
			gs_vram_free(&gs->vram, data_block);
			goto out;
		}
	}

	gs_obj->res.valid = true;
	gs_obj->res.psm = psm;
	gs_obj->res.width = args->width;
	gs_obj->res.height = args->height;
	gs_obj->res.tbw = tbw;
	gs_obj->res.data = data_block;
	gs_obj->res.clut = clut_block;
	gs_obj->res.clut_psm = args->clut_psm;
	gs_obj->res.clut_entries = args->clut_entries;
	err = 0;

out:
	mutex_unlock(&gs->submit_lock);
	drm_gem_object_put(obj);
	return err;
}

static int gs_resource_destroy_ioctl(struct drm_device *dev, void *data,
				     struct drm_file *file_priv)
{
	struct gs_device *gs = dev->dev_private;
	struct drm_gs_resource_create *args = data;
	struct drm_gem_object *obj;
	struct gs_gem_object *gs_obj;
	int err = 0;

	obj = drm_gem_object_lookup(file_priv, args->bo_handle);
	if (!obj)
		return -ENOENT;

	gs_obj = to_gs_gem_obj(obj);

	mutex_lock(&gs->submit_lock);
	if (!gs_obj->res.valid) {
		err = -EINVAL;
		goto out;
	}

	gs_vram_free(&gs->vram, gs_obj->res.data);
	if (gs_obj->res.clut)
		gs_vram_free(&gs->vram, gs_obj->res.clut);

	gs_obj->res.data = NULL;
	gs_obj->res.clut = NULL;
	gs_obj->res.valid = false;

out:
	mutex_unlock(&gs->submit_lock);
	drm_gem_object_put(obj);
	return err;
}

static int gs_submit_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	struct gs_device *gs = dev->dev_private;
	struct drm_gs_submit *args = data;
	u32 sequence = 0;
	int err;

	if (args->flags)
		return -EINVAL;

	err = gs_submit_commands(gs, file_priv, args->cmd_handle,
				 args->cmd_offset, args->cmd_qwords,
				 args->res_count,
				 (const u32 __user *)(unsigned long)args->res_handles,
				 &sequence);
	if (err)
		return err;

	args->sequence = sequence;
	return 0;
}

const struct drm_ioctl_desc gs_ioctls[] = {
	DRM_IOCTL_DEF_DRV(GS_GET_PARAM, gs_get_param_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(GS_WAIT_BO, gs_wait_bo_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(GS_SUBMIT, gs_submit_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(GS_RESOURCE_CREATE, gs_resource_create_ioctl,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(GS_RESOURCE_DESTROY, gs_resource_destroy_ioctl,
			  DRM_RENDER_ALLOW),
};

