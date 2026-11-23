// SPDX-License-Identifier: GPL-2.0
/*
 * STi7105 SH-4 interrupt and timer setup.
 *
 * The on-chip INTC2 has far more sources than intc_enum can name: that
 * type is an unsigned char, and the full STi7105 table needs more than
 * 255 ids. This file therefore describes the SH-4 TMU, the ASC channels,
 * the GMAC and the two USB host groups, which is all the console, the
 * ethernet and USB storage need. Other sources are added with their
 * drivers.
 *
 * Register addresses are the STi7105 INTC2 map. Priorities and masks for
 * the on-chip blocks live in the sysconf page at 0xfe001000; the TMU
 * priorities stay in the SH-4 INTC at 0xffd00000.
 */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/sh_intc.h>
#include <linux/sh_timer.h>
#include <linux/sizes.h>
#include <linux/interrupt.h>
#include <asm/hw_irq.h>
#include <asm/platform_early.h>

#define STX7105_TMU_BASE	0xffd80000
#define STX7105_INTC2_BASE	0xfe001000
#define STX7105_INTC2_SIZE	0x400

#define STX7105_IPRA		0xffd00004
#define STX7105_IPRB		0xffd00008
#define STX7105_IPRC		0xffd0000c

#define STX7105_INTPRI00	(STX7105_INTC2_BASE + 0x300)
#define STX7105_INTPRI04	(STX7105_INTC2_BASE + 0x304)
#define STX7105_INTPRI08	(STX7105_INTC2_BASE + 0x308)

#define STX7105_INTMSK00	(STX7105_INTC2_BASE + 0x340)
#define STX7105_INTMSKCLR00	(STX7105_INTC2_BASE + 0x360)
#define STX7105_INTMSK04	(STX7105_INTC2_BASE + 0x344)
#define STX7105_INTMSKCLR04	(STX7105_INTC2_BASE + 0x364)
#define STX7105_INTMSK08	(STX7105_INTC2_BASE + 0x348)
#define STX7105_INTMSKCLR08	(STX7105_INTC2_BASE + 0x368)

enum {
	UNUSED = 0,

	/* SH-4 TMU. TMU2 has both the underflow and input-capture events. */
	TMU0, TMU1, TMU2,

	/*
	 * INTC2 group 3. Only UART2 is used, but the group priority and the
	 * mask word cover all four ASC channels, so the others must be named
	 * too. They are never given a vector.
	 */
	UART0, UART1, UART2, UART3,

	/*
	 * INTC2 group 8. FDMA0 and FDMA1 share the group and mask word
	 * with the USB port 1 controllers but have no vector here.
	 */
	FDMA0, FDMA1, OHCI1, EHCI1,

	/*
	 * INTC2 group 6. GMAC is the only one given a vector. The other
	 * three share its priority and its mask nibble, so they have to
	 * be named or the group cannot be described.
	 */
	TS_MERGER, GMAC, GMAC_PMT, EMPI_PCI,

	GROUP3, GROUP6, GROUP8,

	/*
	 * INTC2 group 15. TVO_DCS1 and BDISP_CQ share the group and mask
	 * word with the USB port 0 controllers but have no vector here.
	 */
	OHCI0, EHCI0, TVO_DCS1, BDISP_CQ,

	GROUP15,

	/*
	 * ILC3 outputs. These events have no bit in any INTMSK word: the
	 * ILC gates its own inputs. The names exist so the demux can chain
	 * off the Linux IRQs, and so intc_register_irq() has a mask slot.
	 * That slot is a word this file owns and never writes back.
	 */
	ILC0, ILC1, ILC2, ILC3, ILC4, ILC5, ILC6, ILC7,
	ILC8, ILC9, ILC10, ILC11, ILC12, ILC13, ILC14,
};

static struct intc_vect vectors[] __initdata = {
	INTC_VECT(TMU0, 0x400),
	INTC_VECT(TMU1, 0x420),
	INTC_VECT(TMU2, 0x440), INTC_VECT(TMU2, 0x460),

	INTC_VECT(UART2, 0x1120),

	INTC_VECT(GMAC, 0x12c0),

	INTC_VECT(OHCI1, 0x13c0), INTC_VECT(EHCI1, 0x13e0),
	INTC_VECT(OHCI0, 0x1700), INTC_VECT(EHCI0, 0x1720),

	/*
	 * Fifteen of the sixteen ILC3 outputs. The hardware has one more,
	 * at 0x3e0, and it is not wired. The entry path rejects an event
	 * below 0x200, so 0x200 is the lowest vector that reaches do_IRQ().
	 */
	INTC_VECT(ILC0, 0x200), INTC_VECT(ILC1, 0x220),
	INTC_VECT(ILC2, 0x240), INTC_VECT(ILC3, 0x260),
	INTC_VECT(ILC4, 0x280), INTC_VECT(ILC5, 0x2a0),
	INTC_VECT(ILC6, 0x2c0), INTC_VECT(ILC7, 0x2e0),
	INTC_VECT(ILC8, 0x300), INTC_VECT(ILC9, 0x320),
	INTC_VECT(ILC10, 0x340), INTC_VECT(ILC11, 0x360),
	INTC_VECT(ILC12, 0x380), INTC_VECT(ILC13, 0x3a0),
	INTC_VECT(ILC14, 0x3c0),
};

