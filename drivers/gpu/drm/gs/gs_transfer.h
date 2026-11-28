// SPDX-License-Identifier: GPL-2.0
#ifndef GS_TRANSFER_H
#define GS_TRANSFER_H

struct drm_framebuffer;
struct gs_device;

int gs_upload_fb(struct gs_device *gs, struct drm_framebuffer *fb);
void gs_set_scanout(struct gs_device *gs, struct drm_framebuffer *fb);

#endif /* GS_TRANSFER_H */

