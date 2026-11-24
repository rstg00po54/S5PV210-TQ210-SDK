# USB research update

## Direct binary evidence
Vendor ELF Git blob f3455348b6ce5f52d967632508f2c47817ea11f1, x86-64.
- 0x427ab0 stores the fifth argument (image type, r9d) at object+0x20. Type 2 in TQMain::download210Boot (0x417910) selects ordinary framing.
- 0x417710 uses 0xd0020010 for the boot firmware mode and 0x23e00000 for image type 2 in the other branch.
- 0x427f39 computes file length+10; 0x427f4f–0x427f5e sets the address and length header. 0x4280b8 requests 2040 bytes for the first block, 0x428091 requests 2048 for subsequent blocks.
- 0x42804c calls libusb_bulk_transfer, timeout 2000 ms; 0x428143 calls it for a separate 2-byte checksum, timeout 1000 ms. Both read OUT endpoint at global 0x641114.
- csum_upload (0x4278a0) calls fread once with size 1 and count 0x1000, then sums the bytes returned. No outer read loop. Default experiment replicates its first-4096-byte checksum; full-file sum is a selectable alternative. Vendor may issue an extra zero-length bulk call on exact block boundaries due to feof loop; experimental uploader omits that call.
- The 283-byte special prefix branch is reached only with image type zero, and checks whether the file ends in "nb0" (string at 0x42bdb4). It is not part of the type-2 normal boot path.

## BL2 binary
Git blob b271436e5e693a1de43b872a4c8b1d3d2937a83d, 11168 bytes.
- First word EA000006 (branch to file offset 0x20).
- Literal 0xd0022ab2 at file offset 0x10f8 corresponds to descriptor data at file offset 0x2aa2 if link base is 0xd0020010; this supports the vendor header address. At 0x1d68 is 0x23e00000.
- Contains full-speed OUT 0x04 and high-speed OUT 0x02 descriptor bytes, and SEC S5PC110 Test B/D string. USB ID may be identical after handoff. Re-enumeration alone is not proof of execution.
- Calculated additive checksums: first 4096 bytes 0xeef6, whole image 0x18b2.
- TQ210-specific corroboration: https://www.eepw.com.cn/article/201611/322804.htm describes sending a board firmware to 0xd0020010, then a DDR user image to 0x23e00000. Do not confuse USB packet address with ROM entry.

## Ubuntu experiment
The new Python tool uses the existing libusb runtime through ctypes; no Qt or pip dependencies. It performs only USB writes, not flash commands. The supplied stage1 is vendor firmware, not our own. Stage2 uses a new no-stack ARM assembly UART payload linked for DDR. PCLK is assumed to match vendor clock setup (~66.7 MHz); no PLL changes are made after BL2. UART0/1 TX outputs are tested at 115200 8N1. Hardware behavior unverified.

    cd research
    python3 dnw_ram.py --selftest
    python3 dnw_ram.py stage1 --dry-run
    bash build_uart_ddr.sh
    sudo python3 dnw_ram.py stage1

Wait for device to re-enumerate; check lsusb and VMware attachment, then:
    sudo python3 dnw_ram.py stage2 uart_ddr.bin

If stage1 fails to start, reset and retry once with --checksum full. Do not send stage2 until there is evidence of stage1 handoff. No automatic retries. Observe Windows COM5; stage2 prints continuously. Disconnect CH340 TX from CPU RX if voltage compatibility is unverified. Neither USB transport success nor same VID/PID proves CPU execution.
