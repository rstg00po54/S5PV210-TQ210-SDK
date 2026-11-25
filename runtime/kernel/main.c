#include "memory.h"

void runtime_main(void)
{
    console_write("\r\nTQ210 Runtime v0.5\r\n");
    console_write("[0] DDR ENTRY / private stacks / BSS OK\r\n");
    cpu_info();

    /* Fail early on an unexpected memory controller configuration. */
    if (MMIO32(0xf0000008u) != 0x20e01323u ||
        MMIO32(0xf1400008u) != 0x40e01323u) {
        console_write("Unexpected DDR geometry; stopping.\r\n");
        runtime_halt();
    }
    page_init();
    console_write("[1] Page allocator ready\r\n");
    memory_info();
    if (memory_selftest()) {
        console_write("Initial memory test FAIL\r\n");
        runtime_halt();
    }
    console_write("[2] Initial memory test PASS\r\n");
#ifndef RUNTIME_SAFE
    console_write("[3] Enabling MMU...\r\n");
    if (mmu_enable()) {
        console_write("MMU FAIL\r\n");
        runtime_halt();
    }
    console_write("[3] MMU ON\r\n[4] Enabling D-Cache...\r\n");
    if (cache_enable()) {
        console_write("CACHE FAIL\r\n");
        runtime_halt();
    }
    console_write("[4] D-Cache ON\r\n");
    if (memory_selftest()) {
        console_write("Cached memory test FAIL\r\n");
        runtime_halt();
    }
    console_write("[5] Cached memory test PASS\r\n");
#else
    console_write("SAFE MODE: MMU/D-Cache remain OFF; run autotest for staged validation.\r\n");
#endif
    mmu_info();
    shell_run();
}
