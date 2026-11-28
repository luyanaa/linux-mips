/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * PlayStation 2 Graphics Synthesizer DRM UAPI
 *
 * Copyright (C) 2026 Fredrik Noring
 *
 * The driver owns GS local memory, framebuffer/Z-buffer binding, scanout and
 * the GIF/DMA path. User space submits validated GIF display lists and names
 * VRAM resources symbolically; it never sees or supplies a physical GS
 * address.
 */

#ifndef _GS_DRM_H_
#define _GS_DRM_H_

#include <linux/types.h>
#include <drm/drm.h>

#define DRM_GS_GET_PARAM	0x00
#define DRM_GS_WAIT_BO		0x01
#define DRM_GS_SUBMIT		0x02
#define DRM_GS_RESOURCE_CREATE	0x03
#define DRM_GS_RESOURCE_DESTROY	0x04

enum drm_gs_param {
	DRM_GS_PARAM_VRAM_SIZE		= 0,
	DRM_GS_PARAM_VRAM_FREE		= 1,
	DRM_GS_PARAM_NUM_PORTS		= 2,
	DRM_GS_PARAM_SCANOUT_WIDTH	= 3,
	DRM_GS_PARAM_SCANOUT_HEIGHT	= 4,
	DRM_GS_PARAM_SCANOUT_FOURCC	= 5,
	DRM_GS_PARAM_VU1		= 6,
};

struct drm_gs_get_param {
	__u32 param;
	__u32 value;
};

struct drm_gs_wait_bo {
	__u32 handle;
	__u32 __pad;
	__u64 timeout_ns;
};

/**
 * struct drm_gs_resource_create - register a GEM buffer as a GS VRAM resource
 * @bo_handle: dumb GEM buffer handle
 * @width: resource width in pixels
 * @height: resource height in pixels
 * @psm: GS pixel storage mode
 * @tbw: texture base width in 64-pixel units, 0 to derive from @width
 * @clut_psm: CLUT pixel storage mode, 0 for no CLUT
 * @clut_entries: number of CLUT entries
 * @flags: reserved, must be zero
 *
 * The driver allocates the resource's GS local-memory block(s) and keeps
 * their addresses private. Display-list TEX0/BITBLTBUF fields refer to the
 * resource by its index in &drm_gs_submit.res_handles.
 */
struct drm_gs_resource_create {
	__u32 bo_handle;
	__u32 width;
	__u32 height;
	__u32 psm;
	__u32 tbw;
	__u32 clut_psm;
	__u32 clut_entries;
	__u32 flags;
};

/**
 * struct drm_gs_submit - validated GIF display-list submission
 * @cmd_handle: GEM buffer containing 128-bit GIF packets
 * @cmd_offset: byte offset of the first packet, 16-byte aligned
 * @cmd_qwords: number of 128-bit quadwords to execute
 * @res_count: number of entries in @res_handles
 * @res_handles: user pointer to __u32 GEM handles (resources, in slot order)
 * @flags: reserved, must be zero
 * @sequence: out, monotonically increasing submission sequence
 *
 * TEX0 TBP and BITBLTBUF SBP/DBP fields that carry the symbolic marker
 * 0x2000, and TEX0 CBP with marker 0x1000, are replaced by the driver with
 * the resource slot's VRAM block address. Unmarked TEX0 addresses, unmarked
 * BITBLTBUF transfer addresses, FRAME/ZBUF writes and the privileged GS
 * registers are rejected.
 */
struct drm_gs_submit {
	__u32 cmd_handle;
	__u32 cmd_offset;
	__u32 cmd_qwords;
	__u32 res_count;
	__u64 res_handles;
	__u32 flags;
	__u32 sequence;
};

#define DRM_IOCTL_GS_GET_PARAM	DRM_IOWR(DRM_COMMAND_BASE + DRM_GS_GET_PARAM, \
					 struct drm_gs_get_param)
#define DRM_IOCTL_GS_WAIT_BO	DRM_IOWR(DRM_COMMAND_BASE + DRM_GS_WAIT_BO, \
					 struct drm_gs_wait_bo)
#define DRM_IOCTL_GS_SUBMIT	DRM_IOWR(DRM_COMMAND_BASE + DRM_GS_SUBMIT, \
					 struct drm_gs_submit)
#define DRM_IOCTL_GS_RESOURCE_CREATE \
				DRM_IOWR(DRM_COMMAND_BASE + \
					 DRM_GS_RESOURCE_CREATE, \
					 struct drm_gs_resource_create)
#define DRM_IOCTL_GS_RESOURCE_DESTROY \
				DRM_IOWR(DRM_COMMAND_BASE + \
					 DRM_GS_RESOURCE_DESTROY, \
					 struct drm_gs_resource_create)

/* Symbolic resource marker in TEX0/BITBLTBUF address fields. */
#define GS_RES_MARK		0x2000
#define GS_RES_MARK_CLUT	0x1000
#define GS_RES_SLOT_MASK	0x0fff

#endif /* _GS_DRM_H_ */
