# OpenBoot v0.3 — cold iROM clock preservation
Hardware snapshot 2026-11-25, obtained without vendor BL2:
```
ENTRY_CPSR=0x60000153
ENTRY_SP=0xD0037D50
MIDR=0x412FC082
SCTLR=0x00C51078
VBAR=0x00000000
APLL_CON0=0xA0C80601
MPLL_CON=0xA29B0C01
EPLL_CON0=0xA8500303
VPLL_CON=0x006C0303
CLK_SRC0=0x00000111
CLK_SRC1=0x00000000
CLK_SRC4=0x66667777
CLK_SRC6=0x00000000
CLK_DIV0=0x14141251
CLK_DIV1=0x00000400
CLK_DIV4=0x99990000
CLK_DIV6=0x00071000
CLK_DIV_STAT0=0x00000000
CLK_DIV_STAT1=0x00000000
CLK_GATE_0400=0xFFFFFFFF
CLK_GATE_0404=0xFFFFFFFF
```
Assuming 24 MHz FIN: APLL=800 MHz, ARMCLK=400 MHz, HCLKM=133.33 MHz, A2M=133.33 MHz, DMC0 from A2M /1, DMC1 from HCLKM, PCLKP=66.7 MHz. No actual frequency measurement. Sources: Linux S5PV210 clock driver and legacy mach-s5pv210/clock.c. Use the ROM clock tree untouched; only UART gates/pinmux and watchdog control. DDR init guarded by matching key clock registers exactly. TIMINGAREF=0x400 (nominal 7.68 us at 133.33 MHz), rather than original 0x618 at 200 MHz; remaining timing cycles retained as experimental conservative configuration. PHY force/status sequence from original BL2; validity at lower clock not confirmed. Log DMC control/status before writes.
From openboot:
```sh
git pull
sh boot/build.sh
# cold power-cycle into ROM USB mode, UART0 115200 8N1 open
sudo ./build-host/tq210-dnw out/rom_sram.bin --pad-vendor-size
# only after SRAM PASS: cold power-cycle back into ROM USB mode
sudo ./build-host/tq210-dnw out/rom_ddr.bin --pad-vendor-size
```
Do not run old sram.bin/ddr.bin for this experiment. The two new binaries compile to 1524 and 3952 bytes with local Clang/LLD, and ELF entry 0xD0020010; VBAR and stack checked. Hardware SRAM and DDR scratch/execute tests subsequently passed; see [validation log](HW_VALIDATION_V03.md). A stackless '>' marker precedes startup, then C entry, then exact clock checks, then optional DDR. SRAM PASS means C initialization and inherited clock checks pass; DDR SCRATCH/EXEC PASS means two 16 KiB areas and an instruction stub work, not capacity or full stability. After the basic test passes, optional alias probes may be run individually during standalone testing; see the validation log for limitations. No NAND/SD writes. Power-cycle on failure; report entire serial output.
