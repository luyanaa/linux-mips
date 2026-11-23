// SPDX-License-Identifier: GPL-2.0
/*
 * STi7105 Clockgen A, observed rather than programmed.
 *
 * U-Boot has already locked both PLLs and written the output mux.
 * This driver reads those registers back and publishes the rates.
 * It never writes the mux at 0x014/0x024 (a field of 3 stops the
 * clock) and never writes a PLL multiplier. The console clock
 * CLKA_IC_IF_100, the SH-4 clock and the EMI clock are marked
 * critical so the unused-clock pass cannot gate them.
 *
 * The TMU is not one of these outputs. The board keeps the fixed
 * 66666666 Hz clock for it; CLKA_PCI happens to run at that rate
 * but nothing wires the timer to it.
 */
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/io.h>
#include <linux/slab.h>

#include <linux/clk/stx7105.h>

#define CKGA_PHYS		0xfe213000
#define CKGA_SIZE		0xc00

#define CKGA_PLL0_CFG		0x000
#define CKGA_PLL1_CFG		0x004
#define CKGA_POWER_CFG		0x010
#define CKGA_CLKOPSRC		0x014
#define CKGA_CLKOPSRC2		0x024

/* Divider for output idx of parent 0=OSC, 1=PLL0HS, 2=PLL0LS, 3=PLL1. */
#define CKGA_DIV(parent, idx)	(0x800 + (parent) * 0x100 + 4 * (idx))

#define CKGA_OSC_HZ		30000000

#define CKGA_MUX_OSC		0
#define CKGA_MUX_PLL0		1
#define CKGA_MUX_PLL1		2
#define CKGA_MUX_STOP		3

/* Output index of the clocks this phase publishes. */
#define CKGA_IDX_ST40		4
#define CKGA_IDX_IC_IF_100	5
#define CKGA_IDX_PCI		14
#define CKGA_IDX_EMI		15

struct stx7105_pll {
	struct clk_hw hw;
	struct stx7105_clkgena *cga;
};

struct stx7105_clkgena {
	void __iomem *base;
	struct clk_hw *osc;
	struct clk_hw *pll0hs;
	struct clk_hw *pll0ls;
	struct clk_hw *pll1;
};

static u32 ckga_read(struct stx7105_clkgena *cga, u32 off)
{
	return readl(cga->base + off);
}

static struct stx7105_pll *to_stx7105_pll(struct clk_hw *hw)
{
	return container_of(hw, struct stx7105_pll, hw);
}

/* PLL1600: HS = 2 * ref * NDIV / MDIV. MDIV 0 is rejected. */
static unsigned long stx7105_pll0_recalc(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct stx7105_clkgena *cga = to_stx7105_pll(hw)->cga;
	u32 cfg = ckga_read(cga, CKGA_PLL0_CFG);
	u32 mdiv = cfg & 0x7;
	u32 ndiv = (cfg >> 8) & 0xff;

	if (!mdiv || !ndiv)
		return 0;

	return DIV_ROUND_CLOSEST_ULL((u64)2 * parent_rate * ndiv, mdiv);
}

static const struct clk_ops stx7105_pll0_ops = {
	.recalc_rate = stx7105_pll0_recalc,
};

/* PLL800: ((2 * ref * NDIV) / MDIV) >> PDIV. MDIV 0 means 1. */
static unsigned long stx7105_pll1_recalc(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct stx7105_clkgena *cga = to_stx7105_pll(hw)->cga;
	u32 cfg = ckga_read(cga, CKGA_PLL1_CFG);
	u32 mdiv = cfg & 0xff;
	u32 ndiv = (cfg >> 8) & 0xff;
	u32 pdiv = (cfg >> 16) & 0x7;

	if (!ndiv)
		return 0;
	if (!mdiv)
		mdiv = 1;

	return (DIV_ROUND_CLOSEST_ULL((u64)2 * parent_rate * ndiv, mdiv)) >> pdiv;
}

static const struct clk_ops stx7105_pll1_ops = {
	.recalc_rate = stx7105_pll1_recalc,
};

/*
 * One published output. The mux field selects OSC, PLL0 or PLL1, and
 * the divider for that parent is (register & 0x1f) + 1. A mux field of
 * 3 means the output is stopped.
 */
struct stx7105_out {
	struct clk_hw hw;
	struct stx7105_clkgena *cga;
	u8 idx;
};

static struct stx7105_out *to_stx7105_out(struct clk_hw *hw)
{
	return container_of(hw, struct stx7105_out, hw);
}

static u32 stx7105_mux_field(struct stx7105_clkgena *cga, u8 idx)
{
	u32 reg, shift;

	if (idx <= 15) {
		reg = ckga_read(cga, CKGA_CLKOPSRC);
		shift = idx * 2;
	} else {
		reg = ckga_read(cga, CKGA_CLKOPSRC2);
		shift = (idx - 16) * 2;
	}
	return (reg >> shift) & 3;
}

static u8 stx7105_parent_index(struct stx7105_clkgena *cga, u8 idx)
{
	u32 field = stx7105_mux_field(cga, idx);

	if (field == CKGA_MUX_STOP)
		return 0;
	if (field == CKGA_MUX_OSC)
		return 0;
	if (field == CKGA_MUX_PLL0)
		return idx <= 3 ? 1 : 2;
	return 3;
}

static unsigned long stx7105_out_recalc(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct stx7105_out *out = to_stx7105_out(hw);
	u8 parent = stx7105_parent_index(out->cga, out->idx);
	u32 div;

	if (stx7105_mux_field(out->cga, out->idx) == CKGA_MUX_STOP)
		return 0;

	div = ckga_read(out->cga, CKGA_DIV(parent, out->idx)) & 0x1f;
	return parent_rate / (div + 1);
}

