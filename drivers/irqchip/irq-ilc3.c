// SPDX-License-Identifier: GPL-2.0
/*
 * STi7105 interrupt level controller (ILC3).
 *
 * 106 inputs, of which 15 are wired to INTC2 events 0x200, 0x220, ...
 * 0x3c0. Those events are Linux IRQs 16 to 30. The hardware priority
 * register selects which output an input raises: priority P arrives on
 * IRQ (14 - P). Every input defaults to priority 7, so it arrives on
 * IRQ 7's neighbour, IRQ 23 (event 0x2e0), until a driver changes it.
 *
 * USB, ethernet and SATA on this SoC are INTC2 sources and do not come
 * through here. The inputs this board actually uses are the PHY MDINT,
 * the LPC and the STEM connector.
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/platform_device.h>
#include <linux/sh_intc.h>
#include <linux/slab.h>
#include <mach/pdk7105.h>

#define ILC_STATUS(n)		(0x200 + 4 * ((n) / 32))
#define ILC_CLR_STATUS(n)	(0x280 + 4 * ((n) / 32))
#define ILC_ENABLE(n)		(0x400 + 4 * ((n) / 32))
#define ILC_CLR_ENABLE(n)	(0x480 + 4 * ((n) / 32))
#define ILC_SET_ENABLE(n)	(0x500 + 4 * ((n) / 32))
#define ILC_PRIORITY(n)		(0x800 + 8 * (n))
#define ILC_TRIGMODE(n)		(0x804 + 8 * (n))

#define ILC_BANKS		DIV_ROUND_UP(PDK7105_ILC_INPUTS, 32)

/* Priority written for an input that has not asked for one. */
#define ILC_DEFAULT_PRI		7

struct ilc3 {
	void __iomem *base;
	struct irq_domain *domain;
	u8 priority[PDK7105_ILC_INPUTS];
	/* Which inputs are routed to each output, one bit per input. */
	u32 routed[PDK7105_ILC_OUTPUTS][ILC_BANKS];
	raw_spinlock_t lock;
};

static struct ilc3 *ilc3_data;

static void ilc3_write_priority(struct ilc3 *ilc, unsigned int input)
{
	u8 pri = ilc->priority[input];

	if (pri >= PDK7105_ILC_OUTPUTS)
		pri = ILC_DEFAULT_PRI;
	writel(pri, ilc->base + ILC_PRIORITY(input));
}

static void ilc3_mask(struct irq_data *data)
{
	struct ilc3 *ilc = irq_data_get_irq_chip_data(data);
	unsigned int input = irqd_to_hwirq(data);

	writel(BIT(input % 32), ilc->base + ILC_CLR_ENABLE(input));
}

static void ilc3_unmask(struct irq_data *data)
{
	struct ilc3 *ilc = irq_data_get_irq_chip_data(data);
	unsigned int input = irqd_to_hwirq(data);
	unsigned long flags;

	raw_spin_lock_irqsave(&ilc->lock, flags);
	ilc3_write_priority(ilc, input);
	raw_spin_unlock_irqrestore(&ilc->lock, flags);

	writel(BIT(input % 32), ilc->base + ILC_SET_ENABLE(input));
}

static int ilc3_set_type(struct irq_data *data, unsigned int flow)
{
	struct ilc3 *ilc = irq_data_get_irq_chip_data(data);
	unsigned int input = irqd_to_hwirq(data);
	u32 mode;

	switch (flow & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_LEVEL_HIGH:
		mode = 1;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		mode = 2;
		break;
	case IRQ_TYPE_EDGE_RISING:
		mode = 3;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		mode = 4;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		mode = 5;
		break;
	default:
		return -EINVAL;
	}

	writel(mode, ilc->base + ILC_TRIGMODE(input));
	return 0;
}

static struct irq_chip ilc3_chip = {
	.name		= "ILC3",
	.irq_mask	= ilc3_mask,
	.irq_unmask	= ilc3_unmask,
	.irq_set_type	= ilc3_set_type,
};

static int ilc3_map(struct irq_domain *domain, unsigned int virq,
		    irq_hw_number_t hwirq)
{
	struct ilc3 *ilc = domain->host_data;

	irq_set_chip_data(virq, ilc);
	irq_set_chip_and_handler(virq, &ilc3_chip, handle_level_irq);
	return 0;
}

static const struct irq_domain_ops ilc3_domain_ops = {
	.map	= ilc3_map,
	.xlate	= irq_domain_xlate_onecell,
};

/*
 * An output fires. Scan the inputs routed to it and dispatch the one
 * whose status and enable bits are both set. The status bit is then
 * cleared; a level source sets it again if it is still asserted.
 */
static void ilc3_demux(struct irq_desc *desc)
{
	struct ilc3 *ilc = irq_desc_get_handler_data(desc);
	unsigned int parent = irq_desc_get_irq(desc);
	unsigned int output = parent - evt2irq(0x200);
	int bank;

	if (output >= PDK7105_ILC_OUTPUTS) {
		handle_bad_irq(desc);
		return;
	}

	for (bank = 0; bank < ILC_BANKS; bank++) {
		u32 status, pending;
		unsigned int input;

		status = readl(ilc->base + ILC_STATUS(bank * 32)) &
			 readl(ilc->base + ILC_ENABLE(bank * 32));
		pending = status & ilc->routed[output][bank];
		if (!pending)
			continue;

		input = bank * 32 + __ffs(pending);
		writel(BIT(input % 32), ilc->base + ILC_CLR_STATUS(input));
		generic_handle_domain_irq(ilc->domain, input);
		return;
	}

	pr_debug("ilc3: spurious irq %u\n", parent);
}

static int ilc3_probe(struct platform_device *pdev)
{
	struct ilc3 *ilc;
	struct resource *res;
	unsigned int i, virq;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	ilc = devm_kzalloc(&pdev->dev, sizeof(*ilc), GFP_KERNEL);
	if (!ilc)
		return -ENOMEM;
	raw_spin_lock_init(&ilc->lock);

	ilc->base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!ilc->base)
		return -ENOMEM;

	for (i = 0; i < PDK7105_ILC_INPUTS; i++) {
		ilc->priority[i] = ILC_DEFAULT_PRI;
		ilc->routed[14 - ILC_DEFAULT_PRI][i / 32] |= BIT(i % 32);
	}

	/*
	 * No device tree on this board, so the domain gets a named
	 * firmware node rather than one from the platform device. The
	 * inputs are mapped onto Linux IRQs from FIRST_IRQ now, which is
	 * what the old table called them.
	 */
	ilc->domain = irq_domain_create_simple
		(irq_domain_alloc_named_fwnode("sti7105-ilc3"),
		 PDK7105_ILC_INPUTS, PDK7105_ILC_FIRST_IRQ,
		 &ilc3_domain_ops, ilc);
	if (!ilc->domain)
		return -ENOMEM;

	/*
	 * The fifteen INTC2 events are already Linux IRQs, registered by
	 * setup-st40.c. Chain a demux off each of them. Nothing else on
	 * this SoC may request these IRQs.
	 */
	for (i = 0; i < PDK7105_ILC_OUTPUTS; i++) {
		virq = evt2irq(0x200 + i * 0x20);
		irq_set_chained_handler_and_data(virq, ilc3_demux, ilc);
	}

	ilc3_data = ilc;
	return 0;
}

static struct platform_driver ilc3_driver = {
	.probe	= ilc3_probe,
	.driver	= {
		.name = "sti7105-ilc3",
	},
};

static int __init ilc3_init(void)
{
	return platform_driver_register(&ilc3_driver);
}
arch_initcall(ilc3_init);
