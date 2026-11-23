// SPDX-License-Identifier: GPL-2.0
/*
 * STi7105 system configuration registers.
 *
 * The block at 0xfe001000 holds SYS_DEV, SYS_STA and SYS_CFG. INTC2
 * lives in the same page, from 0x300, so this driver only maps the
 * first 0x1e0 bytes. A field is one register: group base plus 4 times
 * the register number, then bits [msb:lsb].
 */
#include <linux/device.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <linux/soc/sti/sti7105-sysconf.h>

static void __iomem *sti7105_sysconf_base;
static DEFINE_SPINLOCK(sti7105_sysconf_lock);

void __iomem *sti7105_sysconf_base_get(void)
{
	return sti7105_sysconf_base;
}
EXPORT_SYMBOL_GPL(sti7105_sysconf_base_get);

static int sti7105_sysconf_check(unsigned int reg, unsigned int lsb,
				 unsigned int msb)
{
	if (!sti7105_sysconf_base)
		return -ENODEV;
	if (reg > 55 || lsb > 31 || msb > 31 || msb < lsb)
		return -EINVAL;
	return 0;
}

/*
 * Read bits [msb:lsb] of SYS_CFGn. The value is shifted down so bit
 * lsb of the register is bit 0 of the result.
 */
u32 sti7105_sysconf_read(unsigned int reg, unsigned int lsb, unsigned int msb)
{
	u32 val;

	if (sti7105_sysconf_check(reg, lsb, msb))
		return 0;

	val = readl(sti7105_sysconf_base + STI7105_SYSCONF_CFG(reg));
	val >>= lsb;
	if (msb - lsb < 31)
		val &= (1U << (msb - lsb + 1)) - 1;
	return val;
}
EXPORT_SYMBOL_GPL(sti7105_sysconf_read);

/*
 * Replace bits [msb:lsb] of SYS_CFGn. The rest of the register is
 * left as it was. SYS_STA is read-only and is not written here.
 */
int sti7105_sysconf_write(unsigned int reg, unsigned int lsb, unsigned int msb,
			  u32 value)
{
	unsigned long flags;
	u32 mask, val;
	int ret;

	ret = sti7105_sysconf_check(reg, lsb, msb);
	if (ret)
		return ret;

	if (msb - lsb < 31)
		mask = ((1U << (msb - lsb + 1)) - 1) << lsb;
	else
		mask = 0xffffffff;

	spin_lock_irqsave(&sti7105_sysconf_lock, flags);
	val = readl(sti7105_sysconf_base + STI7105_SYSCONF_CFG(reg));
	val &= ~mask;
	val |= (value << lsb) & mask;
	writel(val, sti7105_sysconf_base + STI7105_SYSCONF_CFG(reg));
	spin_unlock_irqrestore(&sti7105_sysconf_lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(sti7105_sysconf_write);

static int sti7105_sysconf_probe(struct platform_device *pdev)
{
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	/*
	 * INTC2 has already ioremapped the whole page. request_mem_region
	 * is not used: the two drivers share the page and only this
	 * window is written from here.
	 */
	sti7105_sysconf_base = devm_ioremap(&pdev->dev, res->start,
					    resource_size(res));
	if (!sti7105_sysconf_base)
		return -ENOMEM;

	return 0;
}

static struct platform_driver sti7105_sysconf_driver = {
	.probe	= sti7105_sysconf_probe,
	.driver	= {
		.name = "sti7105-sysconf",
	},
};

static int __init sti7105_sysconf_init(void)
{
	return platform_driver_register(&sti7105_sysconf_driver);
}
arch_initcall(sti7105_sysconf_init);
