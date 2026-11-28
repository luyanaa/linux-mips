/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PlayStation 2 Graphics Synthesizer validated command submission
 *
 * Copyright (C) 2026 Fredrik Noring
 */

#ifndef GS_CMD_H
#define GS_CMD_H

#include <linux/types.h>

struct drm_device;
struct drm_file;
struct gs_device;

#define GS_SUBMIT_MAX_RESOURCES 32

int gs_submit_init(struct gs_device *gs);
void gs_submit_fini(struct gs_device *gs);

int gs_submit_commands(struct gs_device *gs, struct drm_file *file_priv,
		       u32 cmd_handle, u32 cmd_offset, u32 cmd_qwords,
		       u32 res_count, const u32 __user *res_handles,
		       u32 *sequence);

#endif /* GS_CMD_H */
