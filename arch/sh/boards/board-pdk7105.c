// SPDX-License-Identifier: GPL-2.0
/*
 * ST PDK7105 (STi7105, 网乐通) board setup.
 *
 * U-Boot has already programmed the PMB so that cached RAM at 0x80800000
 * reaches physical 0x40800000, and has left ASC2 running at 115200 8n1.
 * This file keeps that console: ASC2 is ttyAS0. Clockgen A is read
 * back and supplies the 100 MHz comms clock; if it cannot be mapped,
 * the fixed 100 MHz clock stays. The TMU keeps the fixed 66 MHz
 * clock, which is not a Clockgen A output.
 *
 * The two USB host ports are powered up and registered here too. Port 0
 * is the onboard port and comes first, so a stick in it is enumerated
 * before the front port exists and becomes sda.
 */
#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk/stx7105.h>
#include <linux/errno.h>
#include <linux/completion.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/hex.h>
#include <linux/if.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/phy.h>
#include <linux/pgtable.h>
#include <linux/kstrtox.h>
#include <linux/netdevice.h>
#include <linux/net_namespace.h>
#include <linux/notifier.h>
#include <linux/pinctrl/pinctrl-sti7105.h>
#include <linux/platform_device.h>
#include <linux/soc/sti/sti7105-sysconf.h>
#include <mach/pdk7105.h>
#include <asm/mmu.h>
#include <linux/rtnetlink.h>
#include <linux/sh_intc.h>
#include <linux/stmmac.h>
#include <linux/string.h>
#include <linux/usb.h>
#include <linux/usb/ehci_pdriver.h>
#include <linux/usb/hcd.h>
#include <linux/usb/ohci_pdriver.h>
#include <asm/machvec.h>

#define PDK7105_ASC2_BASE	0xfd032000
#define PDK7105_ASC2_EVT	0x1120

/*
 * The TMU is not a Clockgen A output. CLKA_PCI runs at this rate, but
 * the old kernel and U-Boot both timed the UART from 100 MHz and the
 * timer from this fixed value, so it stays until a measurement says
 * otherwise.
 */
#define PDK7105_TMU_HZ		66666666
#define PDK7105_COMMS_HZ	100000000

/* USB power and overcurrent fields, in the sysconf page. */
#define PDK7105_SYS_STA15	STI7105_SYSCONF_STA(15)
#define PDK7105_SYS_CFG04	STI7105_SYSCONF_CFG(4)
#define PDK7105_SYS_CFG32	STI7105_SYSCONF_CFG(32)

/* Both USB ports use PIO4, alternate function 4. */
#define PDK7105_USB_PIO_BANK	4
#define PDK7105_USB_PIO_FN	4

/* USB host wrapper, protocol and controller windows. */
struct pdk7105_usb_port {
	unsigned long wrapper;
	unsigned long protocol;
	unsigned long ehci;
	unsigned long ohci;
	unsigned int evt_ehci;
	unsigned int evt_ohci;
	unsigned int pio_oc;
	unsigned int pio_pwr;
};

static const struct pdk7105_usb_port pdk7105_usb_ports[] = {
	{
		.wrapper	= 0xfe100000,
		.protocol	= 0xfe1fff00,
		.ehci		= 0xfe1ffe00,
		.ohci		= 0xfe1ffc00,
		.evt_ehci	= 0x1720,
		.evt_ohci	= 0x1700,
		.pio_oc		= 4,
		.pio_pwr	= 5,
	}, {
		.wrapper	= 0xfea00000,
		.protocol	= 0xfeafff00,
		.ehci		= 0xfeaffe00,
		.ohci		= 0xfeaffc00,
		.evt_ehci	= 0x13e0,
		.evt_ohci	= 0x13c0,
		.pio_oc		= 6,
		.pio_pwr	= 7,
	},
};

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

static u64 pdk7105_usb_dma_mask = DMA_BIT_MASK(32);

static struct resource pdk7105_ehci0_resources[] = {
	DEFINE_RES_MEM(0xfe1ffe00, 0x100),
	DEFINE_RES_IRQ(0),
};

