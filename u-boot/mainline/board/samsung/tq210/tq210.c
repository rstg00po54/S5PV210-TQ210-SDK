// SPDX-License-Identifier: GPL-2.0+
/*
 * TQ210 CoreB: RAM-loaded U-Boot proper.
 * OpenBoot configures clocks, UART0 and both DDR2 controllers.
 */
#include <init.h>
#include <asm/global_data.h>
#include <linux/sizes.h>

DECLARE_GLOBAL_DATA_PTR;

int board_init(void)
{
	gd->bd->bi_boot_params = CFG_SYS_SDRAM_BASE + 0x100;
	gd->bd->bi_arch_number = ~0U;
	return 0;
}

int dram_init(void)
{
	gd->ram_size = SZ_1G;
	return 0;
}

int dram_init_banksize(void)
{
	gd->dram[0].start = CFG_SYS_SDRAM_BASE;
	gd->dram[0].size = SZ_512M;
	gd->dram[1].start = CFG_SYS_SDRAM_BASE + SZ_512M;
	gd->dram[1].size = SZ_512M;
	return 0;
}
