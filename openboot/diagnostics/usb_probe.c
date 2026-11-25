#include "openboot/platform.h"
#ifndef SRAM_ONLY
#ifdef USB_STATE_PROBE
/* Read-only register survey: do not pop GRXSTSP or read FIFO windows.
 * No core reset, PHY power changes, interrupt ACK, DMA, or USB packets. */
struct usb_reg
{
    const char *name;
    uint32_t offset;
};
static uint32_t usb_report(const char *name, uint32_t a)
{
    console_write(name);
    console_write("=");
    uint32_t v = MMIO32(a);
    console_hex(v);
    console_write("\r\n");
    return v;
}
void usb_state(void)
{
    static const struct usb_reg common[] = {
        {"GOTGCTL", 0x000}, {"GOTGINT", 0x004}, {"GAHBCFG", 0x008},
        {"GUSBCFG", 0x00c}, {"GRSTCTL", 0x010}, {"GINTMSK", 0x018},
        {"GRXFSIZ", 0x024}, {"GNPTXFSIZ", 0x028},
        {"GHWCFG1", 0x044}, {"GHWCFG2", 0x048}, {"GHWCFG3", 0x04c},
        {"GHWCFG4", 0x050}
    };
    static const struct usb_reg device[] = {
        {"DCFG", 0x800}, {"DCTL", 0x804}, {"DSTS", 0x808},
        {"DIEPMSK", 0x810}, {"DOEPMSK", 0x814}, {"DAINT", 0x818},
        {"DAINTMSK", 0x81c}, {"DIEPCTL0", 0x900}, {"DIEPINT0", 0x908},
        {"DIEPTSIZ0", 0x910}, {"DIEPDMA0", 0x914},
        {"DOEPCTL0", 0xb00}, {"DOEPINT0", 0xb08},
        {"DOEPTSIZ0", 0xb10}, {"DOEPDMA0", 0xb14},
        {"DOEPCTL2", 0xb40}, {"DOEPINT2", 0xb48},
        {"DOEPTSIZ2", 0xb50}, {"DOEPDMA2", 0xb54}
    };
    console_write("\r\nUSB OTG STATE v0.1 (read only)\r\n");
    uint32_t gate = usb_report("CLK_GATE_IP1", 0xe0100464u);
    uint32_t iso = usb_report("USB_PHY_CONTROL", S5PV210_USB_PHY_CONTROL);
    if (!(gate&(1u << 16)))
    {
        console_write("OTG CLOCK GATED; no controller access\r\n");
        return;
    }
    if (!(iso&1u))
    {
        console_write("DEVICE PHY ISOLATED; no controller access\r\n");
        return;
    }
    usb_report("PHY_PWR", 0xec100000u);
    usb_report("PHY_CLK", 0xec100004u);
    usb_report("PHY_RST", 0xec100008u);
    uint32_t id = usb_report("GSNPSID", 0xec000040u);
    if ((id&0xffff0000u) != 0x4f540000u)
    {
        console_write("Unrecognized core ID; stop register survey\r\n");
        return;
    }
    for (unsigned i = 0; i < sizeof(common)/sizeof(common[0]); i++)
    {
        usb_report(common[i].name, S5PV210_USB_OTG_BASE+common[i].offset);
    }
    uint32_t status = usb_report("GINTSTS", 0xec000014u);
    if (status&1u)
    {
        console_write("HOST MODE; device registers skipped\r\n");
        return;
    }
    for (unsigned i = 0; i < sizeof(device)/sizeof(device[0]); i++)
    {
        usb_report(device[i].name, S5PV210_USB_OTG_BASE+device[i].offset);
    }
    console_write("USB STATE DONE\r\n");
}
#endif
#endif