static struct intc_group groups[] __initdata = {
	INTC_GROUP(GROUP3, UART3, UART2, UART1, UART0),
	INTC_GROUP(GROUP6, EMPI_PCI, GMAC_PMT, GMAC, TS_MERGER),
	INTC_GROUP(GROUP8, FDMA0, FDMA1, OHCI1, EHCI1),
	INTC_GROUP(GROUP15, OHCI0, EHCI0, TVO_DCS1, BDISP_CQ),
};

static struct intc_prio_reg prio_registers[] __initdata = {
					/* 15-12, 11-8, 7-4, 3-0 */
	{ STX7105_IPRA, 0, 16, 4, { TMU0, TMU1, TMU2, 0 } },
	{ STX7105_IPRB, 0, 16, 4, { 0, 0, 0, 0 } },
	{ STX7105_IPRC, 0, 16, 4, { 0, 0, 0, 0 } },

			/* 31-28, 27-24, 23-20, 19-16, 15-12, 11-8, 7-4, 3-0 */
	{ STX7105_INTPRI00, 0, 32, 4, { 0 } },
	{ STX7105_INTPRI04, 0, 32, 4, { GROUP8, 0, GROUP6, 0, 0, GROUP3, 0, 0 } },
	{ STX7105_INTPRI08, 0, 32, 4, { 0, GROUP15, 0, 0, 0, 0, 0, 0 } },
};

/*
 * Shadow of the ILC output mask. Not __initdata: its address is taken
 * from the __initdata mask table, which a static initializer may not do
 * for an __initdata object. It has to be defined before that table,
 * since a static initializer cannot refer to a static further down.
 *
 * register_intc_controller() reads a mask register back while the
 * sources are still disabled, and refuses to come up if that read is
 * all ones. The ILC outputs have no hardware mask, so the word starts
 * clear and the core's read-modify-write of it is the whole mask.
 */
static u32 stx7105_ilc_mask = 0xffffffff;

static struct intc_mask_reg mask_registers[] __initdata = {
	{ STX7105_INTMSK00, STX7105_INTMSKCLR00, 32,
	  { 0 } },
	{ STX7105_INTMSK04, STX7105_INTMSKCLR04, 32,
	  /* 31..28: EHCI1, OHCI1, FDMA1, FDMA0 */
	  { EHCI1, OHCI1, FDMA1, FDMA0,
	  /* 27..24 */
	    0, 0, 0, 0,
	  /* 23..20: TS_MERGER, GMAC, GMAC_PMT, EMPI_PCI */
	    TS_MERGER, GMAC, GMAC_PMT, EMPI_PCI,
	  /* 19..16 */
	    0, 0, 0, 0,
	  /* 15..12 */
	    0, 0, 0, 0,
	  /* 11..8: UART0, UART1, UART2, UART3 */
	    UART0, UART1, UART2, UART3,
	  /* 7..0 */
	    0, 0, 0, 0, 0, 0, 0, 0 } },
	{ STX7105_INTMSK08, STX7105_INTMSKCLR08, 32,
	  /* 31..28 */
	  { 0, 0, 0, 0,
	  /* 27..24: BDISP_CQ, TVO_DCS1, EHCI0, OHCI0 */
	    BDISP_CQ, TVO_DCS1, EHCI0, OHCI0,
	  /* 23..0 */
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },

	/*
	 * ILC outputs have no INTMSK bit. intc_register_irq() refuses a
	 * source with neither a mask nor a priority slot, so each output
	 * is given a bit in this word. Only the set address is filled in,
	 * which makes the core read-modify-write it; the word is private
	 * and is never stored to a register. Bits run from the top, so
	 * ILC0 is bit 31.
	 */
	{ (unsigned long)&stx7105_ilc_mask, 0, 32,
	  { ILC0, ILC1, ILC2, ILC3, ILC4, ILC5, ILC6, ILC7,
	    ILC8, ILC9, ILC10, ILC11, ILC12, ILC13, ILC14, 0,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
};

static DECLARE_INTC_DESC(intc_desc, "stx7105", vectors, groups,
			 mask_registers, prio_registers, NULL);

/*
 * INTC2 registers are in the P4 area and are not covered by the PMB RAM
 * mapping, so the controller has to be handed an ioremap window. The
 * driver matches register addresses against that window.
 */
static struct resource intc2_resources[] __initdata = {
	DEFINE_RES_MEM(STX7105_INTC2_BASE, STX7105_INTC2_SIZE),
};

void __init plat_irq_setup(void)
{
	intc_desc.resource = intc2_resources;
	intc_desc.num_resources = ARRAY_SIZE(intc2_resources);

	register_intc_controller(&intc_desc);
}

static struct sh_timer_config tmu0_platform_data = {
	.channels_mask = 7,
};

static struct resource tmu0_resources[] = {
	DEFINE_RES_MEM(STX7105_TMU_BASE, 0x30),
	DEFINE_RES_IRQ(evt2irq(0x400)),
	DEFINE_RES_IRQ(evt2irq(0x420)),
	DEFINE_RES_IRQ(evt2irq(0x440)),
};

static struct platform_device tmu0_device = {
	.name		= "sh-tmu",
	.id		= 0,
	.dev = {
		.platform_data = &tmu0_platform_data,
	},
	.resource	= tmu0_resources,
	.num_resources	= ARRAY_SIZE(tmu0_resources),
};

static struct platform_device *st40_early_devices[] __initdata = {
	&tmu0_device,
};

void __init plat_early_device_setup(void)
{
	sh_early_platform_add_devices(st40_early_devices,
				      ARRAY_SIZE(st40_early_devices));
}
