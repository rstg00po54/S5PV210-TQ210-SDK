# USB OTG bring-up — read-only iROM handoff probe

Next milestone: independently enumerate as a USB device, implement EP0 control transfers, bulk OUT into validated DDR, then a native C++ two-stage host upload. This commit is only the register survey, not an enumeration or download driver.

Sources for register layout:
- Linux S5PV210 device tree: https://code.googlesource.com/linux/torvalds/linux/+/c6e169bc146a76d5ccbf4d3825f705414352bd03/arch/arm/boot/dts/s5pv210.dtsi (OTG 0xec000000; PHY 0xec100000)
- Linux PHY driver: https://codebrowser.dev/linux/linux/drivers/phy/samsung/phy-s5pv210-usb2.c.html (device PHY isolation control 0xe010e80c bit 0)
- Linux clock driver: https://codebrowser.dev/linux/linux/drivers/clk/samsung/clk-s5pv210.c.html (CLK_GATE_IP1 0xe0100464 bit 16)
- DWC2 register definitions: https://codebrowser.dev/linux/linux/drivers/usb/dwc2/hw.h.html

Build from openboot:
    git pull
    sh boot/build.sh

Cold power-cycle into iROM USB mode, open UART0 at 115200 8N1, then:
    sudo ./build-host/tq210-dnw out/rom_usb_probe.bin --pad-vendor-size

Capture the entire USB OTG STATE section, plus DDR startup results. The probe runs before DDR initialization. After normal DDR scratch/exec PASS, lowercase u repeats the snapshot. On Ubuntu record lsusb before and after loading (no new enumeration is initiated by this firmware). If a register read stalls, last printed register label helps identify the block; cold power-cycle and use rom_ddr_full.bin to recover. If the gate is off or PHY isolated, probe stops without enabling them. If core ID is not the expected OTG signature or controller is in host mode, it avoids device-specific reads. No FIFO read/pop, core reset, interrupt acknowledgements, USB writes, or NAND/SD writes. USB state may change asynchronously. No claim that iROM USB remains enumerated. This new variant has been source-reviewed but not cross-compiled or hardware-tested yet.


## Observed iROM handoff, 2026-11-25

Two snapshots before and after DDR init agreed on GSNPSID=0x4F54281A (DWC OTG 2.81a), PHY_PWR=0x30, PHY_CLK=3, PHY_RST=0, PHY control=1, gate IP1=0xFFFFFFFF, device mode, GAHBCFG=0x27 (DMA enabled), GUSBCFG=0x540F, GRSTCTL=0x80000000, GHWCFG2=0x228FFC50, GHWCFG3=0x1E8084E8, GHWCFG4=0xFFF04030, RX FIFO 1024 words, nonperiodic TX FIFO 256 words, DCFG=0x40190, DOEPCTL2=0x000B8200 (bulk OUT, 512 byte max packet), DOEPDMA2=0xD0022BB4. Host lsusb still showed 04e8:1234. EP0 DMA addresses were not valid application buffers; do not send further USB traffic to the inherited device. The ROM enumeration remains visible, not evidence of our own driver.

## Experimental EP0-only enumeration

New rom_usb_enum.bin retains validated DDR settings. It starts with the normal UART console; send lowercase e once to disconnect the ROM device, reset the OTG core, switch to PIO/polling (DMA disabled), and reconnect with a private test VID:PID 1209:0001, manufacturer OpenBoot, product TQ210 OTG 1. This PID is not unique and is for private testing only, not distributed devices: https://pid.codes/1209/0001/. No bulk endpoints are advertised yet. A vendor-class interface with zero endpoints is intentional; this only tests device descriptors and standard EP0 requests. Driver is in boot/usb_enum.inc. DWC2 register/SET_ADDRESS behavior checked against Linux gadget.c and TinyUSB DWC2 slave-mode RX FIFO handling. Code is newly written; no target validation or cross-compilation performed for this commit.

    git pull
    sh boot/build.sh
    # cold power-cycle into iROM, open UART0
    sudo ./build-host/tq210-dnw out/rom_usb_enum.bin --pad-vendor-size

In Ubuntu another terminal can run sudo dmesg -w. After DDR EXEC PASS, send e via UART. Expect USB SOFT DISCONNECT, USB RECONNECT, USB BUS RESET, USB ENUM SPEED FIELD, SETUP logs, SET ADDRESS, SET CONFIGURATION=1, OPENBOOT USB CONFIGURED. Check lsusb and sudo lsusb -v -d 1209:0001. In VMware the new VID/PID may attach to the host instead of the guest; manually reconnect the new device to Ubuntu if necessary. If only Samsung remains or the new device does not enumerate, capture all UART and kernel messages. If e hangs or q disconnects, cold power-cycle to restore the ROM. No NAND/SD access. Do not send another DNW image to the enumeration-only device.


