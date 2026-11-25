#ifndef RUNTIME_PLATFORM_H
#define RUNTIME_PLATFORM_H

#include <stdint.h>
#include <stddef.h>

#define DDR_BASE       0x20000000u
#define DDR_END        0x60000000u
#define IMAGE_BASE     0x24000000u
#define IMAGE_END      0x25000000u
#define PAGE_SIZE      4096u
#define UART0_BASE     0xe2900000u
#define CLOCK_BASE     0xe0100000u

#define MMIO32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define MMIO8(a)  (*(volatile uint8_t *)(uintptr_t)(a))

static inline void dsb(void) { __asm__ volatile("dsb sy" ::: "memory"); }
static inline void isb(void) { __asm__ volatile("isb" ::: "memory"); }
static inline uint32_t cpu_sctlr(void)
{
    uint32_t v;
    __asm__ volatile("mrc p15,0,%0,c1,c0,0" : "=r"(v));
    return v;
}
static inline uint32_t cpu_midr(void)
{
    uint32_t v;
    __asm__ volatile("mrc p15,0,%0,c0,c0,0" : "=r"(v));
    return v;
}

void uart_putc(char c);
int uart_getc(void);
void console_write(const char *s);
void console_hex(uint32_t n);
void console_dec(uint32_t n);
void console_reg(const char *s, uint32_t n);
void cpu_info(void);
void runtime_main(void) __attribute__((noreturn));
void runtime_fault(uint32_t type, uint32_t pc, uint32_t spsr) __attribute__((noreturn));
void runtime_halt(void) __attribute__((noreturn));

#endif
