# OpenBoot modular source layout

Baseline: `79be964c325dd551d511faa57dd0ee2cf175850d`. This is a source refactor, not a new hardware feature.

| Area | Source |
| --- | --- |
| Startup and linker scripts | boot/start.S, boot/iram.ld, boot/ddr_app.* |
| Main orchestration | boot/main.c |
| Register definitions / MMIO | include/openboot/registers.h, platform.h |
| Clock / watchdog | soc/s5pv210/ |
| UART console | drivers/uart/console.c |
| DDR setup | drivers/ddr/ddr.c |
| Memory and USB tests | diagnostics/ |
| Legacy PIO / OBL1 | drivers/usb/usb_enum.c |
| ROM USB takeover and DMA | drivers/usb/usb_handoff.c |
| OBL2 image protocol and CRC | protocol/obl2.c |
| Native C++ CLI | tools/dnw.cpp |
| Host ROM framing, image and CRC | tools/image.cpp |
| Host libusb lifecycle and transport | tools/transport.cpp |
| Host legacy PIO protocol | tools/bulk.cpp |
| Host same-handle DMA protocol | tools/handoff.cpp |

The former .inc source files are replaced by independently compiled C files.
The existing sixteen firmware variants share boot/sources.txt and boot/variants.txt.
Register write order, timing constants, wire tokens, endpoint IDs, image address,
image size limits and CRC algorithms are intended to remain unchanged.
No NAND/SD I/O or new boot protocol is introduced.

Existing build interface:

    sh boot/build.sh
    cmake --build build-host
    ./build-host/tq210-dnw --selftest

Optional CMake firmware build (GNU ARM toolchain; output in build-firmware/out):

    cmake -S boot -B build-firmware -DCMAKE_TOOLCHAIN_FILE=../cmake/arm-none-eabi.cmake
    cmake --build build-firmware

Regression checklist (separate cold boot for each hardware test):

    sudo ./build-host/tq210-dnw --handoff-test out/rom_usb_handoff.bin
    sudo ./build-host/tq210-dnw --dma-test out/rom_usb_dma_boot.bin
    sudo ./build-host/tq210-dnw --dma-test out/rom_usb_dma_fast.bin
    sudo ./build-host/tq210-dnw --dma-boot out/rom_usb_dma_fast.bin out/ddr_app.bin

Expected CRC for 1 MiB+13: 0x6eefefc7. Execution UART: DDR APP EXEC PASS.
Existing linker assertions and ROM frame size checks remain.
New translation units can change inlining, timing and SRAM usage; the refactor
must not be considered hardware-equivalent until compiled and tested.

Reference baseline can be checked out separately:

    git worktree add --detach ../openboot-before-refactor 79be964c325dd551d511faa57dd0ee2cf175850d

The source split, lexical formatting and preprocessor nesting were checked.
No ARM/native build, CMake configure, or target regression was run in the
current environment because the remote repository cannot be cloned into it.


## Interface cleanup and build regression

The DMA protocol uses driver operations for OUT NAK release, receive-length checks, and endpoint diagnostics; OTG register definitions are private to the handoff driver. The main entry has explicit control blocks and a separate diagnostic console. Linker section garbage collection reduces unused code after splitting the translation units; startup KEEP entries and the existing iRAM size assertion are retained. Binary layout/timing may change.

Run `sh check-build.sh` for all firmware builds, C++ host build, host packet self-test and ROM frame size checks. The script does not access hardware. The new code and build script have not been compiled/executed in the current environment; hardware regression remains mandatory.