## Windows enumeration result and existing DNW driver ID (2026-11-25)

The user observed the custom 1209:0001 device in a Windows USB inspection utility: OpenBoot / TQ210 OTG 1, USB 2.0 High-Speed, device/configuration/interface/qualifier/other-speed descriptors parsed correctly. Windows reported problem code 28 and configuration 0, and VMware did not offer the device. No SET_CONFIGURATION was seen. Unsupported Microsoft OS string 0xEE and BOS requests were stalled; this did not prevent descriptor enumeration.

The SDK's Windows平台工具/TQBoardUSB下载驱动/TQBoardUSB下载驱动/EmbedSky_TQ_Board.inf specifies DeviceID = "VID_04E8&PID_1234" and libusb-win32, default configuration 1. For private development on this board, rom_usb_dnw_enum.bin now advertises that VID/PID to reuse the already installed driver and VMware matching. Manufacturer/product strings remain OpenBoot / TQ210 OTG 1, and the interface intentionally advertises zero bulk endpoints. This is ID-level compatibility, NOT a byte-for-byte clone or DNW protocol compatibility. The original BL2 blob b271436e5e693a1de43b872a4c8b1d3d2937a83d contains other-speed configuration data (full speed IN 0x83 / OUT 0x04, MPS 64; high speed IN 0x81 / OUT 0x02, MPS 512), which will be used as a reference when implementing the actual endpoints. Do not advertise unimplemented endpoints. Same VID/PID as iROM is ambiguous; never use automatic same-ID detection as proof of stage2 readiness.

    git pull
    sh boot/build.sh
    # cold reboot into iROM USB mode, open UART0
    sudo ./build-host/tq210-dnw out/rom_usb_dnw_enum.bin --pad-vendor-size

After DDR EXEC PASS, send lowercase e. Confirm UART announces "USB RECONNECT: local DNW driver ID 04e8:1234; EP0 only". Check Windows Device Manager / USB inspector and VMware Removable Devices. Attach to Ubuntu if necessary. Expect SET CONFIGURATION=1 and OPENBOOT USB CONFIGURED if the host selects the configuration. In Ubuntu use lsusb -d 04e8:1234 and sudo lsusb -v -d 04e8:1234; confirm product is TQ210 OTG 1, not the ROM product. The prior rom_usb_enum.bin remains available unchanged. Do not start the DNW tool again, since bulk is not implemented. No firmware binary or original driver modified. Reuse of installed driver is expected, not guaranteed (driver policy/cache/VMware may affect binding). Source-reviewed, not cross-compiled or hardware-tested yet.


## DNW-ID takeover diagnostics
The first DNW-ID variant reportedly stopped printing after USB SOFT DISCONNECT, before its reconnect message. That does not establish a VID/PID or Windows driver failure: no new descriptors have been sent yet. This update logs each takeover step and the pre-reset GAHBCFG/DCTL/GRSTCTL values, masks controller interrupts before asserting disconnect, waits for disconnect to settle before resetting the core, and replaces the original 50-million-iteration volatile SRAM loop with a register-only ARM delay. All existing bounded AHB/core reset/FIFO waits are retained with register diagnostics on timeout. The PHY, DDR, EP0 descriptors and test VID/PID choices are unchanged. Delay duration is approximate, not measured. MMIO bus stalls cannot be bounded by software polling; the last step label is intended to identify them.

Rebuild and cold-boot rom_usb_dnw_enum.bin as before; send lowercase e once after DDR EXEC PASS. Capture from PRE GAHBCFG through the last [USB n] step or USB RECONNECT. If it stops, do not repeatedly send e; cold power-cycle to recover. If needed compare rom_usb_enum.bin (1209:0001) with the same takeover code, using separate cold boots. Neither variant supports bulk downloads yet. Source reviewed; no cross-build or hardware validation for this revision.


## EP0 takeover confirmed; EP2 PIO bulk experiment
The 7343cb2 build completed all eleven takeover steps and reached SET_CONFIGURATION=1 and OPENBOOT USB CONFIGURED on the user's board. Brief pauses at steps 3 and 7 are intentional register-loop delays; their real duration was not measured. Preserve rom_usb_dnw_enum.bin as the EP0 baseline.

