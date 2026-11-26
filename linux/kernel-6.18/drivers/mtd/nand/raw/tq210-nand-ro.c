// SPDX-License-Identifier: GPL-2.0
/*
 * TQ210 CoreB / S5PV210 raw NAND, initial READ-ONLY port.
 * Register sequence and pin routing from runtime/drivers/nand.c.
 * Optional experimental factory SLC 1-bit ECC, based on Samsung's
 * GPLv2 s3c_nand.c (Copyright (c) 2009 Samsung Electronics Co., Ltd.).
 * Never issue program/erase or data-out cycles.
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
#include <linux/string.h>

#define NFCONF 0x00
#define NFCONT 0x04
#define NFCMD  0x08
#define NFADDR 0x0c
#define NFDATA 0x10
#define NFMECCDATA0 0x14
#define NFMECCDATA1 0x18
#define NFSTAT 0x28
#define NFMECCERR0 0x2c
#define NFMECC0 0x34
#define NF_ECC_MODE (3U << 23)
#define NF_INITMECC BIT(5)
#define NF_MECCLOCK BIT(7)
#define TQ_ECC_STEP 512
#define TQ_ECC_BYTES 4
#define TQ_ECC_OFFSET 40
#define TQ_ECC_STEPS 4
#define NF_ENABLE BIT(0)
#define NF_CE0 BIT(1)
#define NF_CE1 BIT(2)
#define NF_READY BIT(0)
#define NF_TIMING ((3U << 23) | (7U << 12) | (7U << 8) | (7U << 4) | BIT(1))

struct tq210_nand {
	struct nand_controller controller;
	struct nand_chip chip;
	void __iomem *base;
	bool hw_ecc;
	const u8 *expected_ecc;
	int ecc_result;
	u32 calculated;
	u32 syndrome;
};


/* Only the SLC data parity is interpreted. Bytes 56..63 stay reserved,
 * including the old driver's spare-area parity at 60..63.
 */
static int tq210_oob_ecc(struct mtd_info *mtd, int section,
			  struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;
	region->offset = TQ_ECC_OFFSET;
	region->length = TQ_ECC_STEPS * TQ_ECC_BYTES;
	return 0;
}

static int tq210_oob_free(struct mtd_info *mtd, int section,
			   struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;
	region->offset = 2;
	region->length = 38;
	return 0;
}

static const struct mtd_ooblayout_ops tq210_ooblayout = {
	.ecc = tq210_oob_ecc,
	.free = tq210_oob_free,
};

static int tq210_ecc_read_sector(struct tq210_nand *n, u8 *data)
{
	u8 stored[TQ_ECC_BYTES];
	u32 byte, bit, expected;
	unsigned int i;
	int erased;

	memcpy(stored, n->expected_ecc, sizeof(stored));
	/* The controller accumulates ECC only during the 512 data bytes. */
	writel(NF_ENABLE | NF_CE1 | NF_INITMECC, n->base + NFCONT);
	for (i = 0; i < TQ_ECC_STEP; i++)
		data[i] = readb(n->base + NFDATA);
	writel(NF_ENABLE | NF_CE1 | NF_MECCLOCK, n->base + NFCONT);

	n->calculated = readl(n->base + NFMECC0);
	expected = (u32)stored[0] | ((u32)stored[1] << 8) |
		   ((u32)stored[2] << 16) | ((u32)stored[3] << 24);

	/* These are ECC comparison registers, NOT the NAND data register. */
	writel((u32)stored[0] | ((u32)stored[1] << 16),
	       n->base + NFMECCDATA0);
	writel((u32)stored[2] | ((u32)stored[3] << 16),
	       n->base + NFMECCDATA1);
	n->syndrome = readl(n->base + NFMECCERR0);

	/* Erased sectors may have no programmed ECC. Check the actual data
	 * and parity bit count, never assume an all-FF parity means blank.
	 */
	if (expected == 0xffffffffU) {
		erased = nand_check_erased_ecc_chunk(data, TQ_ECC_STEP,
						   stored, TQ_ECC_BYTES,
						   NULL, 0, 1);
		if (erased >= 0)
			return erased;
	}

	switch (n->syndrome & 3) {
	case 0:
		/* The initial port uses a conservative exact parity check too. */
		return n->calculated == expected ? 0 : -EBADMSG;
	case 1:
		byte = (n->syndrome >> 7) & 0x7ff;
		bit = (n->syndrome >> 4) & 7;
		if (byte >= TQ_ECC_STEP || n->calculated == expected)
			return -EBADMSG;
		data[byte] ^= BIT(bit);
		return 1;
	default:
		return -EBADMSG;
	}
}

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

	writel(NF_ENABLE | NF_CE1 | (n->hw_ecc ? NF_MECCLOCK : 0),
	       n->base + NFCONT);
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
			if (n->expected_ecc) {
				if (in->ctx.data.len != TQ_ECC_STEP) {
					ret = -EINVAL;
					break;
				}
				n->ecc_result = tq210_ecc_read_sector(n,
							     in->ctx.data.buf.in);
				break;
			}
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
	writel(NF_ENABLE | NF_CE0 | NF_CE1 |
	       (n->hw_ecc ? NF_MECCLOCK : 0), n->base + NFCONT);
	return ret;
}


