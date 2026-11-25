#include "openboot/platform.h"
#ifndef SRAM_ONLY
static uint32_t pattern(unsigned i, unsigned bank, unsigned pass)
{
    return (0x55aa0000u ^ i ^ (bank ? 0x12340000u:0)) ^ (pass ? 0xffffffffu:0);
}
int ram_test(void)
{
    volatile uint32_t *const banks[2] = {
        (volatile uint32_t *) DDR0_SCRATCH,
        (volatile uint32_t *) DDR1_SCRATCH
    };
    console_write("[4] DDR scratch: 16 KiB per bank, two patterns\r\n");
    for (unsigned pass = 0; pass < 2; pass++)
    {
        for (unsigned bank = 0; bank < 2; bank++)
        {
            const uint32_t base = bank ? DDR1_SCRATCH : DDR0_SCRATCH;
            console_reg("WRITE ", base);
            for (unsigned i = 0; i < 4096; i++)
            {
                banks[bank][i] = pattern(i, bank, pass);
            }
        }
        cpu_sync();
        for (unsigned bank = 0; bank < 2; bank++)
        {
            const uint32_t base = bank ? DDR1_SCRATCH : DDR0_SCRATCH;
            console_reg("READ ", base);
            for (unsigned i = 0; i < 4096; i++)
            {
                const uint32_t expected = pattern(i, bank, pass);
                if (banks[bank][i] != expected)
                {
                    console_reg("FAIL AT ", base + 4 * i);
                    console_reg("EXPECTED ", expected);
                    console_reg("ACTUAL ", banks[bank][i]);
                    return 0;
                }
            }
        }
    }
    console_write("[4] DDR SCRATCH PASS (not capacity verification)\r\n");
    return 1;
}
extern const uint32_t ddr_stub_begin[], ddr_stub_end[];
int ddr_exec(void)
{
    volatile uint32_t *dst = (volatile uint32_t *) DDR0_SCRATCH;
    size_t n = ((uintptr_t) ddr_stub_end-(uintptr_t) ddr_stub_begin)/4;
    for (size_t i = 0; i < n; i++)
    {
        dst[i] = ddr_stub_begin[i];
    }
    cpu_sync();
    uint32_t zero = 0;
    __asm__ volatile("mcr p15,0,%0,c7,c5,0; dsb sy; isb"::"r"(zero):"memory");
    uint32_t (*f)(void) = (uint32_t (*)(void))(uintptr_t) DDR0_SCRATCH;
    uint32_t result = f();
    console_reg("[5] DDR execution result=", result);
    return result == 0x5a5a2100u;
}
/* Optional destructive (RAM only) sample probe. Keep CPU/stack in SRAM. A bus
   hang can require power-cycle, so this is never run automatically. */
void alias_probe(uint32_t base)
{
    uint32_t original[6];
    static const uint32_t off[] = {0, 0x01000000, 0x02000000, 0x04000000, 0x08000000, 0x10000000};
    console_write("Alias sample: NOT a full capacity test\r\n");
    for (unsigned i = 0; i < 6; i++)
    {
        console_reg("PROBE ", base+off[i]);
        original[i] = MMIO32(base+off[i]);
        mmio_write32(base+off[i], 0xa11a0000u+i);
    }
    for (unsigned i = 0; i < 6; i++)
    {
        console_reg("ADDR ", base+off[i]);
        console_reg("VALUE ", MMIO32(base+off[i]));
    }
    /* Restore in reverse order, including when two addresses alias. */
    for (unsigned i = 6; i > 0; i--)
    {
        mmio_write32(base+off[i-1], original[i-1]);
    }
    console_write("Sample complete; inspect values for aliases\r\n");
}
/* Two phases over BOTH 512 MiB windows; samples each 16 MiB block at
   +64 KiB and its last word, plus lower address bits. Never test outside
   configured apertures. Standalone RAM only: invalid addresses can hang.
   Save all originals before any writes; restore after both verification passes. */
