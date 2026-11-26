// SPDX-License-Identifier: GPL-2.0+
/*
 * TQ210 CoreB: RAM-loaded U-Boot proper.
 * OpenBoot configures clocks, UART0 and both DDR2 controllers.
 */
#include <init.h>
#include <command.h>
#include <env.h>
#include <fastboot.h>
#include <linux/libfdt.h>
#include <time.h>
#include <asm/io.h>
#include <asm/arch/cpu.h>
#include <asm/arch/clk.h>
#include <asm/arch/pwm.h>
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

static int do_tqinfo(struct cmd_tbl *cmdtp, int flag, int argc,
		     char *const argv[])
{
	u32 sctlr, actlr, ttbr0;
	struct s5p_timer *pwm = (struct s5p_timer *)S5PC110_PWMTIMER_BASE;

	asm volatile("mrc p15, 0, %0, c1, c0, 0" : "=r" (sctlr));
	asm volatile("mrc p15, 0, %0, c1, c0, 1" : "=r" (actlr));
	asm volatile("mrc p15, 0, %0, c2, c0, 0" : "=r" (ttbr0));
	printf("SCTLR=%08x ACTLR=%08x TTBR0=%08x\n", sctlr, actlr, ttbr0);
	printf("MMU=%s I-cache=%s D-cache=%s L2=%s\n",
	       sctlr & 1 ? "ON" : "OFF",
	       sctlr & (1U << 12) ? "ON" : "OFF",
	       sctlr & (1U << 2) ? "ON" : "OFF",
	       actlr & 2 ? "ON" : "OFF");
	printf("PWM clock=%lu Hz, timer4=%lu Hz\n", get_pwm_clk(), get_tbclk());
	printf("TCFG0=%08x TCFG1=%08x TCON=%08x TCNTB4=%08x TCNTO4=%08x\n",
	       readl(&pwm->tcfg0), readl(&pwm->tcfg1), readl(&pwm->tcon),
	       readl(&pwm->tcntb4), readl(&pwm->tcnto4));
	printf("Elapsed ms=%lu\n", get_timer(0));
	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(tqinfo, 1, 1, do_tqinfo,
	   "show TQ210 clock, timer and CP15 state", "");

/*
 * Unified FIT session, separate from the original manual 0x26000000
 * Fastboot buffer. Keep the entire FIT in the second DDR bank.
 * Boot only after the host explicitly sends continue and USB is released.
 */
#define TQ210_FIT_ADDR 0x40000000UL
#define TQ210_FIT_SIZE SZ_256M

static int do_tqboot(struct cmd_tbl *cmdtp, int flag, int argc,
		    char *const argv[])
{
	const void *fit = (const void *)TQ210_FIT_ADDR;
	ulong size;
	int ret;

	env_set("filesize", NULL);
	env_set("tq210_continue", NULL);
	puts("TQ210: waiting for FIT via USB (Ctrl+C for shell)\n");
	ret = fastboot_usb_run(0, (void *)TQ210_FIT_ADDR, TQ210_FIT_SIZE);
	if (ret || !env_get("tq210_continue")) {
		puts("TQ210: USB boot cancelled or failed\n");
		return CMD_RET_FAILURE;
	}
	env_set("tq210_continue", NULL);
	size = env_get_hex("filesize", 0);
	if (size < sizeof(struct fdt_header) || size > TQ210_FIT_SIZE ||
	    fdt_check_header(fit) || fdt_totalsize(fit) != size) {
		puts("TQ210: invalid or incomplete FIT; not booting\n");
		return CMD_RET_FAILURE;
	}
	puts("TQ210: USB detached, booting FIT\n");
	return run_command("bootm 40000000", 0);
}

U_BOOT_CMD(tqboot, 1, 0, do_tqboot,
	   "receive and boot a FIT over USB without writing storage", "");
