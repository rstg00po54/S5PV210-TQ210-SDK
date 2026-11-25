#include "memory.h"
#include "timebase.h"
#include "selftest.h"

#define SCTLR_M 1u
#define SCTLR_C (1u << 2)

static unsigned passed;
static unsigned skipped;
static unsigned failed;

static int run_stage(const char *name, int (*test)(void))
{
    console_write("\r\n[AUTO] ");
    console_write(name);
    console_write("\r\n");
    if (test()) {
        ++failed;
        console_write("[AUTO] FAIL: ");
        console_write(name);
        console_write("\r\n");
        return -1;
    }
    ++passed;
    console_write("[AUTO] PASS: ");
    console_write(name);
    console_write("\r\n");
    return 0;
}

static void skip_stage(const char *name)
{
    ++skipped;
    console_write("[AUTO] SKIP: ");
    console_write(name);
    console_write("\r\n");
}

static void report(int ok)
{
    console_write("\r\n========== AUTOTEST SUMMARY ==========\r\n");
    console_write(ok ? "RESULT: PASS\r\n" : "RESULT: FAIL\r\n");
    console_write("Passed: ");
    console_dec(passed);
    console_write("  Skipped: ");
    console_dec(skipped);
    console_write("  Failed: ");
    console_dec(failed);
    console_write("\r\n======================================\r\n");
}

void runtime_autotest(void)
{
    passed = 0;
    skipped = 0;
    failed = 0;
    console_write("\r\n========== TQ210 AUTOMATIC TEST =========\r\n");

    if (timer_running()) {
        ++failed;
        console_write("[AUTO] FAIL: stop Timer0 before autotest\r\n");
        report(0);
        return;
    }

    uint32_t initial = cpu_sctlr();
    if (!(initial & SCTLR_M) && (initial & SCTLR_C)) {
        ++failed;
        console_write("[AUTO] FAIL: D-Cache is ON while MMU is OFF\r\n");
        report(0);
        return;
    }

    /* A safe-mode boot exercises all three cache/MMU states. */
    if (!(initial & SCTLR_M)) {
        if (run_stage("PMU baseline: MMU OFF / D-Cache OFF", benchmark_run)) {
            report(0);
            return;
        }
        if (run_stage("Enable MMU", mmu_enable)) {
            report(0);
            return;
        }
    } else {
        skip_stage("MMU-off baseline (MMU already ON at boot)");
        skip_stage("Enable MMU (already ON)");
    }

    if (!(cpu_sctlr() & SCTLR_C)) {
        if (run_stage("PMU: MMU ON / D-Cache OFF", benchmark_run)) {
            report(0);
            return;
        }
        if (run_stage("Enable D-Cache", cache_enable)) {
            report(0);
            return;
        }
    } else {
        skip_stage("MMU-only benchmark (D-Cache already ON)");
        skip_stage("Enable D-Cache (already ON)");
    }

    if (run_stage("PMU: MMU ON / D-Cache ON", benchmark_run)) {
        report(0);
        return;
    }
    if (run_stage("DDR page allocator / memory self-test", memory_selftest)) {
        report(0);
        return;
    }
    if (run_stage("PWM Timer0 polling probe (no IRQ enabled)", timer_probe)) {
        report(0);
        return;
    }

    report(failed == 0u);
}
