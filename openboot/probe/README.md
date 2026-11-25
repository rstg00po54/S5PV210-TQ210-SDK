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
