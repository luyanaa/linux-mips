// SPDX-License-Identifier: GPL-2.0
#ifndef GS_TRANSFER_H
#define GS_TRANSFER_H

#include <linux/types.h>

#include <uapi/asm/gs.h>

struct drm_framebuffer;
struct gs_device;

int gs_fourcc_to_psm(u32 fourcc, enum gs_psm *psm);

int gs_emit_upload(struct gs_device *gs, struct drm_framebuffer *fb,
		   u32 y, u32 x, u32 width, const u8 *src, size_t len);
int gs_upload_fb(struct gs_device *gs, struct drm_framebuffer *fb);
void gs_set_scanout(struct gs_device *gs, struct drm_framebuffer *fb);

#endif /* GS_TRANSFER_H */
