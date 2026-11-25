#include "openboot/platform.h"

#if !defined(SRAM_ONLY) && !defined(USB_HANDOFF_TEST)
static void diagnostic_console(void)
{
#ifdef USB_ENUM_TEST
    console_write("USB enumeration experiment: lowercase e takes ownership; q disconnects.\r\n");
#endif
#ifdef DDR_FULL_TEST
    console_write("Full memory test available: uppercase T destroys all DDR contents.\r\n");
#endif

    for (;;) {
        if (!(MMIO32(S5PV210_UART0_BASE + UART_UTRSTAT) & 1u)) {
            continue;
        }

        unsigned command = MMIO8(S5PV210_UART0_BASE + UART_URXH);

        if (command == 'a') {
            alias_probe(DDR0_SCRATCH);
        } else if (command == 'b') {
            alias_probe(DDR1_SCRATCH);
        } else if (command == 'm') {
            map_probe();
        }
#ifdef USB_ENUM_TEST
        else if (command == 'e') {
            usb_enum_test();
        }
#endif
#ifdef USB_STATE_PROBE
        else if (command == 'u') {
            usb_state();
        }
#endif
#ifdef DDR_FULL_TEST
        else if (command == 'T') {
            full_ddr_test();
        }
#endif
    }
}
#endif

void boot_main(void)
{
    watchdog_disable();
    early_console();

#ifdef ROM_CLOCKS
    console_write("\r\nTQ210 OpenBoot v0.3 ROM CLOCKS\r\n[0] SRAM C ENTRY OK\r\n");
#else
    console_write("\r\nTQ210 OpenBoot v0.1 EXPERIMENTAL\r\n[0] SRAM ENTRY OK\r\n");
#endif

#ifdef USB_HANDOFF_TEST
    handoff_guard();
#endif

    if (!clock_init()) {
        console_write("CLOCK FAIL\r\n");
        boot_halt();
    }

#ifdef SRAM_ONLY
    console_write("SRAM TEST PASS; DDR not touched\r\n");

    for (;;) {
        if (MMIO32(S5PV210_UART0_BASE + UART_UTRSTAT) & 1u) {
            (void)MMIO8(S5PV210_UART0_BASE + UART_URXH);
            console_write("SRAM TEST PASS; DDR not touched\r\n");
        }
    }
#else
#ifdef DMC0_ROW14
    console_write("OpenBoot v0.4 A/B: DMC0 geometry 0x1323\r\n");
#endif
#ifdef USB_STATE_PROBE
    usb_state();
#endif

    drive_strength();

    if (!dmc_init(S5PV210_DMC0_BASE, 0)) {
        console_write("DMC0 FAIL\r\n");
        boot_halt();
    }

    if (!dmc_init(S5PV210_DMC1_BASE, 1)) {
        console_write("DMC1 FAIL\r\n");
        boot_halt();
    }

    if (!ram_test()) {
        boot_halt();
    }

    if (!ddr_exec()) {
        console_write("DDR EXEC FAIL\r\n");
        boot_halt();
    }

    console_write("[5] DDR EXEC PASS; back in SRAM\r\n");

#ifdef USB_HANDOFF_TEST
    handoff_test();
#else
    console_write("OpenBoot diagnostic complete. a=optional DMC0 alias probe, b=DMC1, m=dual-window map test.\r\n");
#ifdef USB_STATE_PROBE
    console_write("USB state probe available: lowercase u repeats snapshot.\r\n");
#endif
#ifdef USB_AUTO_BOOT
    console_write("OpenBoot Auto Boot: starting USB takeover automatically.\r\n");
#ifdef USB_FAST_BOOT
    usb_delay_iterations(4000000u);
#else
    usb_settle();
#endif
    usb_enum_test();
#endif
    diagnostic_console();
#endif
#endif

    boot_halt();
}
