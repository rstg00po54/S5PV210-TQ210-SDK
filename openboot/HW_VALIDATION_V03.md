# OpenBoot v0.3 — hardware validation, 2026-11-25

User-reported serial output, following the standalone iROM USB download test instructions. The SRAM and DDR builds ran successfully. The initial corrupted characters during the second upload are omitted; no substantive DDR lines are omitted. No vendor BL2 is needed on this boot path.

```text
TQ210 OpenBoot v0.3 ROM CLOCKS
[0] SRAM C ENTRY OK
[1] KEEP ROM CLOCKS
APLL=0xA0C80601
MPLL=0xA29B0C01
CLK_SRC0=0x00000111
CLK_DIV0=0x14141251
CLK_SRC6=0x00000000
CLK_DIV6=0x00071000
[1] ROM CLOCK CHECK PASS
SRAM TEST PASS; DDR not touched

[0] SRAM C ENTRY OK
[1] KEEP ROM CLOCKS
APLL=0xA0C80601
MPLL=0xA29B0C01
CLK_SRC0=0x00000111
CLK_DIV0=0x14141251
CLK_SRC6=0x00000000
CLK_DIV6=0x00071000
[1] ROM CLOCK CHECK PASS
[2] DMC0 PHY
BEFORE CONCONTROL=0x0FFF1350
BEFORE MEMCONTROL=0x00202100
BEFORE PHY=0x00000003
PHY POLL DONE=0x00000BC0
[2] DMC0 DDR2 commands
CONCONTROL=0x0FFF1330
MEMCONTROL=0x002024D3
TIMINGAREF=0x00000400
MEMCONFIG0=0x20E01343
PHYSTATUS=0x00002767
[3] DMC1 PHY
BEFORE CONCONTROL=0x0FFF1350
BEFORE MEMCONTROL=0x00202100
BEFORE PHY=0x00000003
PHY POLL DONE=0x00000500
[3] DMC1 DDR2 commands
CONCONTROL=0x0FFF1330
MEMCONTROL=0x002024D3
TIMINGAREF=0x00000400
MEMCONFIG0=0x40E01323
PHYSTATUS=0x00002757
[4] DDR scratch: 16 KiB per bank, two patterns
WRITE 0x20010000
WRITE 0x40010000
READ 0x20010000
READ 0x40010000
WRITE 0x20010000
WRITE 0x40010000
READ 0x20010000
READ 0x40010000
[4] DDR SCRATCH PASS (not capacity verification)
[5] DDR execution result=0x5A5A2100
[5] DDR EXEC PASS; back in SRAM
OpenBoot diagnostic complete. a=optional DMC0 alias probe, b=DMC1.
```

## Interpretation and limitations
- Standalone SRAM C startup and the exact inherited clock checks passed.
- DMC0 and DMC1 each passed two distinct 16 KiB patterns, with reads after writes to both banks.
- The position-independent ARM stub copied into DMC0 memory executed and returned the expected constant to SRAM.
- The configured 512 MiB windows are not proof of physical RAM capacity. Full memory stress, repeated cold boots, and executable tests in DMC1 remain unverified. No independent PLL reconfiguration, USB device driver or second-stage transfer has been demonstrated.
- This run did not modify NAND/SD.

## Next experiment
The existing rom_ddr.bin has a UART command loop. Send lowercase a (DMC0), wait for "Sample complete", then lowercase b (DMC1). It writes and restores six sampled 32-bit words at offsets 0, 16, 32, 64, 128, 256 MiB from each test base. Expected values in address order are 0xA11A0000 through 0xA11A0005 if none of the sampled addresses alias. These sparse samples do not establish full capacity. An unpopulated/invalid memory address may cause a bus hang; note the last PROBE address and power-cycle back into iROM, then reload rom_ddr.bin if necessary. Do not use on a running OS or with valuable data in RAM.
