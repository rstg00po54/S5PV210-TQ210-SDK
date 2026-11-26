// SPDX-License-Identifier: GPL-2.0+
/* TQ210 USB device handoff: preserve the ROM/OpenBoot PHY configuration. */
#include <command.h>
#include <asm/io.h>
#include <asm/arch/cpu.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <usb.h>
#include <usb/dwc2_udc.h>

#define OTG_REG(off) ((void __iomem *)(S5PC110_OTG_BASE + (off)))
#define CLK_GATE_IP1 ((void __iomem *)0xe0100464)
#define PHY_CTRL ((void __iomem *)S5PC110_USB_PHY_CONTROL)
#define DCTL_DISCONNECT BIT(1)

static struct dwc2_plat_otg_data tq210_otg = {
	.regs_phy = S5PC110_PHY_BASE,
	.regs_otg = S5PC110_OTG_BASE,
	.usb_phy_ctrl = S5PC110_USB_PHY_CONTROL,
	.rx_fifo_sz = 1024,
	.np_tx_fifo_sz = 256,
	.tx_fifo_sz = 256,
};

static int tq210_wait_idle(void)
{
	unsigned int i;

	for (i = 0; i < 1000; i++) {
		if (readl(OTG_REG(0x010)) & BIT(31))
			return 0;
		udelay(1000);
	}
	return -ETIMEDOUT;
}

int board_usb_init(int index, enum usb_init_type init)
{
	static bool probed;
	u32 hw3, hw4, gusbcfg;
	unsigned int words, in_eps;
	int ret;

	if (index || init != USB_INIT_DEVICE)
		return -EINVAL;
	if (!(readl(CLK_GATE_IP1) & BIT(16)) ||
	    !(readl(PHY_CTRL) & BIT(0))) {
		puts("TQ210 USB: ROM PHY not enabled; cold boot via OpenBoot\n");
		return -ENODEV;
	}
	if (readl(OTG_REG(0x040)) != 0x4f54281a) {
		puts("TQ210 USB: unexpected DWC2 core ID\n");
		return -ENODEV;
	}
	if (readl(OTG_REG(0x014)) & BIT(0)) {
		puts("TQ210 USB: controller is not in device mode\n");
		return -EINVAL;
	}

	/* OpenBoot has already detached and disabled its DMA. */
	writel(0, OTG_REG(0x018));
	setbits_le32(OTG_REG(0x804), DCTL_DISCONNECT);
	mdelay(100);
	ret = tq210_wait_idle();
	if (ret)
		return ret;
	writel(0, OTG_REG(0x008));

	gusbcfg = readl(OTG_REG(0x00c));
	tq210_otg.usb_gusbcfg = (gusbcfg & ~BIT(29)) | BIT(30);
	hw3 = readl(OTG_REG(0x04c));
	hw4 = readl(OTG_REG(0x050));
	words = hw3 >> 16;
	in_eps = (hw4 >> 26) & 0xf;
	printf("TQ210 USB: GSNPSID=%08x GHWCFG3=%08x GHWCFG4=%08x\n",
	       readl(OTG_REG(0x040)), hw3, hw4);
	printf("TQ210 USB: FIFO=%u words, IN EPs=%u, GUSBCFG=%08x\n",
	       words, in_eps, tq210_otg.usb_gusbcfg);
	if (words < 1280 + 256 * in_eps) {
		puts("TQ210 USB: FIFO allocation exceeds hardware capacity\n");
		return -ENOSPC;
	}
	if (probed)
		return 0;
	ret = dwc2_udc_probe(&tq210_otg);
	if (!ret)
		probed = true;
	return ret;
}

int board_usb_cleanup(int index, enum usb_init_type init)
{
	/* Legacy UDC unregister has quiesced DMA before freeing requests. */
	if (index || init != USB_INIT_DEVICE)
		return -EINVAL;
	setbits_le32(OTG_REG(0x804), DCTL_DISCONNECT);
	writel(0, OTG_REG(0x018));
	return 0;
}

static int do_tqusb(struct cmd_tbl *cmdtp, int flag, int argc,
		    char *const argv[])
{
	printf("USB clock gate=%08x PHY control=%08x\n",
	       readl(CLK_GATE_IP1), readl(PHY_CTRL));
	if (!(readl(CLK_GATE_IP1) & BIT(16)))
		return CMD_RET_FAILURE;
	printf("GSNPSID=%08x GAHBCFG=%08x GUSBCFG=%08x GRSTCTL=%08x\n",
	       readl(OTG_REG(0x040)), readl(OTG_REG(0x008)),
	       readl(OTG_REG(0x00c)), readl(OTG_REG(0x010)));
	printf("GINTSTS=%08x GOTGCTL=%08x DCFG=%08x DCTL=%08x DSTS=%08x\n",
	       readl(OTG_REG(0x014)), readl(OTG_REG(0x000)),
	       readl(OTG_REG(0x800)), readl(OTG_REG(0x804)),
	       readl(OTG_REG(0x808)));
	printf("GHWCFG2=%08x GHWCFG3=%08x GHWCFG4=%08x\n",
	       readl(OTG_REG(0x048)), readl(OTG_REG(0x04c)),
	       readl(OTG_REG(0x050)));
	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(tqusb, 1, 1, do_tqusb, "show TQ210 USB OTG register state", "");
