// SPDX-License-Identifier: GPL-2.0
/*
 * ST PDK7105 (STi7105, 网乐通) board setup.
 *
 * U-Boot has already programmed the PMB so that cached RAM at 0x80800000
 * reaches physical 0x40800000, and has left ASC2 running at 115200 8n1.
 * This file keeps that console: ASC2 is ttyAS0, its baud clock is the
 * fixed 100 MHz comms clock, and the TMU keeps running from the fixed
 * 66 MHz clock. Clockgen A replaces both in a later phase.
 */
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/sh_intc.h>
#include <asm/machvec.h>

#define PDK7105_ASC2_BASE	0xfd032000
#define PDK7105_ASC2_EVT	0x1120

/* Clockgen A outputs used before that clockgen has a driver. */
#define PDK7105_TMU_HZ		66666666
#define PDK7105_COMMS_HZ	100000000

static struct resource asc2_resources[] = {
	DEFINE_RES_MEM(PDK7105_ASC2_BASE, 0x2c),
	DEFINE_RES_IRQ(0),
};

static struct platform_device asc2_device = {
	.name		= "st-asc",
	.id		= 0,
	.resource	= asc2_resources,
	.num_resources	= ARRAY_SIZE(asc2_resources),
};

/*
 * sh_tmu looks the clock up by the device name it was registered with,
 * which is "sh-tmu.0" for id 0, and asks for "fck". The driver then
 * divides that rate by four, matching the SH-4 TMU.
 */
/*
 * sh_tmu looks the clock up as "fck" on "sh-tmu.0" and divides by
 * four. st-asc takes the unnamed clock of "st-asc.0". Both are fixed
 * until Clockgen A is described.
 *
 * clk_init() calls this before late_time_init probes the TMU, so the
 * clock has to come from the machine vector rather than an initcall.
 */
static int __init pdk7105_clk_init(void)
{
	struct clk_hw *tmu, *comms;

	tmu = clk_hw_register_fixed_rate(NULL, "tmu_fck", NULL, 0,
					 PDK7105_TMU_HZ);
	if (!IS_ERR(tmu))
		clk_hw_register_clkdev(tmu, "fck", "sh-tmu.0");

	comms = clk_hw_register_fixed_rate(NULL, "comms_clk", NULL, 0,
					   PDK7105_COMMS_HZ);
	if (!IS_ERR(comms))
		clk_hw_register_clkdev(comms, NULL, "st-asc.0");

	return 0;
}

static int __init pdk7105_devices_setup(void)
{
	asc2_resources[1].start = evt2irq(PDK7105_ASC2_EVT);
	asc2_resources[1].end = asc2_resources[1].start;

	return platform_device_register(&asc2_device);
}
arch_initcall(pdk7105_devices_setup);

static struct sh_machine_vector mv_pdk7105 __initmv = {
	.mv_name	= "PDK7105",
	.mv_clk_init	= pdk7105_clk_init,
};
