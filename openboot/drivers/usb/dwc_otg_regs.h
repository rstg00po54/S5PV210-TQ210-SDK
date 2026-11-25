#ifndef OPENBOOT_DWC_OTG_REGS_H
#define OPENBOOT_DWC_OTG_REGS_H

#include "openboot/platform.h"

/* Private device-mode hardware registers; inherited ROM state is preserved. */
#define HR(o) MMIO32(S5PV210_USB_OTG_BASE+(o))
#define HW(o,v) mmio_write32(S5PV210_USB_OTG_BASE+(o),(v))
#define HB(n) (1u<<(n))
#define H_EPENA HB(31)
#define H_SNAK HB(27)
#define H_CNAK HB(26)
#define H_DPID HB(16)
#define H_XFRC HB(0)
#define H_RESET HB(12)
#define H_SETUP (HB(3)|HB(6))
#define H_DMAERR HB(2)
#define H_DCTL 0x804u
#define H_GINTSTS 0x014u
#define H_GRSTCTL 0x010u
#define H_DIEPCTL1 0x920u
#define H_DIEPINT1 0x928u
#define H_DIEPTSIZ1 0x930u
#define H_DIEPDMA1 0x934u
#define H_DOEPCTL2 0xb40u
#define H_DOEPINT2 0xb48u
#define H_DOEPTSIZ2 0xb50u
#define H_DOEPDMA2 0xb54u

#define H_GAHBCFG 0x008u
#define H_GINTMSK 0x018u
#define H_GSNPSID 0x040u
#define H_DCFG 0x800u
#define H_DSTS 0x808u
#define H_DIEPCTL0 0x900u
#define H_DIEPINT0 0x908u
#define H_DIEPDMA0 0x914u
#define H_DOEPINT0 0xb08u
#define H_DOEPDMA0 0xb14u

#define H_DMA_ENABLE HB(5)
#define H_GLOBAL_IRQ HB(0)
#define H_AHB_IDLE HB(31)
#define H_SOFT_DISCONNECT HB(1)
#define H_GOUT_NAK_STATUS HB(3)
#define H_GIN_NAK_STATUS HB(2)
#define H_CLEAR_IN_NAK HB(8)
#define H_SET_OUT_NAK HB(9)
#define H_CLEAR_OUT_NAK HB(10)

#endif