static u8 stx7105_out_get_parent(struct clk_hw *hw)
{
	struct stx7105_out *out = to_stx7105_out(hw);

	return stx7105_parent_index(out->cga, out->idx);
}

static const struct clk_ops stx7105_out_ops = {
	.recalc_rate = stx7105_out_recalc,
	.get_parent = stx7105_out_get_parent,
};

static struct clk_hw *stx7105_register_pll(struct stx7105_clkgena *cga,
					   const char *name,
					   const struct clk_ops *ops,
					   const char *parent,
					   unsigned long flags)
{
	struct stx7105_pll *pll;
	struct clk_init_data init = {
		.name = name,
		.ops = ops,
		.parent_names = &parent,
		.num_parents = 1,
		.flags = flags,
	};
	int ret;

	pll = kzalloc_obj(*pll);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	pll->cga = cga;
	pll->hw.init = &init;
	ret = clk_hw_register(NULL, &pll->hw);
	if (ret) {
		kfree(pll);
		return ERR_PTR(ret);
	}
	return &pll->hw;
}

static struct clk_hw *stx7105_register_out(struct stx7105_clkgena *cga,
					   const char *name, u8 idx,
					   unsigned long flags)
{
	static const char * const parents[] = {
		"ckga_osc", "ckga_pll0hs", "ckga_pll0ls", "ckga_pll1",
	};
	struct stx7105_out *out;
	struct clk_init_data init = {
		.name = name,
		.ops = &stx7105_out_ops,
		.parent_names = parents,
		.num_parents = ARRAY_SIZE(parents),
		.flags = flags,
	};
	int ret;

	out = kzalloc_obj(*out);
	if (!out)
		return ERR_PTR(-ENOMEM);

	out->cga = cga;
	out->idx = idx;
	out->hw.init = &init;
	ret = clk_hw_register(NULL, &out->hw);
	if (ret) {
		kfree(out);
		return ERR_PTR(ret);
	}
	return &out->hw;
}

/*
 * Register Clockgen A and the two lookups the console needs. Called
 * from the machine vector, before the TMU is probed. Returns 0 even
 * when the block cannot be mapped: the board then keeps its fixed
 * comms clock, so the console still has a baud clock.
 */
int __init stx7105_clkgena_init(void)
{
	struct stx7105_clkgena *cga;
	struct clk_hw *comms, *emi, *cpu, *pci;

	cga = kzalloc_obj(*cga);
	if (!cga)
		return -ENOMEM;

	cga->base = ioremap(CKGA_PHYS, CKGA_SIZE);
	if (!cga->base) {
		kfree(cga);
		pr_warn("stx7105: Clockgen A unavailable, comms clock stays fixed\n");
		return -ENOMEM;
	}

	cga->osc = clk_hw_register_fixed_rate(NULL, "ckga_osc", NULL, 0,
						  CKGA_OSC_HZ);
	if (IS_ERR(cga->osc))
		return PTR_ERR(cga->osc);

	cga->pll0hs = stx7105_register_pll(cga, "ckga_pll0hs", &stx7105_pll0_ops,
					   "ckga_osc", CLK_IS_CRITICAL);
	if (IS_ERR(cga->pll0hs))
		return PTR_ERR(cga->pll0hs);

	cga->pll0ls = clk_hw_register_fixed_factor(NULL, "ckga_pll0ls",
							"ckga_pll0hs",
							CLK_IS_CRITICAL, 1, 2);
	if (IS_ERR(cga->pll0ls))
		return PTR_ERR(cga->pll0ls);

	cga->pll1 = stx7105_register_pll(cga, "ckga_pll1", &stx7105_pll1_ops,
					 "ckga_osc", CLK_IS_CRITICAL);
	if (IS_ERR(cga->pll1))
		return PTR_ERR(cga->pll1);

	/*
	 * These four stay running for the life of the boot. The unused
	 * ones (PCI, and anything not looked up) are critical too where
	 * gating them would stop the interconnect, the CPU or RAM.
	 */
	cpu = stx7105_register_out(cga, "clka_st40_ick", CKGA_IDX_ST40,
				   CLK_IS_CRITICAL | CLK_GET_RATE_NOCACHE);
	comms = stx7105_register_out(cga, "clka_ic_if_100", CKGA_IDX_IC_IF_100,
				     CLK_IS_CRITICAL | CLK_GET_RATE_NOCACHE);
	pci = stx7105_register_out(cga, "clka_pci", CKGA_IDX_PCI,
				   CLK_IS_CRITICAL | CLK_GET_RATE_NOCACHE);
	emi = stx7105_register_out(cga, "clka_emi_master", CKGA_IDX_EMI,
				   CLK_IS_CRITICAL | CLK_GET_RATE_NOCACHE);
	if (IS_ERR(cpu) || IS_ERR(comms) || IS_ERR(pci) || IS_ERR(emi))
		return -EINVAL;

	/* st-asc takes the unnamed clock of st-asc.0. st-rc takes the
	 * unnamed clock of st-rc.0, the same 100 MHz comms clock.
	 */
	clk_hw_register_clkdev(comms, NULL, "st-asc.0");
	clk_hw_register_clkdev(comms, NULL, "st-rc.0");
	clk_hw_register_clkdev(comms, "comms_clk", NULL);

	pr_info("stx7105: Clockgen A osc %lu pll0 %lu pll1 %lu comms %lu\n",
		clk_hw_get_rate(cga->osc), clk_hw_get_rate(cga->pll0hs),
		clk_hw_get_rate(cga->pll1), clk_hw_get_rate(comms));

	return 0;
}
