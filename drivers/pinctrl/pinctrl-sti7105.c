// SPDX-License-Identifier: GPL-2.0
/*
 * STi7105 pin controller, 17 PIO banks of 8 pins.
 *
 * Direction is three bits per pin, one in each of PC0, PC1 and PC2.
 * Each bit has its own set and clear register, so a mode change does
 * not read the register back. Input high-Z is mode 4, alternate
 * push-pull output is mode 6.
 *
 * The alternate function is not in the PIO block. Each bank has one
 * SYS_CFG register, and function N is stored as N-1 in bit planes
 * [pin], [8+pin] and [16+pin]. Bank 14 has no such register. This is
 * not the later pinctrl-st driver: that one describes a different SoC.
 */
#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/soc/sti/sti7105-sysconf.h>
#include <mach/pdk7105.h>

#include <linux/pinctrl/pinctrl-sti7105.h>

#define STI7105_PIO_PIN(bank, pin)	((bank) * 8 + (pin))
#define STI7105_PINS			(PDK7105_PIO_BANKS * 8)

#define PIO_POUT		0x00
#define PIO_SET_POUT		0x04
#define PIO_CLR_POUT		0x08
#define PIO_PIN			0x10
#define PIO_PC(n)		(0x20 + (n) * 0x10)
#define PIO_SET_PC(n)		(PIO_PC(n) + 0x04)
#define PIO_CLR_PC(n)		(PIO_PC(n) + 0x08)

/*
 * SYS_CFG number and highest alternate function for each bank. A zero
 * sysconf means the bank has none (bank 14).
 */
static const u8 sti7105_pio_cfg[] = {
	19, 20, 21, 25, 34, 35, 36, 37, 46, 47, 39, 53, 48, 49, 0, 50, 54,
};

static const u8 sti7105_pio_max_fn[] = {
	5, 4, 4, 4, 4, 4, 4, 4, 3, 4, 2, 4, 5, 5, 1, 4, 2,
};

struct sti7105_pio {
	void __iomem *bank[PDK7105_PIO_BANKS];
	struct pinctrl_dev *pctl;
	struct gpio_chip gc;
	struct pinctrl_pin_desc pins[STI7105_PINS];
	const char *names[STI7105_PINS];
	char namebuf[STI7105_PINS][8];
};

static struct sti7105_pio *sti7105_pio_gc(struct gpio_chip *gc)
{
	return container_of(gc, struct sti7105_pio, gc);
}

static void sti7105_pio_mode(struct sti7105_pio *pio, unsigned int offset,
			     unsigned int mode)
{
	void __iomem *base = pio->bank[offset / 8];
	u32 bit = BIT(offset % 8);
	int i;

	for (i = 0; i < 3; i++)
		writel(bit, base + ((mode & BIT(i)) ? PIO_SET_PC(i) :
							   PIO_CLR_PC(i)));
}

static u32 sti7105_pio_get_mode(struct sti7105_pio *pio, unsigned int offset)
{
	void __iomem *base = pio->bank[offset / 8];
	u32 bit = BIT(offset % 8);
	u32 mode = 0;
	int i;

	for (i = 0; i < 3; i++)
		if (readl(base + PIO_PC(i)) & bit)
			mode |= BIT(i);
	return mode;
}

/*
 * Program the alternate function of one pin. Function 0 is plain GPIO
 * and clears the three planes. Function N is stored as N-1.
 */
static int sti7105_pio_set_function(struct sti7105_pio *pio, unsigned int offset,
				    unsigned int function)
{
	unsigned int bank = offset / 8;
	unsigned int pin = offset % 8;
	int ret;

	if (bank >= PDK7105_PIO_BANKS || function > sti7105_pio_max_fn[bank])
		return -EINVAL;
	if (!sti7105_pio_cfg[bank])
		return function <= 1 ? 0 : -EINVAL;

	if (function)
		function--;

	/*
	 * Each plane is one bit of the function number. The helper
	 * replaces a field, so the other pins of the bank are untouched.
	 */
	ret = sti7105_sysconf_write(sti7105_pio_cfg[bank], pin, pin,
				    function & 1);
	if (ret)
		return ret;
	ret = sti7105_sysconf_write(sti7105_pio_cfg[bank], 8 + pin, 8 + pin,
				    (function >> 1) & 1);
	if (ret)
		return ret;
	return sti7105_sysconf_write(sti7105_pio_cfg[bank], 16 + pin, 16 + pin,
				     (function >> 2) & 1);
}