static struct resource pdk7105_ohci0_resources[] = {
	DEFINE_RES_MEM(0xfe1ffc00, 0x100),
	DEFINE_RES_IRQ(0),
};

static struct resource pdk7105_ehci1_resources[] = {
	DEFINE_RES_MEM(0xfeaffe00, 0x100),
	DEFINE_RES_IRQ(0),
};

static struct resource pdk7105_ohci1_resources[] = {
	DEFINE_RES_MEM(0xfeaffc00, 0x100),
	DEFINE_RES_IRQ(0),
};

/*
 * The wrapper's INSNREG01 holds the AHB/STBus burst threshold. It sits
 * 0x94 bytes into the EHCI region: the capability registers are 0x10
 * bytes long and the register is 0x84 into the operational registers,
 * which is brcm_insnreg[1] in struct ehci_regs.
 */
static int pdk7105_ehci_pre_setup(struct usb_hcd *hcd)
{
	writel(0x00800080, hcd->regs + 0x94);

	return 0;
}

static struct usb_ehci_pdata pdk7105_ehci_pdata = {
	.caps_offset	= 0,
	.pre_setup	= pdk7105_ehci_pre_setup,
};

static struct usb_ohci_pdata pdk7105_ohci_pdata = {
	.num_ports	= 1,
};

#define PDK7105_EHCI_DEV(_id, _res)					\
	{								\
		.name		= "ehci-platform",			\
		.id		= _id,					\
		.resource	= _res,					\
		.num_resources	= ARRAY_SIZE(_res),			\
		.dev = {						\
			.platform_data		= &pdk7105_ehci_pdata,	\
			.dma_mask		= &pdk7105_usb_dma_mask, \
			.coherent_dma_mask	= DMA_BIT_MASK(32),	\
		},							\
	}

#define PDK7105_OHCI_DEV(_id, _res)					\
	{								\
		.name		= "ohci-platform",			\
		.id		= _id,					\
		.resource	= _res,					\
		.num_resources	= ARRAY_SIZE(_res),			\
		.dev = {						\
			.platform_data		= &pdk7105_ohci_pdata,	\
			.dma_mask		= &pdk7105_usb_dma_mask, \
			.coherent_dma_mask	= DMA_BIT_MASK(32),	\
		},							\
	}

static struct platform_device pdk7105_ehci0_device =
	PDK7105_EHCI_DEV(0, pdk7105_ehci0_resources);
static struct platform_device pdk7105_ohci0_device =
	PDK7105_OHCI_DEV(0, pdk7105_ohci0_resources);
static struct platform_device pdk7105_ehci1_device =
	PDK7105_EHCI_DEV(1, pdk7105_ehci1_resources);
static struct platform_device pdk7105_ohci1_device =
	PDK7105_OHCI_DEV(1, pdk7105_ohci1_resources);

static void __iomem *pdk7105_sysconf;

static void pdk7105_usb_power_on(unsigned int port)
{
	int i;

	/* Bits 4 and 6 are the host controller and PHY power-down requests. */
	u32 val = readl(pdk7105_sysconf + PDK7105_SYS_CFG32);

	val &= ~(BIT(4 + port) | BIT(6 + port));
	writel(val, pdk7105_sysconf + PDK7105_SYS_CFG32);

	for (i = 0; i < 5; i++) {
		if (!(readl(pdk7105_sysconf + PDK7105_SYS_STA15) &
		      BIT(4 + port)))
			return;
		mdelay(10);
	}

	pr_warn("pdk7105: USB port %u power-up ack timeout\n", port);
}