New rom_usb_bulk.bin advertises a single OUT endpoint 0x02 (512-byte HS, 64-byte FS) and product TQ210 Bulk Test, bcdDevice 2, same local development VID/PID 04e8:1234. Bulk IN and original DNW stage2 protocol are NOT implemented. The device uses PIO, one packet per arm, and receives a 16-byte LE header (OBL1 magic 0x314c424f, payload length, standard CRC32, reserved 0), followed by payload. Maximum 16 MiB to fixed validated DDR region 0x24000000; no arbitrary address or execution. CRC32 is computed from DDR readback. Vendor EP0 read-only request C0/5A, value 4F42, index 4C31, length 20 reports magic, state (0 ready/1 receive/2 pass/3 fail), received count, expected length, CRC. Reset/reconfiguration discards transfer state. The new host --bulk-test checks product string, revision, active configuration and protocol magic before any bulk writes, sends 1 MiB+13 bytes and verifies device status; do not run it against iROM or earlier variants.

    git pull
    sh boot/build.sh
    cmake --build build-host
    # cold reboot into ROM mode; UART open
    sudo ./build-host/tq210-dnw out/rom_usb_bulk.bin --pad-vendor-size

Send e; attach TQ210 Bulk Test to Ubuntu. Confirm EP2 BULK OUT READY MPS=0x00000200 and OPENBOOT USB CONFIGURED. Run:

    lsusb -d 04e8:1234
    sudo ./build-host/tq210-dnw --bulk-test

Expected BULK DDR VERIFY PASS on both host and UART, same CRC32. Stop on any error, cold power-cycle, and capture logs. No second upload to an already-used receiver; no execution or flash write. Newly written source, not cross-compiled or board-tested in this submission. Driver timing, endpoint interrupts and FS transfer remain experimental.


## EP2 bulk data transport validated
User's rom_usb_bulk.bin plus native --bulk-test transferred 1,048,589 payload bytes (+16 header), all 1,048,605 wire bytes. Host and DDR readback CRC32 both 0x6eefefc7. Last short packet accepted. No timing data was provided; HS link rate is not measured throughput.

## Experimental ARM DDR image loader
rom_usb_exec.bin adds the executable flag (header word 3 = 1), product TQ210 Exec Test, bcdDevice=3. Preserves fixed 0x24000000 destination, max 16 MiB, requires 4-byte-aligned length for executable images; the previous bulk firmware accepts only flag 0. New --load FILE checks identity and protocol, transfers the image, and verifies status/CRC, but never starts it automatically. Only lowercase x on UART, after host verification, allows execution of a verified flag-1 image; ordinary --bulk-test data cannot execute. It checks MMU/D-cache off and USB DMA disabled, verifies DDR CRC again, disconnects USB, disables IRQ/FIQ, invalidates I-cache and branch predictor, then calls ARM entry 0x24000000. SVC mode, existing iRAM stack and VBAR inherited; payload is responsible for runtime, stack and exception vectors. Return stops in OpenBoot. No Linux boot protocol, relocation, ELF parsing or flash write. This is intended for trusted local bare-metal images, not authenticated boot.

Build:
    git pull
    sh boot/build.sh
    cmake --build build-host
    # cold reboot to iROM USB mode
    sudo ./build-host/tq210-dnw out/rom_usb_exec.bin --pad-vendor-size

Send e in UART, attach new device to Ubuntu, wait for EP2 ready and OPENBOOT USB CONFIGURED:
    sudo ./build-host/tq210-dnw --load out/ddr_app.bin

After host BULK DDR VERIFY PASS, send lowercase x on UART. The independently linked stackless ARM test payload checks its entry address and prints "DDR APP EXEC PASS: independent ARM image at 0x24000000", then loops. Recover by cold power-cycle. Do not run --bulk-test before --load on the same receiver without reboot. Newly written source not cross-compiled or tested on target; original rom_usb_bulk.bin retained.


## Independent DDR execution validated
User ran rom_usb_exec.bin and --load out/ddr_app.bin, then UART x. 188 bytes (0xbc) received at 0x24000000, CRC32 0x9bb258bc matched on initial readback and second verification, USB disconnected, independent DDR APP EXEC PASS printed. This validates the manual two-stage boot chain at ROM clocks.