static int tq210_read_page_ecc(struct nand_chip *chip, u8 *buf,
			       int oob_required, int page)
{
	struct tq210_nand *n = nand_get_controller_data(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	unsigned int step, max_bitflips = 0;
	int ret, stat;

	/* Fetch unmodified OOB. Spare metadata ECC is not implemented yet.
	 * Independently address each sector rather than rely on CE continuity.
	 */
	ret = nand_read_oob_op(chip, page, 0, chip->oob_poi, mtd->oobsize);
	if (ret)
		return ret;

	for (step = 0; step < TQ_ECC_STEPS; step++) {
		n->expected_ecc = chip->oob_poi + TQ_ECC_OFFSET +
				  step * TQ_ECC_BYTES;
		n->ecc_result = -EIO;
		ret = nand_read_page_op(chip, page, step * TQ_ECC_STEP,
					buf + step * TQ_ECC_STEP, TQ_ECC_STEP);
		n->expected_ecc = NULL;
		if (ret)
			return ret;
		stat = n->ecc_result;
		if (stat < 0) {
			mtd->ecc_stats.failed++;
			dev_warn_ratelimited(mtd->dev.parent,
				"ECC mismatch page=%d sector=%u calculated=%08x syndrome=%08x\n",
				page, step, n->calculated, n->syndrome);
		} else {
			mtd->ecc_stats.corrected += stat;
			max_bitflips = max_t(unsigned int, max_bitflips, stat);
		}
	}
	return max_bitflips;
}

static int tq210_write_page_denied(struct nand_chip *chip, const u8 *buf,
				   int oob_required, int page)
{
	return -EROFS;
}

static int tq210_attach_chip(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct tq210_nand *n = nand_get_controller_data(chip);
	static const u8 expected_id[] = { 0x01, 0xd3, 0xd1, 0x95, 0x58 };

	if (mtd->writesize != 2048 || mtd->oobsize != 64 ||
	    mtd->erasesize != SZ_128K || mtd->size != SZ_1G)
		return -ENODEV;
	chip->ecc.engine_type = NAND_ECC_ENGINE_TYPE_NONE;
	if (!n->hw_ecc)
		return 0;
	if (chip->id.len < sizeof(expected_id) ||
	    memcmp(chip->id.data, expected_id, sizeof(expected_id)))
		return -ENODEV;

	chip->options |= NAND_NO_SUBPAGE_WRITE;
	chip->ecc.engine_type = NAND_ECC_ENGINE_TYPE_ON_HOST;
	chip->ecc.placement = NAND_ECC_PLACEMENT_OOB;
	chip->ecc.algo = NAND_ECC_ALGO_HAMMING;
	chip->ecc.size = TQ_ECC_STEP;
	chip->ecc.bytes = TQ_ECC_BYTES;
	chip->ecc.strength = 1;
	chip->ecc.read_page = tq210_read_page_ecc;
	chip->ecc.write_page = tq210_write_page_denied;
	chip->ecc.write_page_raw = tq210_write_page_denied;
	mtd_set_ooblayout(mtd, &tq210_ooblayout);
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

	n->hw_ecc = of_property_read_bool(dev->of_node,
					 "embedsky,experimental-slc-ecc");
	writel(NF_ENABLE | NF_CE0 | NF_CE1 |
	       (n->hw_ecc ? NF_MECCLOCK : 0), n->base + NFCONT);
	writel(n->hw_ecc ? NF_TIMING & ~NF_ECC_MODE : NF_TIMING,
	       n->base + NFCONF);
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
	mtd->name = n->hw_ecc ? "tq210-nand-ecc-ro" : "tq210-nand-raw-ro";

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
	dev_warn(dev, "1 GiB NAND registered READ ONLY; %s\n",
		 n->hw_ecc ? "experimental 1-bit/512B ECC, OOB 40..55" :
			     "ECC correction disabled");
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