static void pdk7105_usb_port_init(unsigned int port)
{
	const struct pdk7105_usb_port *p = &pdk7105_usb_ports[port];
	void __iomem *wrapper, *protocol;
	u32 val, req;
	int i, ret;

	pdk7105_usb_power_on(port);

	/*
	 * Overcurrent: active low, from the PIO4 pin, enabled. The DC
	 * shift field is 2 on both ports (STLinux bug 10991).
	 */
	val = readl(pdk7105_sysconf + PDK7105_SYS_CFG04);
	val &= ~BIT(3 + port);
	val &= ~BIT(5 + port);
	val |= BIT(11 + port);
	val &= ~(3 << (13 + 2 * port));
	val |= 2 << (13 + 2 * port);
	writel(val, pdk7105_sysconf + PDK7105_SYS_CFG04);

	/*
	 * These windows are P4 and are not in the PMB RAM map, same as
	 * the sysconf page. 32-bit ioremap does not fall back to a P4
	 * identity address, so the physical value cannot be read directly.
	 */
	wrapper = ioremap(p->wrapper, 0x100);
	protocol = ioremap(p->protocol, 0x100);
	if (!wrapper || !protocol) {
		pr_warn("pdk7105: USB port %u registers unavailable\n", port);
		goto out_unmap;
	}

	/* Clear the 16-bit AHB strap; the PLL strap is left alone. */
	val = readl(wrapper + 0x14);
	val &= ~0x4;
	writel(val, wrapper + 0x14);

	/*
	 * STBus config: read-ahead on, 32-bit opcode, write posting off,
	 * threshold enabled, three packets per chunk, threshold 128.
	 */
	req = (1 << 21) | (5 << 16) | (1 << 14) | (3 << 9) | 7;
	for (i = 0; i < 100; i++) {
		writel(req, protocol + 0x04);
		if ((readl(protocol + 0x04) & 0x7fffffff) == req)
			break;
	}
	if (i == 100)
		pr_warn("pdk7105: USB port %u STBus config did not stick\n",
			port);

out_unmap:
	iounmap(protocol);
	iounmap(wrapper);

	/*
	 * Both pins are alternate 4. OC is an input, power is a push-pull
	 * output. The PIO driver returns -EPROBE_DEFER until it has probed,
	 * which is earlier than this call: both are arch_initcall and the
	 * PIO object is linked before the board file.
	 */
	ret = sti7105_pio_config(PDK7105_USB_PIO_BANK, p->pio_oc,
				 PDK7105_USB_PIO_FN, STI7105_PIO_IN);
	if (!ret)
		ret = sti7105_pio_config(PDK7105_USB_PIO_BANK, p->pio_pwr,
					 PDK7105_USB_PIO_FN, STI7105_PIO_ALT_OUT);
	if (ret)
		pr_warn("pdk7105: USB port %u pins not configured (%d)\n",
			port, ret);
}

static struct completion pdk7105_usb0_done;

static int pdk7105_usb_notify(struct notifier_block *nb, unsigned long action,
			      void *data)
{
	struct usb_device *udev = data;
	struct usb_hcd *hcd;

	/* The root hub has no parent and shows up before any device. */
	if (action != USB_DEVICE_ADD || !udev || !udev->parent || !udev->bus)
		return NOTIFY_OK;

	hcd = bus_to_hcd(udev->bus);
	if (hcd->self.controller == &pdk7105_ehci0_device.dev ||
	    hcd->self.controller == &pdk7105_ohci0_device.dev)
		complete(&pdk7105_usb0_done);

	return NOTIFY_OK;
}

static struct notifier_block pdk7105_usb_nb = {
	.notifier_call = pdk7105_usb_notify,
};

static void pdk7105_usb_set_irq(struct resource *res, unsigned int evt)
{
	res->start = evt2irq(evt);
	res->end = res->start;
}

