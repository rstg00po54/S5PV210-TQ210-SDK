#ifndef OPENBOOT_USB_HANDOFF_INTERNAL_H
#define OPENBOOT_USB_HANDOFF_INTERNAL_H

#include "openboot/platform.h"

/* Interface between the OBL2 protocol and the inherited USB DMA transport. */
extern volatile uint32_t handoff_packet[128];

__attribute__((noreturn)) void handoff_abort(const char *reason);
void handoff_check(void);
void handoff_arm(void);
void handoff_enable_out(void);
uint32_t handoff_out_control(void);
void handoff_ack_out(void);
unsigned handoff_receive_length(unsigned capacity);
void handoff_notice(const char *message);
void handoff_arm_dma(uint32_t address, unsigned bytes);
void handoff_wait_dma(unsigned capacity, unsigned expected);
__attribute__((noreturn)) void handoff_run(void);

#endif
