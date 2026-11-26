# SPDX-License-Identifier: GPL-2.0+
#
# Copyright (C) 2009 Samsung Electronics
# Minkyu Kang <mk7.kang@samsung.com>

obj-$(CONFIG_PWM_S5P) += pwm.o
ifdef CONFIG_ARCH_NEXELL
obj-$(CONFIG_S5P4418_ONEWIRE) += pwm.o
else
obj-y += cpu_info.o
ifndef CONFIG_XPL_BUILD
ifdef CONFIG_TARGET_TQ210
obj-y += timer-tq210.o
else
obj-y += timer.o
endif
obj-y += sromc.o
endif
endif