## Experimental automatic two-stage boot
rom_usb_auto.bin adds auto takeover after DDR checks, revision 4 / TQ210 Auto Boot, and explicit vendor control GO (40/5b, value 4f42, index 4c31, no data). Requires verified flag-1 image and completed EP0 IN status before launch, then waits briefly before disconnect and repeats DDR CRC. Existing UART x fallback retained. --boot verifies cold ROM product string, pads SRAM image to 11168 bytes, uses known ROM framing, waits up to 60 seconds for new product/revision and ready protocol status, sends DDR image and checks CRC, then issues GO. It does not resend stage1 automatically. Cold boot with only one matching board connected. Exact ROM product string check is conservative and may need adjustment for a differing ROM descriptor. VMware may require manual reattachment; do not send e/x unless diagnosing manually. Host GO ACK does not prove CPU execution; UART is the proof. No NAND/SD writes, Linux ABI, cryptographic authentication, or arbitrary addresses. New source not compiled or target-tested in this revision; known manual firmware remains available.

    git pull
    sh boot/build.sh
    cmake --build build-host
    # cold reboot to iROM USB mode, keep UART monitor open
    sudo ./build-host/tq210-dnw --boot out/rom_usb_auto.bin out/ddr_app.bin

Expected final UART: DDR APP EXEC PASS: independent ARM image at 0x24000000. If auto firmware exceeds 11168 bytes, host refuses rather than sending an unverified ROM frame. If timeout/error occurs capture host and UART logs; cold reset before retry.


## Automatic boot host path validated; timing experiment
The user's 0a2416b run completed both stages and USB GO ACK. No wall-clock timestamps or automatic-run UART success line were provided; actual program execution for this specific automatic run is not independently confirmed here (manual x execution was confirmed earlier).

The new host reports elapsed milliseconds for ROM upload, stage2 readiness including re-enumeration, DDR upload including host CRC, verification status, GO ACK, and total. Auto discovery checks device revision before requesting strings, polls every 50ms instead of 200ms, and uses a 1s readiness control request timeout (post-upload verification retains 30s). The 60s discovery deadline is not an intentional fixed wait. A slow synchronous libusb call may extend the deadline slightly.

New optional rom_usb_fast.bin keeps both original 40-million-iteration disconnect/device-mode settle delays, ROM framing, DDR checks, second CRC and USB identity. It reduces the separate pre-takeover and post-GO delays to 4 million iterations and suppresses the three verbose lines per EP0 SETUP request. Actual savings depend on CPU and host timing. The original rom_usb_auto.bin remains available with the original delays and logs. Neither variant changes the 400 MHz CPU clock or PIO bulk engine. These are experimental changes, not measured speedups; build and compare cold boots on the same VM.

    git pull
    sh boot/build.sh
    cmake --build build-host
    # each command on a separate cold boot
    sudo ./build-host/tq210-dnw --boot out/rom_usb_auto.bin out/ddr_app.bin
    sudo ./build-host/tq210-dnw --boot out/rom_usb_fast.bin out/ddr_app.bin

Record all [TIME] lines and the final UART DDR APP EXEC PASS. Total measures host GO ACK, not final payload execution. If fast mode regresses, use baseline. No target cross-build or hardware validation performed for this commit. Further optimization should follow the measurements; VMware/Windows enumeration latency may dominate.


## Fast boot latency measurement
User measured 19 ms ROM upload, 4925 ms stage2 readiness (includes DDR init and USB re-enumeration), 1 ms DDR upload, 18 ms verification, 0 ms GO ACK, total 4968 ms. This does not separately time device initialization or VMware enumeration; 99.1% is the aggregate readiness interval.

## Experimental no-re-enumeration DMA handoff
New rom_usb_handoff.bin and --handoff-test intentionally keep the original USB address and host libusb handle, no reset, no SET_CONFIGURATION after stage1, no product or status query after handoff. DWC OTG 2.81a buffer DMA remains enabled; EP2 must already be inactive, active-type bulk/512-byte HS, and not stalled. Assert global OUT NAK and wait for AHB idle; redirect inherited EP0 DMA pointers into owned aligned 512-byte SRAM scratch, verify readback, abort on unexpected setup/reset/DMA error. This is a controlled EP2-only experiment, not a general USB stack: EP0 SETUP may bypass NAK; no other host traffic allowed. Writes to active EP0 DMA registers may be ignored or undefined on this revision; readback is a precaution, not proof of safety. A hardware fault or failure requires cold reset. Original SRAM/DDR checks retained; use two separate fixed DMA packets (512 then 37 bytes) with no DATA0/DATA1 reset command, verify exact length, contents and DDR copy at 0x24000000. No arbitrary code execution or NAND/SD writes. DMA/endpoint register layout reference https://codebrowser.dev/linux/linux/drivers/usb/dwc2/hw.h.html ; global OUT NAK/endpoint disable reference https://cos.googlesource.com/third_party/kernel/+/7666206b23e339344a2974d8af2b7fce882288f5/drivers/usb/dwc2/gadget.c . These sources do not establish that live ROM takeover is supported.

    git pull
    sh boot/build.sh
    cmake --build build-host
    # cold ROM, single board, UART monitor open, no lsusb -v/other USB tools
    sudo ./build-host/tq210-dnw --handoff-test out/rom_usb_handoff.bin

