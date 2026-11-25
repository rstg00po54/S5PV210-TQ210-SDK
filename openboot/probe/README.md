# Cold USB entry probes

Use these if OpenBoot v0.1 transfers successfully but emits no UART output. Neither image uses a stack, CP15 initialization, DDR, PLL changes, or flash. Both retain the ROM clock tree, enable the watchdog and UART register clocks, disable the watchdog and mask IRQ/FIQ. Entry is 0xD0020010, with the original BL2's 0x20 initial branch layout.

From openboot:

    sh probe/build.sh
    sudo ./build-host/tq210-dnw out/entry_uart.bin --pad-vendor-size

Start UART0 capture first (115200 8N1). Cold power-cycle into ROM USB mode before each download. The UART probe continuously cycles candidate divisors using the inherited PCLK and emits OPENBOOT ENTRY UART0 DIV=0xNN. Garbage at some divisors is expected. Record any readable message or any activity. Absence of readable messages does not prove failure to execute.

If silent, cold power-cycle again and run:

    sudo ./build-host/tq210-dnw out/entry_gpio.bin --pad-vendor-size

Observe UART0 TX (GPA0[1], at the board TX output, common ground) with an oscilloscope. It should repeatedly toggle as a GPIO, not transmit UART data. Do not connect an actively driven external output to TX. A square wave establishes code execution without relying on UART baud rate. No waveform is inconclusive: ROM entry, early peripheral access and physical signal path still require isolation.

Both binaries were assembled and linked locally using clang/LLD (148 and 388 bytes respectively). No board verification. Existing SRAM/DDR images are unchanged. For any failure, send full downloader log, serial capture and waveform observations. Use cold power cycles between experiments; do not use these probes as stage2 or program NAND.

## Hardware result and next test

On 2026-11-25 the user observed repeated readable `OPENBOOT ENTRY UART0 DIV=0x23` messages at 115200 baud, interleaved with expected garbage from divisor cycling. With the cold-ROM test procedure this establishes independent RAM code execution without vendor BL2. Exact PCLK and boot-time PLL settings are not yet measured.

Build and upload the new fixed-divisor, stackless state probe after a cold power cycle:

    sh probe/build.sh
    sudo ./build-host/tq210-dnw out/rom_state.bin --pad-vendor-size

Capture the entire log starting from ENTRY_CPSR and ENTRY_SP, through CP15 and CLK registers. Send any byte to repeat the CP15/clock snapshot (initial CPSR/SP are printed only once). UART divisor 35, slot 0. No PLL/divider/mux writes, no DDR access, no flash. UART pinmux/gates, watchdog and interrupt mask are configured as in the successful entry probe. If a register read fails, the last printed label can identify it. The new probe compiled locally with clang/LLD (980 bytes, entry 0xd0020010, initial branch to +0x20); board validation pending. Do not run the original DDR initialization until ROM state is reviewed.