static int __init pdk7105_usb0_init(void)
{
	/*
	 * The sysconf driver maps the same window and stops at 0x1e0,
	 * short of INTC2. This second mapping only reads and writes the
	 * USB power registers; it does not request the region.
	 */
	pdk7105_sysconf = ioremap(STI7105_SYSCONF_PHYS, STI7105_SYSCONF_SIZE);
	if (!pdk7105_sysconf)
		return -ENOMEM;

	pdk7105_usb_port_init(0);

	pdk7105_usb_set_irq(&pdk7105_ehci0_resources[1],
			    pdk7105_usb_ports[0].evt_ehci);
	pdk7105_usb_set_irq(&pdk7105_ohci0_resources[1],
			    pdk7105_usb_ports[0].evt_ohci);

	/*
	 * The host drivers are device_initcall, so this only registers
	 * the devices. The notifier has to be in place before that probe,
	 * or the stick's USB_DEVICE_ADD is missed and the wait below
	 * always runs out.
	 */
	init_completion(&pdk7105_usb0_done);
	usb_register_notify(&pdk7105_usb_nb);

	platform_device_register(&pdk7105_ehci0_device);
	platform_device_register(&pdk7105_ohci0_device);

	return 0;
}
arch_initcall(pdk7105_usb0_init);

/*
 * Port 0 has to have enumerated a device before port 1 is registered,
 * so a stick on the onboard port is the first SCSI disk. An empty
 * onboard port waits five seconds and boots anyway.
 */
static int __init pdk7105_usb1_init(void)
{
	if (!pdk7105_sysconf)
		return 0;

	if (!wait_for_completion_timeout(&pdk7105_usb0_done, 5 * HZ))
		pr_notice("pdk7105: no device on USB port 0, continuing\n");

	usb_unregister_notify(&pdk7105_usb_nb);

	pdk7105_usb_port_init(1);

	pdk7105_usb_set_irq(&pdk7105_ehci1_resources[1],
			    pdk7105_usb_ports[1].evt_ehci);
	pdk7105_usb_set_irq(&pdk7105_ohci1_resources[1],
			    pdk7105_usb_ports[1].evt_ohci);

	platform_device_register(&pdk7105_ehci1_device);
	platform_device_register(&pdk7105_ohci1_device);

	return 0;
}
late_initcall(pdk7105_usb1_init);

/* MAC addresses from nwhwconf=, applied once the network devices exist. */
#define PDK7105_MAC_MAX	4

static char pdk7105_nw_device[IFNAMSIZ];
static struct {
	char name[IFNAMSIZ];
	u8 addr[ETH_ALEN];
} pdk7105_macs[PDK7105_MAC_MAX];
static int pdk7105_mac_count;

int pdk7105_phy_addr __read_mostly = -1;
EXPORT_SYMBOL(pdk7105_phy_addr);

/*
 * Factory bootargs put phyaddr in the next word after nwhwconf= rather
 * than inside it ("hwaddr:.. phyaddr:0,watchdog:5000"). The kernel
 * stops a parameter at the first space, so the PHY address has to be
 * taken from the whole command line. Last well-formed value wins.
 */
static void __init pdk7105_scan_phyaddr(const char *cmd)
{
	const char *p = cmd;

	if (!p)
		return;

	while ((p = strstr(p, "phyaddr:")) != NULL) {
		char buf[8];
		unsigned int val;
		size_t n = 0;

		p += 8;
		while (n < sizeof(buf) - 1 && isdigit(*p))
			buf[n++] = *p++;
		buf[n] = '\0';
		if (!n || kstrtouint(buf, 10, &val) || val > 31) {
			pr_warn("nwhwconf: bad phyaddr \"%s\"\n", buf);
			continue;
		}
		pdk7105_phy_addr = val;
	}
}

