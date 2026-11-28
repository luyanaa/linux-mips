// SPDX-License-Identifier: GPL-2.0
#ifndef GS_VRAM_H
#define GS_VRAM_H

#include <linux/mutex.h>
#include <linux/types.h>

#include <drm/drm_mm.h>

#include <uapi/asm/gs.h>

struct gs_vram {
	struct drm_mm mm;
	struct mutex lock;
	size_t used;
};

struct gs_vram_block;

u32 gs_vram_pages(enum gs_psm psm, u32 width, u32 height);

int gs_vram_init(struct gs_vram *vram);
void gs_vram_fini(struct gs_vram *vram);
struct gs_vram_block *gs_vram_alloc(struct gs_vram *vram,
				     enum gs_psm psm, u32 width, u32 height);
void gs_vram_free(struct gs_vram *vram, struct gs_vram_block *block);
u32 gs_vram_block_offset(const struct gs_vram_block *block);
size_t gs_vram_free_size(struct gs_vram *vram);
u32 gs_vram_block_page(const struct gs_vram_block *block);
u32 gs_vram_block_block(const struct gs_vram_block *block);
size_t gs_vram_block_size(const struct gs_vram_block *block);
bool gs_vram_block_fits(const struct gs_vram_block *block,
			enum gs_psm psm, u32 width, u32 height);

#endif /* GS_VRAM_H */