static int sti7105_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct sti7105_pio *pio = sti7105_pio_gc(gc);

	return !!(readl(pio->bank[offset / 8] + PIO_PIN) & BIT(offset % 8));
}

static int sti7105_gpio_set(struct gpio_chip *gc, unsigned int offset, int value)
{
	struct sti7105_pio *pio = sti7105_pio_gc(gc);

	writel(BIT(offset % 8),
	       pio->bank[offset / 8] + (value ? PIO_SET_POUT : PIO_CLR_POUT));
	return 0;
}

static int sti7105_gpio_direction_input(struct gpio_chip *gc,
					unsigned int offset)
{
	sti7105_pio_mode(sti7105_pio_gc(gc), offset, PDK7105_PIO_IN);
	return 0;
}

static int sti7105_gpio_direction_output(struct gpio_chip *gc,
					 unsigned int offset, int value)
{
	struct sti7105_pio *pio = sti7105_pio_gc(gc);

	sti7105_gpio_set(gc, offset, value);
	/* Push-pull output is mode 2. */
	sti7105_pio_mode(pio, offset, 2);
	return 0;
}

static int sti7105_gpio_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	u32 mode = sti7105_pio_get_mode(sti7105_pio_gc(gc), offset);

	/* Modes 2 and 6 drive the pin. Everything else is an input. */
	if (mode == 2 || mode == PDK7105_PIO_ALT_OUT)
		return GPIO_LINE_DIRECTION_OUT;
	return GPIO_LINE_DIRECTION_IN;
}

static int sti7105_pctl_groups(struct pinctrl_dev *pctldev)
{
	return 0;
}

static const char *sti7105_pctl_group_name(struct pinctrl_dev *pctldev,
					   unsigned int selector)
{
	return NULL;
}

static int sti7105_pctl_group_pins(struct pinctrl_dev *pctldev,
				   unsigned int selector,
				   const unsigned int **pins,
				   unsigned int *num_pins)
{
	return -EINVAL;
}

static const struct pinctrl_ops sti7105_pctl_ops = {
	.get_groups_count = sti7105_pctl_groups,
	.get_group_name = sti7105_pctl_group_name,
	.get_group_pins = sti7105_pctl_group_pins,
};

static int sti7105_pmx_functions(struct pinctrl_dev *pctldev)
{
	return 0;
}

static const char *sti7105_pmx_function_name(struct pinctrl_dev *pctldev,
					     unsigned int selector)
{
	return NULL;
}

static int sti7105_pmx_function_groups(struct pinctrl_dev *pctldev,
				       unsigned int selector,
				       const char * const **groups,
				       unsigned int *num_groups)
{
	return -EINVAL;
}

static int sti7105_pmx_set(struct pinctrl_dev *pctldev, unsigned int func,
			   unsigned int group)
{
	return -EINVAL;
}

/*
 * A GPIO request clears the alternate function and leaves the
 * direction to the gpiochip call that follows.
 */
static int sti7105_pmx_gpio_request(struct pinctrl_dev *pctldev,
				    struct pinctrl_gpio_range *range,
				    unsigned int offset)
{
	struct sti7105_pio *pio = pinctrl_dev_get_drvdata(pctldev);

	offset += range->pin_base;
	return sti7105_pio_set_function(pio, offset, 0);
}

static const struct pinmux_ops sti7105_pmx_ops = {
	.get_functions_count = sti7105_pmx_functions,
	.get_function_name = sti7105_pmx_function_name,
	.get_function_groups = sti7105_pmx_function_groups,
	.set_mux = sti7105_pmx_set,
	.gpio_request_enable = sti7105_pmx_gpio_request,
};