static int __init pdk7105_nwhwconf_setup(char *str)
{
	char *p = str;

	while (*p) {
		char *tok, *arg;
		unsigned int val;

		while (*p == ',' || isspace(*p))
			p++;
		if (!*p)
			break;

		tok = p;
		while (*p && *p != ',' && !isspace(*p))
			p++;
		if (*p)
			*p++ = '\0';

		arg = strchr(tok, ':');
		if (!arg) {
			pr_debug("nwhwconf: ignoring \"%s\"\n", tok);
			continue;
		}
		*arg++ = '\0';

		if (!strcmp(tok, "device")) {
			strscpy(pdk7105_nw_device, arg, IFNAMSIZ);
		} else if (!strcmp(tok, "hwaddr")) {
			if (!pdk7105_nw_device[0]) {
				pr_warn("nwhwconf: hwaddr without device\n");
				continue;
			}
			if (pdk7105_mac_count >= PDK7105_MAC_MAX)
				continue;
			if (!mac_pton(arg,
				      pdk7105_macs[pdk7105_mac_count].addr) ||
			    !is_valid_ether_addr(pdk7105_macs[pdk7105_mac_count].addr)) {
				pr_warn("nwhwconf: bad hwaddr \"%s\"\n", arg);
				continue;
			}
			strscpy(pdk7105_macs[pdk7105_mac_count].name,
				pdk7105_nw_device, IFNAMSIZ);
			pdk7105_mac_count++;
		} else if (!strcmp(tok, "phyaddr")) {
			val = 0;
			if (kstrtouint(arg, 10, &val) || val > 31) {
				pr_warn("nwhwconf: bad phyaddr \"%s\"\n", arg);
				continue;
			}
			pdk7105_phy_addr = val;
		} else {
			pr_debug("nwhwconf: ignoring \"%s\"\n", tok);
		}
	}

	pdk7105_scan_phyaddr(boot_command_line);

	return 1;
}
__setup("nwhwconf=", pdk7105_nwhwconf_setup);

/*
 * ST's bootloader reserves the large physically contiguous area for the
 * video blocks. Nothing here allocates it; the video phase will.
 */
static int __init pdk7105_bigphysarea_setup(char *str)
{
	return 1;
}
__setup("bigphysarea=", pdk7105_bigphysarea_setup);

static int __init pdk7105_apply_macs(void)
{
	int i;

	for (i = 0; i < pdk7105_mac_count; i++) {
		struct sockaddr_storage ss = { .ss_family = 0 };
		struct net_device *dev;

		dev = dev_get_by_name(&init_net, pdk7105_macs[i].name);
		if (!dev)
			continue;

		/*
		 * Always overwrite. stmmac invents a random address when
		 * it probes with none, and that address is valid, so a
		 * check for an invalid one would keep the random value.
		 */
		ss.ss_family = dev->type;
		memcpy(((struct sockaddr *)&ss)->sa_data,
		       pdk7105_macs[i].addr, ETH_ALEN);
		rtnl_lock();
		if (dev_set_mac_address(dev, &ss, NULL))
			pr_warn("nwhwconf: %s: MAC not set\n", dev->name);
		rtnl_unlock();
		dev_put(dev);
	}

	return 0;
}

/*
 * late_initcall, not device_initcall. This file is linked from
 * arch/sh, before drivers/, so a device_initcall here runs before
 * dwmac-generic registers. The netdev does not exist yet, and stmmac
 * then probes with no address and installs a random one. Level 7 is
 * after that probe.
 */
late_initcall(pdk7105_apply_macs);

/*
 * On-chip GMAC0. Not a PCI device: the MAC sits at 0xfd110000 and its
 * interrupt is INTC2 event 0x12c0, named macirq because that is the
 * name stmmac_get_platform_resources() looks up. The platform data is
 * consumed by dwmac-generic. core_type must be set: its zero value is
 * the MAC100 core, not this GMAC.
 *
 * The PHY clock is external (the old ext_clk = 1), so PIO9.5 is left
 * alone, and so is PIO9.7, which is the HDMI hotplug pin. MII inputs
 * are alternate 1 with direction only; the old pad code stored -1,
 * which selected the same function.
 */
#define PDK7105_GMAC_BASE	0xfd110000
#define PDK7105_GMAC_SIZE	0x8000
#define PDK7105_GMAC_EVT	0x12c0
#define PDK7105_GMAC_MII_FN	1
#define PDK7105_PHY_RESET_BANK	15
#define PDK7105_PHY_RESET_PIN	5

static struct stmmac_dma_cfg pdk7105_gmac_dma = {
	.pbl = 32,
};

static struct stmmac_mdio_bus_data pdk7105_gmac_mdio = {
	.phy_mask = 0,
};

