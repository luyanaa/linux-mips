// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer DRM device
 *
 * Copyright (C) 2020 Fredrik Noring
 */

#ifndef GS_DRM_H
#define GS_DRM_H

#include <drm/drm_device.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_plane.h>
#include <linux/fb.h>

#include "gs_tile.h"
#include "gs_vram.h"

/**
 * struct gs_device - driver specific structure
 */
struct gs_device {
	struct drm_device dev;

	struct drm_crtc crtc;
	struct drm_plane plane;
	struct drm_encoder encoder;
	struct drm_connector connector;

	struct drm_framebuffer framebuffer;

	struct fb_info *info;
	struct ps2fb_par ps2par;
	struct gs_vram vram;
};

#endif /* GS_DRM_H */
