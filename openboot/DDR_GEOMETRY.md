# v0.4 DMC0 geometry A/B test
## Observed v0.3 hardware alias test
At DMC0 0x20010000, 0x28010000 and 0x30010000 all returned the last written pattern A11A0005; offsets 16, 32 and 64 MiB were distinct. DMC1 offsets 0, 16, 32, 64, 128, 256 MiB returned six distinct values. Both earlier 16 KiB scratch tests and DMC0 execution passed. This establishes aliasing under the current controller geometry, not the physical chip capacity.

TQ210 CoreB public product description lists 1 GB DDR2 (https://m.elecfans.com/article/1092302.html). A TQ210 board report describes eight K4T1G084QQ chips, four x8 chips per DMC, 14 row bits, 10 column bits, 8 banks (https://www.eepw.com.cn/article/201611/322804.htm). Such a layout gives 512 MiB per DMC; exact population on this board is not yet verified. Original BL2's DMC0 config 0x20E01343 has a different row field from DMC1's 0x40E01323. A/B variant changes only active DMC0 MEMCONFIG0 to 0x20E01323 (plus an identifying log message); inactive MEMCONFIG1 remains unchanged, clock/timing settings and initialization sequence retained. Hypothesis: geometry mismatch causes mirroring, not missing chips. Baseline remains available.

From openboot:
    git pull
    sh boot/build.sh
    # cold power cycle into USB ROM mode
    sudo ./build-host/tq210-dnw out/rom_ddr_row14.bin --pad-vendor-size

After DDR SCRATCH PASS and EXEC PASS, send lowercase a, then b, then m separately. m writes two address-dependent patterns (forward and reverse order) over 172 sparse words across the two configured 512 MiB windows, including each 16 MiB block and lower address bits. It saves all original words before writes and restores afterward if accesses complete. Errors are counted separately per window. A zero-error result is not full RAM stress or definitive capacity confirmation. Invalid addresses may hang; record last output, power-cycle, and return to the known baseline rom_ddr.bin if needed. Do not run on a live OS. No NAND/SD writes. The new version is source-reviewed but not built or hardware-tested in this submission.


## Hardware validation — 2026-11-25

User's GCC build after division-free fix d7db969 ran successfully.
With DMC0 MEMCONFIG0=0x20E01323 and DMC1=0x40E01323:
- Both 16 KiB tests and DMC0 execution stub passed.
- Both six-address alias samples returned A11A0000 through A11A0005 in order.
- All 172 sparse addresses across both 512 MiB windows passed in both directions, with DMC0 MAP ERRORS=0 and DMC1 MAP ERRORS=0.
- DMC0's previous 128/256 MiB alias is absent at those samples. The controller geometry change is validated on this board; physical chip organization and exhaustive reliability are not established by sparse samples.

## Optional v0.5 full-window test

The previous rom_ddr_row14.bin remains the working baseline. New rom_ddr_full.bin uses identical DMC and clock parameters and adds uppercase T for a destructive DDR-only test. Build locally:

    git pull
    sh boot/build.sh

Cold power-cycle, enter iROM USB mode, open UART0, then:

    sudo ./build-host/tq210-dnw out/rom_ddr_full.bin --pad-vendor-size

After scratch and execution PASS, optionally run m again, then send uppercase T once. All DDR contents will be overwritten, including the old test stub. The iRAM code and stack remain intact. The test requires MMU and D-cache off, and both corrected MEMCONFIG0 values. It writes every aligned 32-bit word across both banks before verifying either bank, then repeats with the complementary address-dependent pattern. 32 MiB progress markers identify the current region. A complete successful run checks 268435456 words per pass, two passes, 4 GiB total read/write traffic. Counts are cumulative per bank. Do not use with live DDR data or an OS. No NAND/SD access; reset on hangs. The new command is source-reviewed, not yet cross-compiled or tested on hardware. This is an exhaustive address/pattern test, not a proof of long-term retention, all data patterns, or operation at other clocks.

## Full 1 GiB test — PASS (2026-11-25)

User ran rom_ddr_full.bin and uppercase T following a standalone boot. Both complete passes wrote every aligned 32-bit word in [0x20000000,0x60000000) and then read both banks. Each pass reported DMC0 and DMC1 cumulative errors 0. Final output:

    DMC0 CUMULATIVE ERRORS=0x00000000
    DMC1 CUMULATIVE ERRORS=0x00000000
    FULL DDR TEST PASS: 1024 MiB, two patterns, every 32-bit word
    DDR contents overwritten; iRAM console still running

32 write and 32 verify blocks per pass, two passes, 4 GiB total memory traffic. Corrected geometry confirmed at the inherited ROM clock state, without vendor BL2. Not a long-term retention, temperature, all-pattern, or higher-frequency qualification. Preserve this firmware as a baseline.