static void pdk7105_gmac_fix_mac_speed(void *priv, phy_interface_t interface,
				       int speed, unsigned int mode)
{
	/* SYS_CFG7 bit 20 selects 100 Mbit. 10 and 1000 leave it clear. */
	sti7105_sysconf_write(7, 20, 20, speed == SPEED_100);
}

/*
 * One MII pin. An input only sets the direction; the function field of
 * the old pad table was -1 for those, which is alternate 1 here.
 */
struct pdk7105_mii_pin {
	u8 bank;
	u8 pin;
	u8 mode;
};

static const struct pdk7105_mii_pin pdk7105_mii_pins[] = {
	{ 7, 4, STI7105_PIO_IN },	/* RXDV */
	{ 7, 5, STI7105_PIO_IN },	/* RXERR */
	{ 7, 6, STI7105_PIO_ALT_OUT },	/* TXD.0 */
	{ 7, 7, STI7105_PIO_ALT_OUT },	/* TXD.1 */
	{ 8, 0, STI7105_PIO_ALT_OUT },	/* TXD.2 */
	{ 8, 1, STI7105_PIO_ALT_OUT },	/* TXD.3 */
	{ 8, 2, STI7105_PIO_ALT_OUT },	/* TXEN */
	{ 8, 3, STI7105_PIO_ALT_OUT },	/* MDIO, driven, not bidirectional */
	{ 8, 4, STI7105_PIO_ALT_OUT },	/* MDC */
	{ 8, 5, STI7105_PIO_IN },	/* RXCLK */
	{ 8, 6, STI7105_PIO_IN },	/* RXD.0 */
	{ 8, 7, STI7105_PIO_IN },	/* RXD.1 */
	{ 9, 0, STI7105_PIO_IN },	/* RXD.2 */
	{ 9, 1, STI7105_PIO_IN },	/* RXD.3 */
	{ 9, 2, STI7105_PIO_IN },	/* TXCLK */
	{ 9, 3, STI7105_PIO_IN },	/* COL */
	{ 9, 4, STI7105_PIO_IN },	/* CRS */
	{ 9, 6, STI7105_PIO_IN },	/* MDINT */
};

static int pdk7105_gmac_init(struct device *dev, void *priv)
{
	int i, ret;

	/*
	 * SYS_CFG7: bit 16 enables the interface, bit 27 selects MII
	 * rather than reverse MII. Bit 17 takes MDIO from the GMAC,
	 * bit 18 stays MII rather than RMII, and bits 26:25 select
	 * GMII/MII. Bit 20 is the speed select and is written later.
	 */
	sti7105_sysconf_write(7, 16, 16, 1);
	sti7105_sysconf_write(7, 27, 27, 1);
	sti7105_sysconf_write(7, 17, 17, 0);
	sti7105_sysconf_write(7, 18, 18, 0);
	sti7105_sysconf_write(7, 25, 26, 0);

	for (i = 0; i < ARRAY_SIZE(pdk7105_mii_pins); i++) {
		const struct pdk7105_mii_pin *p = &pdk7105_mii_pins[i];

		ret = sti7105_pio_config(p->bank, p->pin, PDK7105_GMAC_MII_FN,
					 p->mode);
		if (ret) {
			dev_warn(dev, "MII pin %u.%u not configured (%d)\n",
				 p->bank, p->pin, ret);
			return ret;
		}
	}

	/* PHY reset is GPIO, not an alternate function. */
	ret = sti7105_pio_config(PDK7105_PHY_RESET_BANK, PDK7105_PHY_RESET_PIN,
				 0, STI7105_PIO_OUT);
	if (ret)
		return ret;
	sti7105_pio_set(PDK7105_PHY_RESET_BANK, PDK7105_PHY_RESET_PIN, 0);
	mdelay(1);
	sti7105_pio_set(PDK7105_PHY_RESET_BANK, PDK7105_PHY_RESET_PIN, 1);

	return 0;
}

