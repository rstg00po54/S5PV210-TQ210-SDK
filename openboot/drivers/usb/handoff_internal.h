#ifndef OPENBOOT_USB_HANDOFF_INTERNAL_H
#define OPENBOOT_USB_HANDOFF_INTERNAL_H
#include "openboot/platform.h"
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

extern volatile uint32_t handoff_packet[128];
void handoff_abort(const char *reason);
void handoff_check(void);
void handoff_arm(void);
void handoff_notice(const char *message);
void handoff_arm_dma(uint32_t address, unsigned bytes);
void handoff_wait_dma(unsigned capacity, unsigned expected);
void handoff_run(void);
#endif
