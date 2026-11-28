// SPDX-License-Identifier: GPL-2.0
#ifndef GS_VRAM_H
#define GS_VRAM_H

#include <linux/types.h>

#include <drm/drm_mm.h>

struct gs_vram {
	struct drm_mm mm;
	struct mutex lock;
};

struct gs_vram_block;

int gs_vram_init(struct gs_vram *vram);
void gs_vram_fini(struct gs_vram *vram);
struct gs_vram_block *gs_vram_alloc(struct gs_vram *vram, size_t size);
void gs_vram_free(struct gs_vram *vram, struct gs_vram_block *block);
u32 gs_vram_block_offset(const struct gs_vram_block *block);
size_t gs_vram_block_size(const struct gs_vram_block *block);

#endif /* GS_VRAM_H */

