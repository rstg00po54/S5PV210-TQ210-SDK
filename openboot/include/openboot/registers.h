#ifndef OPENBOOT_S5PV210_REGISTERS_H
#define OPENBOOT_S5PV210_REGISTERS_H

/* TQ210 CoreB physical addresses. Timing values and write order are inherited
 * from the previously validated ROM-clock implementation. */
#define S5PV210_CLK_BASE          0xe0100000u
#define S5PV210_UART0_BASE        0xe2900000u
#define S5PV210_GPIO_BASE         0xe0200000u
#define S5PV210_WATCHDOG_BASE     0xe2700000u
#define S5PV210_DMC0_BASE         0xf0000000u
#define S5PV210_DMC1_BASE         0xf1400000u
#define S5PV210_USB_OTG_BASE      0xec000000u
#define S5PV210_USB_PHY_CONTROL   0xe010e80cu
#define CLK_GATE_IP1             0x464u
#define CLK_GATE_IP3             0x46cu
#define CLK_APLL_CON0            0x100u
#define CLK_MPLL_CON             0x108u
#define CLK_EPLL_CON0            0x110u
#define CLK_SRC0                 0x200u
#define CLK_SRC6                 0x218u
#define CLK_DIV0                 0x300u
#define CLK_DIV6                 0x318u
#define CLK_DIV_STAT0            0x1000u
#define UART_ULCON               0x00u
#define UART_UCON                0x04u
#define UART_UFCON               0x08u
#define UART_UMCON               0x0cu
#define UART_UTRSTAT             0x10u
#define UART_UTXH                0x20u
#define UART_URXH                0x24u
#define UART_UBRDIV              0x28u
#define UART_UDIVSLOT            0x2cu
#define DMC_CONCONTROL           0x00u
#define DMC_MEMCONTROL           0x04u
#define DMC_MEMCONFIG0           0x08u
#define DMC_MEMCONFIG1           0x0cu
#define DMC_DIRECTCMD            0x10u
#define DMC_PRECHCONFIG          0x14u
#define DMC_PHYCONTROL0          0x18u
#define DMC_PHYCONTROL1          0x1cu
#define DMC_PWRDNCONFIG          0x28u
#define DMC_TIMINGAREF           0x30u
#define DMC_TIMINGROW            0x34u
#define DMC_TIMINGDATA           0x38u
#define DMC_TIMINGPOWER          0x3cu
#define DMC_PHYSTATUS            0x40u
#define DMC_QOSCONTROL0          0x60u
#define DMC_QOSCONFIG0           0x64u
#define DDR0_BASE                0x20000000u
#define DDR1_BASE                0x40000000u
#define DDR_END                  0x60000000u
#define DDR0_SCRATCH             0x20010000u
#define DDR1_SCRATCH             0x40010000u
#define DDR_IMAGE_BASE           0x24000000u
#define DDR_IMAGE_LIMIT          0x01000000u
#endif
