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
