/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __CLK_STX7105_H
#define __CLK_STX7105_H

/*
 * Read Clockgen A back and publish its outputs. Never writes a PLL or
 * the output mux. Returns 0, or an errno when the block cannot be
 * mapped; the caller then keeps its fixed comms clock.
 */
int stx7105_clkgena_init(void);

#endif /* __CLK_STX7105_H */
