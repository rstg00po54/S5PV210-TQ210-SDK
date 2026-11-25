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