static struct sti7105_pio *sti7105_pio_dev;

/*
 * Select the alternate function and the direction for one pin. The
 * board uses it for the USB overcurrent and power pins, which are
 * alternate 4: input for overcurrent, push-pull output for power.
 * Returns -EPROBE_DEFER until the controller has probed.
 */
int sti7105_pio_config(unsigned int bank, unsigned int pin,
		       unsigned int function, unsigned int mode)
{
	struct sti7105_pio *pio = READ_ONCE(sti7105_pio_dev);
	unsigned int offset;
	int ret;

	if (!pio)
		return -EPROBE_DEFER;
	if (bank >= PDK7105_PIO_BANKS || pin > 7)
		return -EINVAL;

	offset = STI7105_PIO_PIN(bank, pin);
	ret = sti7105_pio_set_function(pio, offset, function);
	if (ret)
		return ret;
	sti7105_pio_mode(pio, offset, mode);
	return 0;
}
EXPORT_SYMBOL_GPL(sti7105_pio_config);

static int sti7105_pio_probe(struct platform_device *pdev)
{
	struct sti7105_pio *pio;
	struct pinctrl_desc *desc;
	int i, ret;

	if (!sti7105_sysconf_base_get())
		return -EPROBE_DEFER;

	pio = devm_kzalloc(&pdev->dev, sizeof(*pio), GFP_KERNEL);
	if (!pio)
		return -ENOMEM;

	for (i = 0; i < PDK7105_PIO_BANKS; i++) {
		pio->bank[i] = devm_ioremap(&pdev->dev, PDK7105_PIO_BASE(i),
					    0x100);
		if (!pio->bank[i])
			return -ENOMEM;
	}

	for (i = 0; i < STI7105_PINS; i++) {
		scnprintf(pio->namebuf[i], sizeof(pio->namebuf[i]), "pio%d.%d",
			  i / 8, i % 8);
		pio->names[i] = pio->namebuf[i];
		pio->pins[i].number = i;
		pio->pins[i].name = pio->namebuf[i];
	}

	desc = devm_kzalloc(&pdev->dev, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;
	desc->name = "sti7105-pio";
	desc->pins = pio->pins;
	desc->npins = STI7105_PINS;
	desc->pctlops = &sti7105_pctl_ops;
	desc->pmxops = &sti7105_pmx_ops;
	desc->owner = THIS_MODULE;

	ret = devm_pinctrl_register_and_init(&pdev->dev, desc, pio, &pio->pctl);
	if (ret)
		return ret;

	pio->gc.label = "sti7105-pio";
	pio->gc.parent = &pdev->dev;
	pio->gc.owner = THIS_MODULE;
	pio->gc.base = 0;
	pio->gc.ngpio = STI7105_PINS;
	pio->gc.names = pio->names;
	pio->gc.get = sti7105_gpio_get;
	pio->gc.set = sti7105_gpio_set;
	pio->gc.direction_input = sti7105_gpio_direction_input;
	pio->gc.direction_output = sti7105_gpio_direction_output;
	pio->gc.get_direction = sti7105_gpio_get_direction;

	ret = devm_gpiochip_add_data(&pdev->dev, &pio->gc, pio);
	if (ret)
		return ret;

	ret = gpiochip_add_pin_range(&pio->gc, dev_name(&pdev->dev), 0, 0,
				     STI7105_PINS);
	if (ret)
		return ret;

	ret = pinctrl_enable(pio->pctl);
	if (ret)
		return ret;

	WRITE_ONCE(sti7105_pio_dev, pio);
	return 0;
}

static struct platform_driver sti7105_pio_driver = {
	.probe	= sti7105_pio_probe,
	.driver	= {
		.name = "sti7105-pio",
	},
};

static int __init sti7105_pio_init(void)
{
	return platform_driver_register(&sti7105_pio_driver);
}
arch_initcall(sti7105_pio_init);