static struct plat_stmmacenet_data pdk7105_gmac_data = {
	.core_type		= DWMAC_CORE_GMAC,
	.bus_id			= 0,
	.phy_addr		= -1,
	.phy_interface		= PHY_INTERFACE_MODE_MII,
	.mdio_bus_data		= &pdk7105_gmac_mdio,
	.dma_cfg		= &pdk7105_gmac_dma,
	.clk_csr		= STMMAC_CSR_60_100M,
	.enh_desc		= true,
	.tx_coe			= true,
	.bugged_jumbo		= true,
	.pmt			= true,
	.maxmtu			= 1500,
	.rx_queues_to_use	= 1,
	.tx_queues_to_use	= 1,
	.fix_mac_speed		= pdk7105_gmac_fix_mac_speed,
	.init			= pdk7105_gmac_init,
};

static u64 pdk7105_gmac_dma_mask = DMA_BIT_MASK(32);

static struct resource pdk7105_gmac_resources[] = {
	DEFINE_RES_MEM(PDK7105_GMAC_BASE, PDK7105_GMAC_SIZE),
	DEFINE_RES_IRQ_NAMED(0, "macirq"),
};

static struct platform_device pdk7105_gmac_device = {
	.name		= "stmmaceth",
	.id		= 0,
	.resource	= pdk7105_gmac_resources,
	.num_resources	= ARRAY_SIZE(pdk7105_gmac_resources),
	.dev = {
		.platform_data		= &pdk7105_gmac_data,
		.dma_mask		= &pdk7105_gmac_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

/*
 * device_initcall. The dwmac driver is not registered yet, so this
 * only publishes the device; the probe happens when that driver is
 * registered, later in the same initcall level. The stored MAC is
 * applied from pdk7105_apply_macs(), which is a late_initcall.
 */
static int __init pdk7105_gmac_register(void)
{
	pdk7105_gmac_data.phy_addr = pdk7105_phy_addr;
	pdk7105_usb_set_irq(&pdk7105_gmac_resources[1], PDK7105_GMAC_EVT);

	return platform_device_register(&pdk7105_gmac_device);
}
device_initcall(pdk7105_gmac_register);

/*
 * On-chip IRB, the infrared receiver. st-rc looks the device up as
 * "st-rc" and takes the first memory and the first IRQ resource.
 * The clock is the unnamed clock of "st-rc.0", registered with the
 * 100 MHz comms clock. There is no device tree, so the driver takes
 * infrared rather than UHF when no rx-mode property exists.
 *
 * PIO3.0 is the infrared input. The old driver sets its direction
 * and, because the function it was given is -1, does not touch the
 * alternate-function planes, so this does the same. PIO3.2 and PIO3.3
 * are the transmitter outputs and stay untouched.
 */
#define PDK7105_IRB_BASE	0xfd018000
#define PDK7105_IRB_SIZE	0x234
#define PDK7105_IRB_EVT		0x11a0
#define PDK7105_IRB_RX_BANK	3
#define PDK7105_IRB_RX_PIN	0

static struct resource pdk7105_irb_resources[] = {
	DEFINE_RES_MEM(PDK7105_IRB_BASE, PDK7105_IRB_SIZE),
	DEFINE_RES_IRQ(0),
};

static struct platform_device pdk7105_irb_device = {
	.name		= "st-rc",
	.id		= 0,
	.resource	= pdk7105_irb_resources,
	.num_resources	= ARRAY_SIZE(pdk7105_irb_resources),
};

static int __init pdk7105_irb_register(void)
{
	int ret;

	ret = sti7105_pio_direction(PDK7105_IRB_RX_BANK, PDK7105_IRB_RX_PIN,
				    STI7105_PIO_IN);
	if (ret)
		return ret;

	pdk7105_usb_set_irq(&pdk7105_irb_resources[1], PDK7105_IRB_EVT);

	return platform_device_register(&pdk7105_irb_device);
}
device_initcall(pdk7105_irb_register);

/*
 * sh_tmu looks the clock up as "fck" on "sh-tmu.0" and divides by
 * four. st-asc takes the unnamed clock of "st-asc.0".
 *
 * clk_init() calls this before late_time_init probes the TMU, so the
 * clock has to come from the machine vector rather than an initcall.
 * Clockgen A replaces the comms clock when it can read its registers;
 * the timer clock is not one of its outputs and stays fixed.
 */
static int __init pdk7105_clk_init(void)
{
	struct clk_hw *tmu, *comms;

	tmu = clk_hw_register_fixed_rate(NULL, "tmu_fck", NULL, 0,
					 PDK7105_TMU_HZ);
	if (!IS_ERR(tmu))
		clk_hw_register_clkdev(tmu, "fck", "sh-tmu.0");

	if (stx7105_clkgena_init()) {
		comms = clk_hw_register_fixed_rate(NULL, "comms_clk", NULL, 0,
						   PDK7105_COMMS_HZ);
		if (!IS_ERR(comms)) {
			clk_hw_register_clkdev(comms, NULL, "st-asc.0");
			clk_hw_register_clkdev(comms, NULL, "st-rc.0");
		}
	}

	return 0;
}

static struct resource pdk7105_sysconf_resource[] = {
	DEFINE_RES_MEM(STI7105_SYSCONF_PHYS, STI7105_SYSCONF_SIZE),
};

static struct platform_device pdk7105_sysconf_device = {
	.name		= "sti7105-sysconf",
	.id		= -1,
	.resource	= pdk7105_sysconf_resource,
	.num_resources	= ARRAY_SIZE(pdk7105_sysconf_resource),
};

/*
 * The PIO driver maps each bank from PDK7105_PIO_BASE() itself. The
 * resource only has to exist; bank 0's window names the device.
 */
static struct resource pdk7105_pio_resource[] = {
	DEFINE_RES_MEM(0xfd020000, 0x1000),
};

static struct platform_device pdk7105_pio_device = {
	.name		= "sti7105-pio",
	.id		= -1,
	.resource	= pdk7105_pio_resource,
	.num_resources	= ARRAY_SIZE(pdk7105_pio_resource),
};

static struct resource pdk7105_ilc_resource[] = {
	DEFINE_RES_MEM(PDK7105_ILC_PHYS, PDK7105_ILC_SIZE),
};

static struct platform_device pdk7105_ilc_device = {
	.name		= "sti7105-ilc3",
	.id		= -1,
	.resource	= pdk7105_ilc_resource,
	.num_resources	= ARRAY_SIZE(pdk7105_ilc_resource),
};

/*
 * One 16 MB page of the PCI region, at the address U-Boot used. A
 * 512 MB window does not fit below P3SEG. The bolt happens from a
 * device_initcall, which is after pmb_init() has dropped every
 * bootloader entry whose physical page is outside RAM and has shrunk
 * the uncached alias to 16 MB. That alias follows RAM and ends well
 * below 0xa7000000, so the two do not overlap.
 */
static int __init pdk7105_pmb_init(void)
{
	int ret;

	ret = pmb_bolt_mapping(PDK7105_PCI_MEM_VIRT, PDK7105_PCI_MEM_PHYS,
			       PDK7105_PCI_MEM_SIZE, PAGE_KERNEL_NOCACHE);
	if (ret)
		pr_warn("pdk7105: PCI window not bolted (%d)\n", ret);

	return 0;
}
device_initcall(pdk7105_pmb_init);

static int __init pdk7105_devices_setup(void)
{
	asc2_resources[1].start = evt2irq(PDK7105_ASC2_EVT);
	asc2_resources[1].end = asc2_resources[1].start;

	platform_device_register(&pdk7105_sysconf_device);
	platform_device_register(&pdk7105_pio_device);
	platform_device_register(&pdk7105_ilc_device);

	return platform_device_register(&asc2_device);
}
arch_initcall(pdk7105_devices_setup);

static struct sh_machine_vector mv_pdk7105 __initmv = {
	.mv_name	= "PDK7105",
	.mv_clk_init	= pdk7105_clk_init,
};
