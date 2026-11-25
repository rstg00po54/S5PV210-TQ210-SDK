#include "platform.h"

/* Keep the verified OpenBoot UART configuration (115200 8N1). */
void uart_putc(char c)
{
    for (unsigned n = 0; n < 2000000u; ++n) {
        if (MMIO32(UART0_BASE + 0x10u) & 2u) {
            MMIO8(UART0_BASE + 0x20u) = (uint8_t)c;
            return;
        }
    }
}
int uart_getc(void)
{
    while (!(MMIO32(UART0_BASE + 0x10u) & 1u)) {
        __asm__ volatile("nop");
    }
    return MMIO8(UART0_BASE + 0x24u);
}
void console_write(const char *s)
{
    while (*s) {
        uart_putc(*s++);
    }
}
void console_hex(uint32_t n)
{
    static const char digits[] = "0123456789ABCDEF";
    console_write("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        uart_putc(digits[(n >> shift) & 15u]);
    }
}
/* Decimal output without a freestanding division runtime. */
void console_dec(uint32_t n)
{
    static const uint32_t places[] = {
        1000000000u,100000000u,10000000u,1000000u,100000u,
        10000u,1000u,100u,10u,1u
    };
    int started = 0;
    for (unsigned i = 0; i < 10; ++i) {
        unsigned d = 0;
        while (n >= places[i]) {
            n -= places[i];
            ++d;
        }
        if (d || started || i == 9) {
            uart_putc((char)('0' + d));
            started = 1;
        }
    }
}
void console_reg(const char *s, uint32_t n)
{
    console_write(s);
    console_hex(n);
    console_write("\r\n");
}
