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
