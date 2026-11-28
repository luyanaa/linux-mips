// SPDX-License-Identifier: GPL-2.0
/*
 * Placeholder VDSO for builds where the bare-metal R5900 toolchain cannot
 * link a shared object.  User space falls back to system calls.
 */
#include <asm/vdso.h>

struct mips_vdso_image vdso_image = {
	.mapping = {
		.name = "[vdso]",
	},
};

#ifdef CONFIG_MIPS32_O32
struct mips_vdso_image vdso_image_o32 = {
	.mapping = {
		.name = "[vdso]",
	},
};
#endif

#ifdef CONFIG_MIPS32_N32
struct mips_vdso_image vdso_image_n32 = {
	.mapping = {
		.name = "[vdso]",
	},
};
#endif
