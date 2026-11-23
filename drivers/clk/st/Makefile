# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_ARCH_STI) += clkgen-mux.o clkgen-pll.o clkgen-fsyn.o clk-flexgen.o

# STi7105 Clockgen A. Not part of the ARCH_STI flexgen set: that one
# describes a different SoC and is not built for SuperH.
obj-$(CONFIG_CPU_SUBTYPE_ST40)	+= clk-stx7105.o
