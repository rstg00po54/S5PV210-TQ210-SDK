#include "platform.h"

void cpu_info(void)
{
    uint32_t cpsr, vbar, clidr, ctr;
    __asm__ volatile("mrs %0,cpsr" : "=r"(cpsr));
    __asm__ volatile("mrc p15,0,%0,c12,c0,0" : "=r"(vbar));
    __asm__ volatile("mrc p15,1,%0,c0,c0,1" : "=r"(clidr));
    __asm__ volatile("mrc p15,0,%0,c0,c0,1" : "=r"(ctr));
    console_reg("MIDR=", cpu_midr());
    console_reg("CPSR=", cpsr);
    console_reg("SCTLR=", cpu_sctlr());
    console_reg("VBAR=", vbar);
    console_reg("CLIDR=", clidr);
    console_reg("CTR=", ctr);
    console_reg("APLL=", MMIO32(CLOCK_BASE + 0x100u));
    console_reg("MPLL=", MMIO32(CLOCK_BASE + 0x108u));
    console_reg("CLK_SRC0=", MMIO32(CLOCK_BASE + 0x200u));
    console_reg("CLK_DIV0=", MMIO32(CLOCK_BASE + 0x300u));
    if (MMIO32(CLOCK_BASE + 0x100u) == 0xa0c80601u &&
        MMIO32(CLOCK_BASE + 0x200u) == 0x111u &&
        MMIO32(CLOCK_BASE + 0x300u) == 0x14141251u) {
        console_write("ARMCLK: nominal 400 MHz (inherited ROM clock)\r\n");
    }
}
void runtime_halt(void)
{
    __asm__ volatile("cpsid if" ::: "memory");
    for (;;) {
        __asm__ volatile("wfi" ::: "memory");
    }
}
void runtime_fault(uint32_t type, uint32_t pc, uint32_t spsr)
{
    uint32_t dfsr, dfar, ifsr, ifar;
    __asm__ volatile("mrc p15,0,%0,c5,c0,0" : "=r"(dfsr));
    __asm__ volatile("mrc p15,0,%0,c6,c0,0" : "=r"(dfar));
    __asm__ volatile("mrc p15,0,%0,c5,c0,1" : "=r"(ifsr));
    __asm__ volatile("mrc p15,0,%0,c6,c0,2" : "=r"(ifar));
    console_write("\r\nFATAL ARM EXCEPTION\r\n");
    console_reg("TYPE=", type);
    console_reg("PC=", pc);
    console_reg("SPSR=", spsr);
    console_reg("SCTLR=", cpu_sctlr());
    if (type == 4u) {
        console_reg("DFSR=", dfsr);
        console_reg("DFAR=", dfar);
    } else if (type == 3u) {
        console_reg("IFSR=", ifsr);
        console_reg("IFAR=", ifar);
    }
    console_write("Halted; cold power-cycle to recover.\r\n");
    runtime_halt();
}
