/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _GS_DRM_H_
#define _GS_DRM_H_

#include <linux/types.h>

#define DRM_GS_GET_PARAM	0x00
#define DRM_GS_WAIT_BO		0x01
#define DRM_GS_SUBMIT		0x02

enum drm_gs_param {
	DRM_GS_PARAM_VRAM_SIZE	= 0,
	DRM_GS_PARAM_NUM_PORTS	= 1,
};

enum drm_gs_opcode {
	DRM_GS_OP_UPLOAD	= 0,
	DRM_GS_OP_DOWNLOAD	= 1,
	DRM_GS_OP_COPY		= 2,
	DRM_GS_OP_SOLID		= 3,
	DRM_GS_OP_SPRITE	= 4,
	DRM_GS_OP_DRAW		= 5,
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

struct drm_gs_submit {
	__u32 op;
	__u32 bo_handle;
	__u32 src_handle;
	__u32 dst_handle;
	__u32 x;
	__u32 y;
	__u32 width;
	__u32 height;
};

#define DRM_IOCTL_GS_GET_PARAM	DRM_IOWR(DRM_COMMAND_BASE + DRM_GS_GET_PARAM, \
					 struct drm_gs_get_param)
#define DRM_IOCTL_GS_WAIT_BO	DRM_IOWR(DRM_COMMAND_BASE + DRM_GS_WAIT_BO, \
					 struct drm_gs_wait_bo)
#define DRM_IOCTL_GS_SUBMIT	DRM_IOWR(DRM_COMMAND_BASE + DRM_GS_SUBMIT, \
					 struct drm_gs_submit)

#endif /* _GS_DRM_H_ */

