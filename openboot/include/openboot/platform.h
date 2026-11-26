#ifndef OPENBOOT_PLATFORM_H
#define OPENBOOT_PLATFORM_H

#include <stdint.h>
#include <stddef.h>
#include "openboot/registers.h"

#define MMIO32(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define MMIO8(address)  (*(volatile uint8_t *)(uintptr_t)(address))
#define MMIO_TIMEOUT 2000000u

static inline void mmio_write32(uint32_t address, uint32_t value)
{
    MMIO32(address) = value;
    __asm__ volatile("dsb sy" ::: "memory");
}

static inline int mmio_wait_mask(uint32_t address, uint32_t mask, uint32_t expected)
{
    for (unsigned i = 0; i < MMIO_TIMEOUT; i++) {
        if ((MMIO32(address) & mask) == expected) {
            return 1;
        }
    }
    return 0;
}

static inline void cpu_sync(void)
{
    __asm__ volatile("dsb sy; isb" ::: "memory");
}

static inline __attribute__((noreturn)) void boot_halt(void)
{
    for (;;) {
        __asm__ volatile("nop");
    }
}

void uart_init(unsigned divisor, unsigned slot);
void console_write(const char *message);
void console_hex(uint32_t value);
void console_reg(const char *label, uint32_t value);
void boot_fault(void);
void early_console(void);
int clock_init(void);
void watchdog_disable(void);
void drive_strength(void);
int dmc_init(uint32_t base, int bank);
int ram_test(void);
int ddr_exec(void);
void alias_probe(uint32_t base);
void map_probe(void);
void full_ddr_test(void);
void usb_state(void);
void usb_enum_test(void);
void usb_delay_iterations(uint32_t count);
void usb_settle(void);
void handoff_guard(void);
__attribute__((noreturn)) void handoff_test(void);

void nand_boot(void);

#endif
