// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer local memory allocator
 *
 * Copyright (C) 2026 Fredrik Noring
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include <drm/drm_mm.h>

#include <uapi/asm/gs.h>

#include "gs_vram.h"

struct gs_vram_block {
	struct drm_mm_node node;
	size_t size;
	u32 pages;
	enum gs_psm psm;
	u32 width;
	u32 height;
};

/**
 * gs_psm_page_width - width in pixels of a GS page for a pixel storage mode
 * @psm: pixel storage mode
 *
 * Return: page width in pixels, or zero for unsupported pixel storage modes
 */
static u32 gs_psm_page_width(enum gs_psm psm)
{
	switch (psm) {
	case gs_psm_t8:
	case gs_psm_t8h:
	case gs_psm_t4:
	case gs_psm_t4hl:
	case gs_psm_t4hh:
		return 128;
	case gs_psm_ct32:
	case gs_psm_ct24:
	case gs_psm_gpu24:
	case gs_psm_z32:
	case gs_psm_z24:
	case gs_psm_ct16:
	case gs_psm_ct16s:
	case gs_psm_z16:
	case gs_psm_z16s:
		return 64;
	default:
		return 0;
	}
}

/**
 * gs_psm_page_height - height in pixels of a GS page for a pixel storage mode
 * @psm: pixel storage mode
 *
 * Return: page height in pixels, or zero for unsupported pixel storage modes
 */
static u32 gs_psm_page_height(enum gs_psm psm)
{
	switch (psm) {
	case gs_psm_ct16:
	case gs_psm_ct16s:
	case gs_psm_z16:
	case gs_psm_z16s:
	case gs_psm_t8:
	case gs_psm_t8h:
		return 64;
	case gs_psm_t4:
	case gs_psm_t4hl:
	case gs_psm_t4hh:
		return 128;
	case gs_psm_ct32:
	case gs_psm_ct24:
	case gs_psm_gpu24:
	case gs_psm_z32:
	case gs_psm_z24:
		return 32;
	default:
		return 0;
	}
}

/**
 * gs_vram_pages - number of GS pages occupied by a buffer
 * @psm: pixel storage mode
 * @width: buffer width in pixels
 * @height: buffer height in pixels
 *
 * The Graphics Synthesizer addresses memory as a grid of 8 KiB pages. A
 * buffer occupies whole pages for its width and height; the unused blocks in
 * the last page row cannot be reused by another buffer.
 *
 * Return: number of pages, or zero for unsupported modes or dimensions
 */
u32 gs_vram_pages(enum gs_psm psm, u32 width, u32 height)
{
	const u32 page_width = gs_psm_page_width(psm);
	const u32 page_height = gs_psm_page_height(psm);

	if (!page_width || !page_height || !width || !height)
		return 0;

	return DIV_ROUND_UP(width, page_width) *
	       DIV_ROUND_UP(height, page_height);
}

int gs_vram_init(struct gs_vram *vram)
{
	mutex_init(&vram->lock);
	drm_mm_init(&vram->mm, 0, GS_MEMORY_SIZE);
	vram->used = 0;

	return 0;
}

void gs_vram_fini(struct gs_vram *vram)
{
	mutex_lock(&vram->lock);
	drm_mm_takedown(&vram->mm);
	mutex_unlock(&vram->lock);
	mutex_destroy(&vram->lock);
}

struct gs_vram_block *gs_vram_alloc(struct gs_vram *vram,
				    enum gs_psm psm, u32 width, u32 height)
{
	struct gs_vram_block *block;
	const u32 pages = gs_vram_pages(psm, width, height);
	int err;

	if (!pages || pages > GS_PAGE_COUNT)
		return ERR_PTR(-EINVAL);

	block = kzalloc(sizeof(*block), GFP_KERNEL);
	if (!block)
		return ERR_PTR(-ENOMEM);

	block->pages = pages;
	block->size = (size_t)pages * GS_PAGE_SIZE;
	block->psm = psm;
	block->width = width;
	block->height = height;

	mutex_lock(&vram->lock);
	err = drm_mm_insert_node_generic(&vram->mm, &block->node,
					 block->size, GS_PAGE_SIZE, 0, 0);
	mutex_unlock(&vram->lock);

	if (err) {
		kfree(block);
		return ERR_PTR(err);
	}

	vram->used += block->size;

	return block;
}

void gs_vram_free(struct gs_vram *vram, struct gs_vram_block *block)
{
	if (!block)
		return;

	mutex_lock(&vram->lock);
	drm_mm_remove_node(&block->node);
	vram->used -= block->size;
	mutex_unlock(&vram->lock);

	kfree(block);
}

bool gs_vram_block_fits(const struct gs_vram_block *block,
			enum gs_psm psm, u32 width, u32 height)
{
	const u32 pages = gs_vram_pages(psm, width, height);

	return block && block->psm == psm && pages && pages <= block->pages;
}

u32 gs_vram_block_offset(const struct gs_vram_block *block)
{
	return block ? (u32)block->node.start : 0;
}

size_t gs_vram_free_size(struct gs_vram *vram)
{
	size_t used;

	mutex_lock(&vram->lock);
	used = vram->used;
	mutex_unlock(&vram->lock);

	return GS_MEMORY_SIZE - used;
}

/**
 * gs_vram_block_page - DISPFB page address of a VRAM allocation
 * @block: VRAM allocation
 *
 * The DISPFB FBP field has 9 bits and addresses 8 KiB GS pages.
 *
 * Return: page index, or zero without an allocation
 */
u32 gs_vram_block_page(const struct gs_vram_block *block)
{
	return block ? (u32)block->node.start / GS_PAGE_SIZE : 0;
}

/**
 * gs_vram_block_block - BITBLTBUF block address of a VRAM allocation
 * @block: VRAM allocation
 *
 * The BITBLTBUF DBP field has 14 bits and addresses 256-byte GS blocks.
 *
 * Return: block index, or zero without an allocation
 */
u32 gs_vram_block_block(const struct gs_vram_block *block)
{
	return block ? (u32)block->node.start / GS_BLOCK_SIZE : 0;
}

size_t gs_vram_block_size(const struct gs_vram_block *block)
{
	return block ? block->size : 0;
}
