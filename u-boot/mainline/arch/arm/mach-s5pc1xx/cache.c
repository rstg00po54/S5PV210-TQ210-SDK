// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2014 Samsung Electronics
 * Minkyu Kang <mk7.kang@samsung.com>
 * Robert Baldyga <r.baldyga@samsung.com>
 *
 * based on arch/arm/cpu/armv7/omap3/cache.S
 */

#include <cpu_func.h>
#include <asm/cache.h>

#if !CONFIG_IS_ENABLED(SYS_DCACHE_OFF)
void enable_caches(void)
{
	/* TQ210 bring-up: enable manually from the relocated Shell. */
	if (!IS_ENABLED(CONFIG_TARGET_TQ210))
		dcache_enable();
}

void disable_caches(void)
{
	dcache_disable();
}
#endif

#ifndef CONFIG_SYS_L2CACHE_OFF
/* Cortex-A8 integrated L2: avoid the old stack-manipulating inline asm. */
void v7_outer_cache_enable(void)
{
	unsigned int actlr;

	asm volatile("mrc p15, 0, %0, c1, c0, 1" : "=r" (actlr));
	actlr |= 2;
	asm volatile("dsb sy\n\tmcr p15, 0, %0, c1, c0, 1\n\tisb"
		     : : "r" (actlr) : "memory");
}

void v7_outer_cache_disable(void)
{
	unsigned int actlr;

	asm volatile("mrc p15, 0, %0, c1, c0, 1" : "=r" (actlr));
	actlr &= ~2U;
	asm volatile("dsb sy\n\tmcr p15, 0, %0, c1, c0, 1\n\tisb"
		     : : "r" (actlr) : "memory");
}
#endif