Wait for exact HANDOFF EP2 READY on UART, then type READY in host terminal. If no READY, type anything else or Ctrl-C and cold reset. Do not send more data. Expected HANDOFF SAME CONNECTION PASS on UART plus two same-handle transfers on host. No automatic fallback or retry. This first experiment intentionally uses a manual readiness gate to prevent sending data to the ROM if takeover failed; it cannot yet measure automatic total boot latency. Keep original rom_usb_fast.bin. Source not cross-compiled/target-tested in this commit.


## Automatic same-handle readiness experiment
User saw HANDOFF EP2 READY on UART with the initial manual variant; no confirmed packets yet. Replace manual stdin READY with a read-only Bulk IN 0x81 readiness message on the existing USB handle. Before sending stage1, host checks active ROM configuration advertises HS Bulk IN 0x81 and OUT 0x02 on the same interface. Device additionally checks inherited EP1 active bulk configuration, idle and MPS=512, preserves TX FIFO selection and data PID, supplies aligned owned SRAM DMA buffer and one 16-byte IN transfer. After EP2 is armed, send OB-HANDOFF-RDY1!; only exact received marker permits host to send 512 and 37-byte test packets. After DDR content verification device returns OB-HANDOFF-OK01!; host prints HANDOFF DDR VERIFY PASS only if exact marker received. Host IN requests have 5-second timeout; no automatic retry, no EP0 requests after stage1, no re-enumeration, no execution. Earlier EP0 DMA guard caveats still apply; IN hardware behavior has not been tested. UART remains diagnostic only. Rebuild host and firmware together; same command --handoff-test out/rom_usb_handoff.bin, no input required. If error occurs, capture host and UART logs and cold power cycle. Source not cross-compiled or hardware-tested in this revision.


## Same-handle bidirectional DMA validated
The 0ada083 firmware printed EP1 readiness/verification notice completion, EP2 packet lengths 512 and 37, EP2 control alternating 0x000A8200 and 0x000B8200, and HANDOFF SAME CONNECTION PASS: 549 bytes, DMA + DDR readback. This establishes a working controlled same-connection transfer in both directions, without a USB reset/re-enumeration. No total timing reported for this run.

## Experimental continuous direct-to-DDR DMA boot
New rom_usb_dma_boot.bin retains the verified handoff guard and EP1 notification path. Protocol OBL2: 16-byte header (magic 0x324c424f, length, CRC32, executable flag), fixed DDR base 0x24000000, maximum 16 MiB. Header is a separate short OUT transfer, then EP2 receives directly into DDR in windows up to 65536 bytes / 128 packets. Final DMA capacity is rounded to 512 bytes within the reserved region, and actual length is checked against residual. Each transfer is armed only after the preceding DMA finishes. Readback CRC32 uses a 16-entry nibble table. EP1 sends exact readiness, header acceptance, verify and GO acknowledgment tokens. Executable images require a separate exact GO packet and a second CRC before GO ACK. Disconnect, disable DMA after AHB idle, invalidate I-cache/branch predictor, enter ARM image at 0x24000000. Same inherited SVC stack/VBAR ABI as earlier, no Linux boot ABI. No NAND/SD writes. Data-only test does not execute. Both host and firmware must be updated; original rom_usb_handoff.bin and rom_usb_fast.bin retained. Inherited EP0 guard limitations remain; controlled host only, no other USB queries.

    git pull
    sh boot/build.sh
    cmake --build build-host
    # separate cold boots for each command
    sudo ./build-host/tq210-dnw --dma-boot out/rom_usb_dma_boot.bin out/ddr_app.bin
    sudo ./build-host/tq210-dnw --dma-test out/rom_usb_dma_boot.bin

Expected boot UART DDR APP EXEC PASS; data-only test 1048589 bytes CRC32 0x6eefefc7, no execution. Host timing ends at GO ACK, not actual application execution. No measured direct-DDR throughput yet; new multi-packet DMA mode, CRC and launch path are uncompiled and untested in this revision. Fail closed on unexpected state; cold reboot before retry.
