// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 memory
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/types.h>

#include <asm/bootinfo.h>
#include <asm/io.h>

#include <asm/mach-ps2/iop.h>
#include <asm/mach-ps2/rom.h>

int valid_mmap_phys_addr_range(unsigned long pfn, size_t size)
{
	return 1;
}

int valid_phys_addr_range(phys_addr_t addr, size_t size)
{
	/* FIXME: What happens when /dev/mem writes to ROM? */
	return addr + size <= __pa(high_memory) ||
	       (IOP_RAM_BASE <= addr && addr + size <= IOP_RAM_BASE + IOP_RAM_SIZE) ||
	       (ROM0_BASE <= addr && addr + size <= ROM0_BASE + ROM0_SIZE) ||
	       (ROM1_BASE <= addr && addr + size <= ROM1_BASE + ROM1_SIZE);
}

void __init plat_mem_setup(void)
{
	ioport_resource.start = 0x10000000;
	ioport_resource.end   = 0x1fffffff;

	iomem_resource.start = 0x00000000;
	iomem_resource.end   = KSEG2 - 1;

	memblock_add(0x00000000, 0x02000000);
	memblock_reserve(ROM0_BASE, ROM0_SIZE);
	memblock_reserve(ROM1_BASE, ROM1_SIZE);

	set_io_port_base(CKSEG1);	/* KSEG1 is uncached */
}
