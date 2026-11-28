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
#include <linux/mutex.h>

#include <uapi/asm/gif.h>

#include "gs_vram.h"

#define GIF_PACKAGE_TAG(package) ((package)++)->gif.tag = (struct gif_tag)
#define GIF_PACKAGE_REG(package) ((package)++)->gif.reg = (struct gif_data_reg)
#define GIF_PACKAGE_AD(package)  ((package)++)->gif.packed.ad = (struct gif_packed_ad)

union package {
	union gif_data gif;
};

/**
 * struct gs_device - driver specific structure
 * @dev: DRM device
 * @crtc: display CRTC
 * @plane: primary plane
 * @encoder: display encoder
 * @connector: display connector
 * @package: tags and data for the GIF
 * @package.capacity: maximum number of GIF packages in 16-byte units
 * @package.order: page order of the DMA package buffer
 * @package.buffer: DMA buffer for GIF packages
 * @vram: Graphics Synthesizer local memory allocator
 * @submit_lock: serialises validated command submission
 * @sequence: last submitted command sequence number
 * @scanout: framebuffer currently bound as FRAME by the driver
 */
struct gs_device {
	struct drm_device dev;

	struct drm_crtc crtc;
	struct drm_plane plane;
	struct drm_encoder encoder;
	struct drm_connector connector;

	struct {
		size_t capacity;
		unsigned int order;
		union package *buffer;
	} package;

	struct gs_vram vram;

	struct mutex submit_lock;
	u32 sequence;
	struct drm_framebuffer *scanout;
};

#endif /* GS_DRM_H */
