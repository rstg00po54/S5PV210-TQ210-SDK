# S5PV210 / TQ210 research

This directory is independent of the original SDK. No vendor files are modified.
Status: protocol reverse engineering, not hardware validated.

## Findings from the SDK
- Linux平台工具/opt/EmbedSky/TQBoardDNW for linux/TQBoardDNW_64 (ELF, unstripped) was inspected directly. Symbol addresses: QDownLoadThread::run 0x427d80, TQMain::downLoadFile 0x417710, TQMain::download210Boot 0x417910, csum_upload 0x4278a0.
- downLoadFile contains explicit address constants 0xd0020010 and 0x23e00000. The selection depends on mode and image type. Do not assume the USB header address is the CPU execution entry.
- run builds an 8-byte header with address and file length + 10. The ordinary transfer path reads up to 2040 payload bytes into the first 2048-byte block, then up to 2048 bytes per block; a separate 2-byte checksum transfer exists. A special image path also exists and is not yet decoded.
- The checksum routine includes an unsigned-byte sum. Coverage of the entire file versus a bounded read must still be verified for long files.
- The vendor readme explicitly requires V210_USB.BL2.bin for USB boot. It is an ARM image (initial ARM branch to offset 0x20), not a generic raw UART test. Its required wrapping and execution address are not yet verified.
- Original src/12.uart_putchar/Makefile links at 0x20000000 and start.S sets SP=0x40000000. It requires initialized DDR. main.c waits for a received byte, then transmits byte+1; there is no unsolicited greeting.
- USB transfer success is not evidence of execution. Do not infer a damaged UART from failed unverified boot images.

## Tools
- audit_vendor.sh: extracts exact vendor function disassembly and optional ARM firmware disassembly into a local output directory.
- dnw_probe.c: read-only USB descriptor/endpoint diagnostic using libusb 1.0, no payload writes. Used to distinguish transport from boot-image problems.

Run:
    cd research
    bash audit_vendor.sh
    make
    sudo ./dnw_probe

## Next experiment
Decode special image framing and checksum coverage from the disassembly; consult the supplied iROM application note. Only then implement a boot-stage uploader. Isolate CH340C TX if its voltage exceeds the core board UART rating. Use the original UART test after DDR initialization and verify by sending A and receiving B at 115200 8N1.

Do not flash NAND/SD or send unverified initialization images.
