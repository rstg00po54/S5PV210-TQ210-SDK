// SPDX-License-Identifier: GPL-2.0
/*
 * TQ210 CoreB / S5PV210 raw NAND, initial READ-ONLY port.
 * Register sequence and pin routing from runtime/drivers/nand.c.
 * No ECC correction yet; never issue program/erase or data-out cycles.
 */
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>

#define NFCONF 0x00
#define NFCONT 0x04
#define NFCMD  0x08
#define NFADDR 0x0c
#define NFDATA 0x10
#define NFSTAT 0x28
#define NF_ENABLE BIT(0)
#define NF_CE0 BIT(1)
#define NF_CE1 BIT(2)
#define NF_READY BIT(0)
#define NF_TIMING ((3U << 23) | (7U << 12) | (7U << 8) | (7U << 4) | BIT(1))

struct tq210_nand {
	struct nand_controller controller;
	struct nand_chip chip;
	void __iomem *base;
};

static bool tq210_read_command(u8 cmd)
{
	switch (cmd) {
	case NAND_CMD_READ0:
	case NAND_CMD_READ1:
	case NAND_CMD_READOOB:
	case NAND_CMD_READSTART:
	case NAND_CMD_RNDOUT:
	case NAND_CMD_RNDOUTSTART:
	case NAND_CMD_READID:
	case NAND_CMD_STATUS:
	case NAND_CMD_RESET:
	case NAND_CMD_PARAM:
	case NAND_CMD_GET_FEATURES:
		return true;
	default:
		return false;
	}
}

static int tq210_exec_op(struct nand_chip *chip,
			const struct nand_operation *op, bool check_only)
{
	struct tq210_nand *n = nand_get_controller_data(chip);
	unsigned int i, j;
	u32 status;
	int ret = 0;

	if (op->cs || op->deassert_wp)
		return -EOPNOTSUPP;

	/* Validate the entire operation before selecting the device. */
	for (i = 0; i < op->ninstrs; i++) {
		const struct nand_op_instr *in = &op->instrs[i];

		switch (in->type) {
		case NAND_OP_CMD_INSTR:
			if (!tq210_read_command(in->ctx.cmd.opcode))
				return -EOPNOTSUPP;
			break;
		case NAND_OP_ADDR_INSTR:
			if (in->ctx.addr.naddrs > 5)
				return -EOPNOTSUPP;
			break;
		case NAND_OP_DATA_IN_INSTR:
		case NAND_OP_WAITRDY_INSTR:
			break;
		default:
			return -EOPNOTSUPP;
		}
	}
	if (check_only)
		return 0;

	writel(NF_ENABLE | NF_CE1, n->base + NFCONT);
	for (i = 0; i < op->ninstrs; i++) {
		const struct nand_op_instr *in = &op->instrs[i];

		switch (in->type) {
		case NAND_OP_CMD_INSTR:
			writeb(in->ctx.cmd.opcode, n->base + NFCMD);
			break;
		case NAND_OP_ADDR_INSTR:
			for (j = 0; j < in->ctx.addr.naddrs; j++)
				writeb(in->ctx.addr.addrs[j], n->base + NFADDR);
			break;
		case NAND_OP_DATA_IN_INSTR:
			/* Raw 8-bit data, including spare area when requested. */
			for (j = 0; j < in->ctx.data.len; j++)
				((u8 *)in->ctx.data.buf.in)[j] =
					readb(n->base + NFDATA);
			break;
		case NAND_OP_WAITRDY_INSTR:
			/* tWB guard, then bounded R/B polling (NFSTAT bit 0). */
			udelay(2);
			ret = readl_poll_timeout(n->base + NFSTAT, status,
						 status & NF_READY, 1,
						 max(1U, in->ctx.waitrdy.timeout_ms) * 1000UL);
			break;
		default:
			ret = -EOPNOTSUPP;
			break;
		}
		if (ret)
			break;
		if (in->delay_ns)
			ndelay(in->delay_ns);
	}
	writel(NF_ENABLE | NF_CE0 | NF_CE1, n->base + NFCONT);
	return ret;
}

static int tq210_attach_chip(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);

	/* Do not guess the factory ECC layout. Expose raw data for diagnostics. */
	chip->ecc.engine_type = NAND_ECC_ENGINE_TYPE_NONE;
	if (mtd->writesize != 2048 || mtd->oobsize != 64 ||
	    mtd->erasesize != SZ_128K || mtd->size != SZ_1G)
		return -ENODEV;
	return 0;
}

static const struct nand_controller_ops tq210_nand_ops = {
	.attach_chip = tq210_attach_chip,
	.exec_op = tq210_exec_op,
};

static int tq210_nand_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tq210_nand *n;
	struct nand_chip *chip;
	struct mtd_info *mtd;
	struct clk *clk;
	int ret;

	n = devm_kzalloc(dev, sizeof(*n), GFP_KERNEL);
	if (!n)
		return -ENOMEM;

	n->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(n->base))
		return PTR_ERR(n->base);

	clk = devm_clk_get_enabled(dev, "nand");
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "NAND clock unavailable\n");

	writel(NF_ENABLE | NF_CE0 | NF_CE1, n->base + NFCONT);
	writel(NF_TIMING, n->base + NFCONF);
	dev_info(dev, "NFCONF=%08x NFCONT=%08x NFSTAT=%08x clock=%lu Hz\n",
		 readl(n->base + NFCONF), readl(n->base + NFCONT),
		 readl(n->base + NFSTAT), clk_get_rate(clk));

	nand_controller_init(&n->controller);
	n->controller.ops = &tq210_nand_ops;
	chip = &n->chip;
	chip->controller = &n->controller;
	chip->options |= NAND_ROM;
	chip->ecc.engine_type = NAND_ECC_ENGINE_TYPE_NONE;
	nand_set_controller_data(chip, n);
	nand_set_flash_node(chip, dev->of_node);
	mtd = nand_to_mtd(chip);
	mtd->dev.parent = dev;
	mtd->name = "tq210-nand-raw-ro";

	/* Standard NAND identification and in-memory bad-block scan.
	 * No flash BBT is requested. The exec_op whitelist also blocks writes.
	 */
	ret = nand_scan(chip, 1);
	if (ret)
		return dev_err_probe(dev, ret, "read-only NAND scan failed\n");

	mtd->flags &= ~MTD_WRITEABLE;
	ret = mtd_device_register(mtd, NULL, 0);
	if (ret) {
		nand_cleanup(chip);
		return ret;
	}
	platform_set_drvdata(pdev, n);
	dev_warn(dev, "1 GiB NAND registered READ ONLY; ECC correction disabled\n");
	return 0;
}

static void tq210_nand_remove(struct platform_device *pdev)
{
	struct tq210_nand *n = platform_get_drvdata(pdev);

	WARN_ON(mtd_device_unregister(nand_to_mtd(&n->chip)));
	nand_cleanup(&n->chip);
	writel(NF_ENABLE | NF_CE0 | NF_CE1, n->base + NFCONT);
}

static const struct of_device_id tq210_nand_of_match[] = {
	{ .compatible = "embedsky,tq210-nand-ro" },
	{ }
};
MODULE_DEVICE_TABLE(of, tq210_nand_of_match);

static struct platform_driver tq210_nand_driver = {
	.probe = tq210_nand_probe,
	.remove = tq210_nand_remove,
	.driver = {
		.name = "tq210-nand-ro",
		.of_match_table = tq210_nand_of_match,
	},
};
module_platform_driver(tq210_nand_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TQ210 S5PV210 read-only raw NAND diagnostics");
