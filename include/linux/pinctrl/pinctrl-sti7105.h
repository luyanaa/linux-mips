/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PINCTRL_STI7105_H
#define __PINCTRL_STI7105_H

/*
 * Direction modes of an STi7105 PIO pin. The value is the three PC
 * bits: bit 0 in PC0, bit 1 in PC1, bit 2 in PC2.
 */
#define STI7105_PIO_IN		4
#define STI7105_PIO_OUT		2
#define STI7105_PIO_ALT_OUT	6

int sti7105_pio_config(unsigned int bank, unsigned int pin,
		       unsigned int function, unsigned int mode);

#endif /* __PINCTRL_STI7105_H */
