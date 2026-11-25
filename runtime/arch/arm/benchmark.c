#include "memory.h"
#include "timebase.h"

#define PMU_CYCLE (1u << 31)
#define BENCH_PAGES 256u
#define BENCH_BYTES (BENCH_PAGES * PAGE_SIZE)

static uint32_t cycles(void)
{
    uint32_t v;
    dsb();
    isb();
    __asm__ volatile("mrc p15,0,%0,c9,c13,0" : "=r"(v));
    return v;
}

static void pmu_start(void)
{
    uint32_t v, cycle = PMU_CYCLE;
    __asm__ volatile("mrc p15,0,%0,c9,c12,0" : "=r"(v));
    __asm__ volatile("mcr p15,0,%0,c9,c14,2" :: "r"(cycle) : "memory");
    __asm__ volatile("mcr p15,0,%0,c9,c12,3" :: "r"(cycle) : "memory");
    v = (v & ~8u) | 5u; /* E=1, C=reset cycle counter, D=0 */
    __asm__ volatile("mcr p15,0,%0,c9,c12,0" :: "r"(v) : "memory");
    __asm__ volatile("mcr p15,0,%0,c9,c12,1" :: "r"(cycle) : "memory");
    isb();
}
static void pmu_stop(void)
{
    uint32_t cycle = PMU_CYCLE;
    __asm__ volatile("mcr p15,0,%0,c9,c12,2" :: "r"(cycle) : "memory");
    isb();
}

int benchmark_run(void)
{
    if (timer_running()) {
        console_write("Stop timer before benchmark.\r\n");
        return -1;
    }
    uintptr_t a = page_alloc(BENCH_PAGES);
    if (!a) {
        console_write("BENCH allocation FAIL\r\n");
        return -1;
    }
    pmu_start();
    uint32_t t = cycles();
    for (volatile unsigned i = 0; i < 1000u; ++i) {
        __asm__ volatile("nop");
    }
    if (cycles() == t) {
        pmu_stop();
        (void)page_free(a, BENCH_PAGES);
        console_write("PMU counter not advancing\r\n");
        return -1;
    }

    volatile uint32_t *p = (volatile uint32_t *)a;
    uint32_t w0, w1, r0, r1, hot0, hot1, sum = 0, errors = 0;
    cache_sync_range(a, BENCH_BYTES);
    w0 = cycles();
    for (unsigned i = 0; i < BENCH_BYTES / 4u; ++i) {
        p[i] = i ^ 0xa55aa55au;
    }
    cache_sync_range(a, BENCH_BYTES);
    w1 = cycles();
    r0 = cycles();
    for (unsigned i = 0; i < BENCH_BYTES / 4u; ++i) {
        uint32_t value = p[i];
        if (value != (i ^ 0xa55aa55au)) {
            ++errors;
        }
        sum ^= value;
    }
    r1 = cycles();
    hot0 = cycles();
    for (unsigned i = 0; i < 4096u; ++i) {
        sum ^= p[i];
    }
    hot1 = cycles();
    uint32_t overflow;
    __asm__ volatile("mrc p15,0,%0,c9,c12,3" : "=r"(overflow));
    pmu_stop();
    console_write("BENCH: 1 MiB sequential write + cache clean/invalidate, read + compare\r\n");
    console_write(cpu_sctlr() & 4u ? "D-Cache ON\r\n" : "D-Cache OFF\r\n");
    console_reg("BUFFER=", (uint32_t)a);
    console_reg("WRITE_CYCLES=", w1 - w0);
    console_reg("READ_VERIFY_CYCLES=", r1 - r0);
    console_reg("16K_READ_CYCLES=", hot1 - hot0);
    console_reg("CHECKSUM=", sum);
    console_reg("ERRORS=", errors);
    if (overflow & PMU_CYCLE) {
        console_write("PMU overflow: timing invalid\r\n");
    }
    if (page_free(a, BENCH_PAGES)) {
        console_write("BENCH free FAIL\r\n");
        return -1;
    }
    if (errors || (overflow & PMU_CYCLE)) {
        console_write("BENCH FAIL\r\n");
        return -1;
    }
    console_write("BENCH PASS\r\n");
    return 0;
}
