// SPDX-License-Identifier: GPL-2.0+
/*
 * TQ210 PWM4 free-running clock, based on the inherited ROM PCLK.
 * No PLL or DDR changes. Keep all timer state in gd for relocation.
 */
#include <div64.h>
#include <init.h>
#include <time.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/arch/cpu.h>
#include <asm/arch/clk.h>
#include <asm/arch/pwm.h>

DECLARE_GLOBAL_DATA_PTR;

#define TQ210_PWM_PRESCALE 16U
#define TQ210_PWM_DIV      4U

static struct s5p_timer *pwm_timer(void)
{
	return (struct s5p_timer *)S5PC110_PWMTIMER_BASE;
}

int timer_init(void)
{
	struct s5p_timer *pwm = pwm_timer();
	u32 val;

	gd->arch.timer_rate_hz = get_pwm_clk() /
		(TQ210_PWM_PRESCALE * TQ210_PWM_DIV);
	if (!gd->arch.timer_rate_hz)
		return -1;

	/* Stop timer 4 before changing the shared prescaler and divider. */
	clrbits_le32(&pwm->tcon, TCON_START(4) | TCON_UPDATE(4) |
		     TCON4_AUTO_RELOAD);
	clrsetbits_le32(&pwm->tcfg0, 0xffU << 8,
			(TQ210_PWM_PRESCALE - 1) << 8);
	clrsetbits_le32(&pwm->tcfg1, 0xfU << MUX_DIV_SHIFT(4),
			MUX_DIV_4 << MUX_DIV_SHIFT(4));

	/* Disable IRQ4; clear its pending flag, preserving IRQ0-3 enables. */
	writel((readl(&pwm->tintcstat) & 0xfU) | (1U << 9),
	       &pwm->tintcstat);
	writel(0xffffffffU, &pwm->tcntb4);
	val = readl(&pwm->tcon);
	writel(val | TCON_UPDATE(4) | TCON4_AUTO_RELOAD, &pwm->tcon);
	writel(val | TCON4_AUTO_RELOAD | TCON_START(4), &pwm->tcon);

	gd->arch.lastinc = readl(&pwm->tcnto4);
	gd->arch.timer_reset_value = 0;
	return 0;
}

ulong notrace get_tbclk(void)
{
	return gd->arch.timer_rate_hz;
}

uint64_t notrace get_ticks(void)
{
	u32 now = readl(&pwm_timer()->tcnto4);

	/* Unsigned subtraction handles one 32-bit hardware wrap. */
	gd->arch.timer_reset_value += (u32)(gd->arch.lastinc - now);
	gd->arch.lastinc = now;
	return gd->arch.timer_reset_value;
}

uint64_t get_timer_us(uint64_t base)
{
	uint64_t ticks = get_ticks();

	ticks *= 1000000ULL;
	do_div(ticks, get_tbclk());
	return ticks - base;
}

ulong notrace timer_get_us(void)
{
	return get_timer_us(0);
}

/* get_timer() and __udelay() use the generic lib/time.c implementations. */