#define NS_PER_BANK 86u
#define NS_ALL (2u*NS_PER_BANK)
/* i is always in [0, NS_ALL). Avoid software division in freestanding ARM. */
static unsigned sample_index(unsigned i)
{
    return i >= NS_PER_BANK ? i-NS_PER_BANK : i;
}
static uint32_t sample_addr(unsigned i)
{
    uint32_t base = i >= NS_PER_BANK ? DDR1_BASE:DDR0_BASE;
    unsigned j = sample_index(i);
    if (j < 64u)
    {
        uint32_t block = (j >> 1) << 24;
        return base+block+((j&1u) ? 0x00fffffcu:0x10000u);
    }
    /* Walking bits 2..23 from base; avoids duplicate +64 KiB sample. */
    unsigned bit = j-64u+2u;
    return base+(bit == 16u ? 0u:(1u << bit));
}
void map_probe(void)
{
    uint32_t saved[NS_ALL];
    unsigned errors[2] = {0, 0};
    console_write("MAP TEST: 512 MiB windows x2, sparse samples; not full stress\r\n");
    console_reg("DMC0 MEMCONFIG0=", MMIO32(0xf0000008u));
    console_reg("DMC1 MEMCONFIG0=", MMIO32(0xf1400008u));
    for (unsigned i = 0; i < NS_ALL; i++)
    {
        uint32_t a = sample_addr(i);
        unsigned j = sample_index(i);
        if (j < 64u && !(j&1u))
        {
            console_reg("PROBE BLOCK=", a);
        }
        saved[i] = MMIO32(a);
    }
    for (unsigned pass = 0; pass < 2; pass++)
    {
        for (unsigned k = 0; k < NS_ALL; k++)
        {
            unsigned i = pass ? NS_ALL-1u-k:k;
            uint32_t a = sample_addr(i);
            mmio_write32(a, a ^ (pass ? 0x5aa5a55au:0xa55a5aa5u));
        }
        cpu_sync();
        for (unsigned i = 0; i < NS_ALL; i++)
        {
            uint32_t a = sample_addr(i), expected = a ^ (pass ? 0x5aa5a55au:0xa55a5aa5u), actual = MMIO32(a);
            if (actual != expected)
            {
                unsigned bank = i >= NS_PER_BANK;
                if (errors[bank] < 12u)
                {
                    console_reg("MISMATCH ADDR=", a);
                    console_reg("EXPECTED=", expected);
                    console_reg("ACTUAL=", actual);
                }
                errors[bank]++;
            }
        }
    }
    for (unsigned i = 0; i < NS_ALL; i++)
    {
        mmio_write32(sample_addr(i), saved[i]);
    }
    console_reg("DMC0 MAP ERRORS=", errors[0]);
    console_reg("DMC1 MAP ERRORS=", errors[1]);
    if (errors[0] || errors[1])
    {
        console_write("MAP SAMPLE FAIL\r\n");
    }
    else
    {
        console_write("MAP SAMPLE PASS: no alias in sampled addresses; capacity/stress unconfirmed\r\n");
    }
}
#ifdef DDR_FULL_TEST
/* Destructive, explicitly requested standalone RAM test. Code, data and
 * stack remain in iRAM. Never run with an OS or valuable DDR data present. */
static uint32_t full_pattern(uint32_t addr, unsigned pass)
{
    uint32_t v = (addr >> 2) ^ 0x55aa33ccu;
    return pass ? ~v : v;
}
void full_ddr_test(void)
{
    uint32_t ctl;
    __asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(ctl));
    if ((ctl&5u) || MMIO32(0xf0000008u) != 0x20e01323u || MMIO32(0xf1400008u) != 0x40e01323u)
    {
        console_write("FULL TEST BLOCKED: unexpected cache/MMU or DMC configuration\r\n");
        return;
    }
    console_write("FULL DDR TEST: destructive, 0x20000000..0x5FFFFFFF\r\n");
    uint32_t errors[2] = {0, 0};
    for (unsigned pass = 0; pass < 2; pass++)
    {
        console_reg("PASS NUMBER=", pass+1u);
        /* Both banks must be completely written before either is verified. */
        for (uint32_t block = DDR0_BASE; block < DDR_END; block += 0x02000000u)
        {
            console_reg("WRITE BLOCK=", block);
            for (uint32_t a = block; a < block+0x02000000u; a += 4u)
            {
                MMIO32(a) = full_pattern(a, pass);
            }
            cpu_sync();
        }
        for (uint32_t block = DDR0_BASE; block < DDR_END; block += 0x02000000u)
        {
            console_reg("VERIFY BLOCK=", block);
            for (uint32_t a = block; a < block+0x02000000u; a += 4u)
            {
                uint32_t got = MMIO32(a), expected = full_pattern(a, pass);
                if (got != expected)
                {
                    unsigned bank = a >= DDR1_BASE;
                    if (errors[bank] < 8u)
                    {
                        console_reg("FAIL AT=", a);
                        console_reg("EXPECTED=", expected);
                        console_reg("ACTUAL=", got);
                    }
                    errors[bank]++;
                }
            }
        }
        console_reg("DMC0 CUMULATIVE ERRORS=", errors[0]);
        console_reg("DMC1 CUMULATIVE ERRORS=", errors[1]);
    }
    if (errors[0] || errors[1])
    {
        console_write("FULL DDR TEST FAIL\r\n");
    }
    else
    {
        console_write("FULL DDR TEST PASS: 1024 MiB, two patterns, every 32-bit word\r\n");
    }
    console_write("DDR contents overwritten; iRAM console still running\r\n");
}
#endif
#endif
