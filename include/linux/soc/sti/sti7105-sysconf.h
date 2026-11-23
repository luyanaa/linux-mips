/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __STI7105_SYSCONF_H
#define __STI7105_SYSCONF_H

#include <linux/types.h>

/*
 * STi7105 system configuration, physical 0xfe001000. SYS_STA starts at
 * 0x008 and SYS_CFG at 0x100. INTC2 occupies the same page from 0x300,
 * so a sysconf mapping must stop at 0x1e0.
 */
#define STI7105_SYSCONF_PHYS		0xfe001000
#define STI7105_SYSCONF_SIZE		0x1e0
#define STI7105_SYSCONF_STA(n)		(0x008 + 4 * (n))
#define STI7105_SYSCONF_CFG(n)		(0x100 + 4 * (n))

void __iomem *sti7105_sysconf_base_get(void);

/* Bits [msb:lsb] of SYS_CFGn. The result is shifted down to bit 0. */
u32 sti7105_sysconf_read(unsigned int reg, unsigned int lsb, unsigned int msb);
int sti7105_sysconf_write(unsigned int reg, unsigned int lsb, unsigned int msb,
			  u32 value);

#endif /* __STI7105_SYSCONF_H */
