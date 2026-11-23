/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_PDK7105_H
#define __ASM_SH_PDK7105_H

/*
 * STi7105 (PDK7105) addresses shared by the board file and the
 * sysconf, PIO and ILC3 drivers. The sysconf page also holds INTC2;
 * this window stops at 0x1e0 so the two do not request the same region.
 */
#define PDK7105_SYSCONF_PHYS	0xfe001000
#define PDK7105_SYSCONF_SIZE	0x1e0

#define PDK7105_SYS_STA(n)	(0x008 + 4 * (n))
#define PDK7105_SYS_CFG(n)	(0x100 + 4 * (n))

/*
 * PIO0-6 sit in the comms block, PIO7-16 in the standalone block.
 * The 7105 programming note and the old driver agree on these bases;
 * bank 14 has no alternate-function sysconf register.
 */
#define PDK7105_PIO_BANKS	17
#define PDK7105_PIO_BASE(n)	\
	((n) < 7 ? 0xfd020000 + (n) * 0x1000 :		\
		   0xfe010000 + ((n) - 7) * 0x1000)

/* Direction is one bit per pin in PC0, PC1 and PC2. */
#define PDK7105_PIO_IN		4	/* high-Z input */
#define PDK7105_PIO_ALT_OUT	6	/* alternate-function push-pull */

/*
 * ILC3. The priority and trigger registers of inputs past 31 sit above
 * the old 0x900 window, so the mapping is a full page. LPC is at
 * 0xfd008000 and is not covered. Fifteen outputs are wired to INTC2
 * events 0x200..0x3c0; the hardware has a sixteenth that is not.
 */
#define PDK7105_ILC_PHYS	0xfd000000
#define PDK7105_ILC_SIZE	0x1000
#define PDK7105_ILC_INPUTS	106
#define PDK7105_ILC_OUTPUTS	15
#define PDK7105_ILC_FIRST_IRQ	176

/*
 * PCI host memory, one 16 MB PMB. The old EMISS window was 512 MB at
 * physical 0xc0000000, but pmb_addr_valid() rejects any mapping whose
 * last byte is at or above P3SEG, and 512 MB does not fit in what is
 * left of P2 once RAM and its uncached alias are in place. U-Boot
 * itself maps one 16 MB page of this region at virtual 0xa7000000
 * (physical 0x07000000, the PCI hole below the EMISS window). The
 * entry is bolted after pmb_synchronize(), which drops any bootloader
 * entry whose physical page is outside RAM.
 */
#define PDK7105_PCI_MEM_PHYS	0x07000000
#define PDK7105_PCI_MEM_SIZE	0x01000000
#define PDK7105_PCI_MEM_VIRT	0xa7000000

#endif /* __ASM_SH_PDK7105_H */
