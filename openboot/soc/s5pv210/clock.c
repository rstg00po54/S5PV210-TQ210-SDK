#include "openboot/platform.h"
#ifdef ROM_CLOCKS
/* Verified cold iROM console, no PLL/mux/divider writes. */
void early_console(void)
{
    uart_init(35, 0);
}
int clock_init(void)
{
    console_write("[1] KEEP ROM CLOCKS\r\n");
    console_reg("APLL=", MMIO32(S5PV210_CLK_BASE + CLK_APLL_CON0));
    console_reg("MPLL=", MMIO32(S5PV210_CLK_BASE + CLK_MPLL_CON));
    console_reg("CLK_SRC0=", MMIO32(S5PV210_CLK_BASE + CLK_SRC0));
    console_reg("CLK_DIV0=", MMIO32(S5PV210_CLK_BASE + CLK_DIV0));
    console_reg("CLK_SRC6=", MMIO32(S5PV210_CLK_BASE + CLK_SRC6));
    console_reg("CLK_DIV6=", MMIO32(S5PV210_CLK_BASE + CLK_DIV6));
    if (MMIO32(S5PV210_CLK_BASE + CLK_APLL_CON0) != 0xa0c80601u || MMIO32(S5PV210_CLK_BASE + CLK_MPLL_CON) != 0xa29b0c01u ||
    MMIO32(S5PV210_CLK_BASE + CLK_SRC0) != 0x111u || MMIO32(S5PV210_CLK_BASE + CLK_DIV0) != 0x14141251u ||
    MMIO32(S5PV210_CLK_BASE + CLK_SRC6) != 0 || MMIO32(S5PV210_CLK_BASE + CLK_DIV6) != 0x00071000u)
    {
        console_write("Unexpected ROM clock state; DDR init blocked\r\n");
        return 0;
    }
    console_write("[1] ROM CLOCK CHECK PASS\r\n");
    return 1;
}
#else
/* Before changing clocks, select oscillator and use a known 24 MHz UART clock
   (PCLK_PSYS = FIN when divider is 1). This depends on the board's 24 MHz crystal. */
void early_console(void)
{
    /* Establish known conservative dividers before bypassing PLLs. */
    mmio_write32(S5PV210_CLK_BASE + CLK_DIV0, 0x14131440);
    if (!mmio_wait_mask(S5PV210_CLK_BASE + CLK_DIV_STAT0, 0xffu, 0)) boot_halt();
    mmio_write32(S5PV210_CLK_BASE + CLK_SRC0, 0);
    for (volatile unsigned i = 0; i<10000; i++) __asm__ volatile("nop");
    mmio_write32(S5PV210_CLK_BASE + CLK_DIV0, 0);
    if (!mmio_wait_mask(S5PV210_CLK_BASE + CLK_DIV_STAT0, 0xffu, 0)) boot_halt();
    /* 24 MHz / (16 * 13) ~115384 baud. */
    uart_init(12, 0);
}
static int pll(uint32_t off, uint32_t val)
{
    mmio_write32(S5PV210_CLK_BASE+off, val);
    return mmio_wait_mask(S5PV210_CLK_BASE+off, 1u<<29, 1u<<29);
}
int clock_init(void)
{
    console_write("[1] PLL setup\r\n");
    mmio_write32(S5PV210_CLK_BASE+0x00, 0xffff);
    mmio_write32(S5PV210_CLK_BASE+0x08, 0xffff);
    mmio_write32(S5PV210_CLK_BASE+0x10, 0xffff);
    /* CLK_SRC0 already bypasses PLLs. Program A/M/E; VPLL is not selected
       in the verified 0x111 mux snapshot, so leave it alone. */
    if (!pll(0x100, 0x80fa0601))
    {
        console_write("APLL LOCK TIMEOUT\r\n");
        return 0;
    }
    if (!pll(0x108, 0x829b0c01))
    {
        console_write("MPLL LOCK TIMEOUT\r\n");
        return 0;
    }
    if (!pll(0x110, 0x80600602))
    {
        console_write("EPLL LOCK TIMEOUT\r\n");
        return 0;
    }
    /* Avoid logging during divider/mux changes: UART timing changes too. */
    (void)mmio_wait_mask(S5PV210_UART0_BASE + UART_UTRSTAT, 4, 4);
    mmio_write32(S5PV210_CLK_BASE + CLK_DIV0, 0x14131440);
    if (!mmio_wait_mask(S5PV210_CLK_BASE + CLK_DIV_STAT0, 0xffu, 0)) return 0;
    mmio_write32(S5PV210_CLK_BASE + CLK_SRC6, 0);
    mmio_write32(S5PV210_CLK_BASE + CLK_DIV6, 0);
    mmio_write32(S5PV210_CLK_BASE + CLK_SRC0, 0x111);
    for (volatile unsigned i = 0; i<10000; i++) __asm__ volatile("nop");
    /* Known-working UART0 setup from vendor-initialized CoreB probe. */
    uart_init(35, 1);
    console_reg("APLL=", MMIO32(S5PV210_CLK_BASE + CLK_APLL_CON0));
    console_reg("MPLL=", MMIO32(S5PV210_CLK_BASE + CLK_MPLL_CON));
    console_reg("CLK_SRC0=", MMIO32(S5PV210_CLK_BASE + CLK_SRC0));
    console_reg("CLK_DIV0=", MMIO32(S5PV210_CLK_BASE + CLK_DIV0));
    console_write("[1] CLOCK OK\r\n");
    return 1;
}
#endif
