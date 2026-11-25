#include "openboot/platform.h"
static unsigned console_ready;
static void tx(char c)
{
    if (mmio_wait_mask(S5PV210_UART0_BASE + UART_UTRSTAT, 2, 2)) MMIO8(S5PV210_UART0_BASE + UART_UTXH) =(uint8_t)c;
}
void console_write(const char *s)
{
    while (*s) tx(*s++);
}
void console_hex(uint32_t v)
{
    static const char d[] = "0123456789ABCDEF";
    console_write("0x");
    for (int i = 28; i >= 0; i -= 4) tx(d[(v>>i)&15]);
}
void console_reg(const char *s, uint32_t v)
{
    console_write(s);
    console_hex(v);
    console_write("\r\n");
}
void boot_fault(void)
{
    if (console_ready)
    {
        uint32_t a, s;
        __asm__ volatile("mrc p15,0,%0,c6,c0,0":"=r"(a));
        __asm__ volatile("mrc p15,0,%0,c5,c0,0":"=r"(s));
        console_write("EXCEPTION (DFAR/DFSR meaningful for data abort)\r\n");
        console_reg("DFAR=", a);
        console_reg("DFSR=", s);
    }
    boot_halt();
}
void uart_init(unsigned divisor, unsigned slot)
{
    mmio_write32(S5PV210_CLK_BASE + CLK_GATE_IP3, MMIO32(S5PV210_CLK_BASE + CLK_GATE_IP3)|0x420000u);
    mmio_write32(S5PV210_GPIO_BASE, (MMIO32(S5PV210_GPIO_BASE)&~255u)|0x22u);
    mmio_write32(S5PV210_UART0_BASE + UART_UCON, 0);
    mmio_write32(S5PV210_UART0_BASE + UART_UFCON, 0);
    mmio_write32(S5PV210_UART0_BASE + UART_UMCON, 0);
    mmio_write32(S5PV210_UART0_BASE, 3);
    mmio_write32(S5PV210_UART0_BASE + UART_UBRDIV, divisor);
    mmio_write32(S5PV210_UART0_BASE + UART_UDIVSLOT, slot);
    mmio_write32(S5PV210_UART0_BASE + UART_UCON, 5);
    console_ready = 1;
}
