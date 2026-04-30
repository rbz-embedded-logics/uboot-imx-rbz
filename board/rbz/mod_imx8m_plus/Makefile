#
# Copyright 2021 RBZ
#
# SPDX-License-Identifier:      GPL-2.0+
#

obj-y += mod_imx8m_plus.o

ifdef CONFIG_SPL_BUILD
obj-y += spl.o
obj-$(CONFIG_IMX8M_LPDDR4) += lpddr4_timing_1gb_micron.o
obj-$(CONFIG_IMX8M_LPDDR4) += lpddr4_timing_4gb_micron.o
obj-$(CONFIG_IMX8M_LPDDR4) += lpddr4_timing_2gb_micron.o
endif
