// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer local memory allocator
 *
 * Copyright (C) 2026 Fredrik Noring
 */

#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include <drm/drm_mm.h>

#include <uapi/asm/gs.h>

#include "gs_vram.h"

struct gs_vram_block {
	struct drm_mm_node node;
	size_t size;
};

int gs_vram_init(struct gs_vram *vram)
{
	mutex_init(&vram->lock);
	drm_mm_init(&vram->mm, 0, GS_MEMORY_SIZE);

	return 0;
}

void gs_vram_fini(struct gs_vram *vram)
{
	mutex_lock(&vram->lock);
	drm_mm_takedown(&vram->mm);
	mutex_unlock(&vram->lock);
	mutex_destroy(&vram->lock);
}

struct gs_vram_block *gs_vram_alloc(struct gs_vram *vram, size_t size)
{
	struct gs_vram_block *block;
	int err;

	if (!size)
		return ERR_PTR(-EINVAL);

	block = kzalloc(sizeof(*block), GFP_KERNEL);
	if (!block)
		return ERR_PTR(-ENOMEM);

	block->size = PAGE_ALIGN(size);

	mutex_lock(&vram->lock);
	err = drm_mm_insert_node_generic(&vram->mm, &block->node,
					 block->size, GS_PAGE_SIZE, 0, 0);
	mutex_unlock(&vram->lock);

	if (err) {
		kfree(block);
		return ERR_PTR(err);
	}

	return block;
}

void gs_vram_free(struct gs_vram *vram, struct gs_vram_block *block)
{
	if (!block)
		return;

	mutex_lock(&vram->lock);
	drm_mm_remove_node(&block->node);
	mutex_unlock(&vram->lock);

	kfree(block);
}

u32 gs_vram_block_offset(const struct gs_vram_block *block)
{
	return block ? block->node.start : 0;
}

size_t gs_vram_block_size(const struct gs_vram_block *block)
{
	return block ? block->size : 0;
}

