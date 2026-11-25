#include "openboot/platform.h"
#ifndef SRAM_ONLY
#ifdef ROM_CLOCKS
#define DDR_REFRESH 0x400u
#else
#define DDR_REFRESH 0x618u
#endif
static const uint32_t directcmd[12] = {0x07000000, 0x01000000, 0x00020000, 0x00030000, 0x00010400, 0x00000542, 0x01000000, 0x05000000, 0x05000000, 0x00000442, 0x00010780, 0x00010400};
static void pause(void)
{
    for (volatile unsigned i = 0; i < 1000; i++)
    {
        __asm__ volatile("nop");
    }
}
int dmc_init(uint32_t base, int bank)
{
    console_write(bank ? "[3] DMC1 PHY\r\n":"[2] DMC0 PHY\r\n");
    console_reg("BEFORE CONCONTROL=", MMIO32(base));
    console_reg("BEFORE MEMCONTROL=", MMIO32(base + DMC_MEMCONTROL));
    console_reg("BEFORE PHY=", MMIO32(base + DMC_PHYSTATUS));
    mmio_write32(base + DMC_PHYCONTROL0, 0x00101008);
    mmio_write32(base + DMC_PHYCONTROL1, 0x46);
    mmio_write32(base + DMC_PHYCONTROL0, 0x0010100a);
    mmio_write32(base + DMC_PHYCONTROL0, 0x0010100b);
    /* Match vendor DLL poll/force sequence, but with bounded wait. */
    if (!mmio_wait_mask(base + DMC_PHYSTATUS, 3, 0))
    {
        console_reg("PHYSTATUS TIMEOUT=", MMIO32(base + DMC_PHYSTATUS));
        return 0;
    }
    console_reg("PHY POLL DONE=", MMIO32(base + DMC_PHYSTATUS));
    uint32_t v = MMIO32(base + DMC_PHYCONTROL0) | 0x6a000000;
    mmio_write32(base + DMC_PHYCONTROL0, v);
    mmio_write32(base + DMC_PHYCONTROL0, v&~2u);
    mmio_write32(base + DMC_CONCONTROL, 0x0fff1010);
    mmio_write32(base + DMC_MEMCONTROL, 0x002024c0);
#ifdef DMC0_ROW14
    /* A/B experiment: change only DMC0 chip geometry to match DMC1's row field. */
    mmio_write32(base + DMC_MEMCONFIG0, bank ? 0x40e01323:0x20e01323);
    mmio_write32(base + DMC_MEMCONFIG1, bank ? 0xffe01323:0x1ee01343);
#else
    mmio_write32(base + DMC_MEMCONFIG0, bank ? 0x40e01323:0x20e01343);
    mmio_write32(base + DMC_MEMCONFIG1, bank ? 0xffe01323:0x1ee01343);
#endif
    mmio_write32(base + DMC_PRECHCONFIG, 0xff000000);
    mmio_write32(base + DMC_PWRDNCONFIG, 0xffff00ff);
    mmio_write32(base + DMC_TIMINGAREF, DDR_REFRESH);
    mmio_write32(base + DMC_TIMINGROW, 0x27233309);
    mmio_write32(base + DMC_TIMINGDATA, bank ? 0x23240303:0x23240304);
    mmio_write32(base + DMC_TIMINGPOWER, 0x08c80232);
    for (unsigned i = 0; i < 16; i++)
    {
        mmio_write32(base + DMC_QOSCONTROL0+8*i, 0x00ff0000);
    }
    for (unsigned i = 0; i < 16; i++)
    {
        mmio_write32(base + DMC_QOSCONFIG0+8*i, 0);
    }
    console_write(bank ? "[3] DMC1 DDR2 commands\r\n":"[2] DMC0 DDR2 commands\r\n");
    /* Explicit gaps are conservative; original emitted consecutive writes. */
    for (unsigned cs = 0; cs < 2; cs++)
    {
        for (unsigned i = 0; i < 12; i++)
        {
            mmio_write32(base + DMC_DIRECTCMD, directcmd[i] | (cs << 20));
            pause();
        }
    }
    mmio_write32(base, MMIO32(base) | 0x20);
    mmio_write32(base + DMC_MEMCONTROL, (MMIO32(base + DMC_MEMCONTROL)&~0x3fu) | 0x13);
    console_reg("CONCONTROL=", MMIO32(base));
    console_reg("MEMCONTROL=", MMIO32(base + DMC_MEMCONTROL));
    console_reg("TIMINGAREF=", MMIO32(base + DMC_TIMINGAREF));
    console_reg("MEMCONFIG0=", MMIO32(base + DMC_MEMCONFIG0));
    console_reg("PHYSTATUS=", MMIO32(base + DMC_PHYSTATUS));
    return 1;
}
void drive_strength(void)
{
    for (uint32_t a = 0xe02003cc; a <= 0xe02005ec; a += 0x20)
    {
        mmio_write32(a, (a == 0xe02004cc || a == 0xe02005ec) ? 0x3fff:0xffff);
    }
}
#endif
