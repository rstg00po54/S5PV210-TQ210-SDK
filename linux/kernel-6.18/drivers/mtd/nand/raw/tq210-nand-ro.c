// SPDX-License-Identifier: GPL-2.0
/*
 * TQ210 CoreB / S5PV210 raw NAND, initial READ-ONLY port.
 * Register sequence and pin routing from runtime/drivers/nand.c.
 * Optional experimental factory SLC 1-bit ECC, based on Samsung's
 * GPLv2 s3c_nand.c (Copyright (c) 2009 Samsung Electronics Co., Ltd.).
 * Normal MTD operations remain read only. Explicit scratch boot tests
 * can program and erase one checked blank block before MTD registration.
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
#include <linux/slab.h>
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
#define BCH_CONF 0x00
#define BCH_CONT 0x20
#define BCH_STAT 0x30
#define BCH_SECSTAT 0x40
#define BCH_DECODE_DONE BIT(24)
#define BCH_BUSY BIT(31)
#define BCH_INIT BIT(2)
#define BCH_ENCODE BIT(16)
#define BCH_PARITY_OFFSET 12
#define BCH_PARITY_BYTES 13
#define TQ_DIAG_LIMIT 64
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
	bool scratch_test;
	void __iomem *bch;
	const u8 *bch_expected;
	u32 bch_column;
	u32 bch_status;
	u32 bch_secstat;
	u32 bch_erl[4];
	u32 bch_erp[2];
	bool bch_parity_match;
	unsigned int diag_count;
	u32 self_syndrome;
	u32 expected;
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

static u32 tq210_slc_compare(struct tq210_nand *n, u32 parity)
{
	writel((parity & 0xff) | (((parity >> 8) & 0xff) << 16),
	       n->base + NFMECCDATA0);
	writel(((parity >> 16) & 0xff) | (((parity >> 24) & 0xff) << 16),
	       n->base + NFMECCDATA1);
	return readl(n->base + NFMECCERR0);
}

static int tq210_ecc_read_sector(struct tq210_nand *n, u8 *data)
{
	const u8 *stored = n->expected_ecc;
	unsigned int i;

	writel(NF_ENABLE | NF_CE1 | NF_INITMECC, n->base + NFCONT);
	for (i = 0; i < TQ_ECC_STEP; i++)
		data[i] = readb(n->base + NFDATA);
	writel(NF_ENABLE | NF_CE1 | NF_MECCLOCK, n->base + NFCONT);

	n->calculated = readl(n->base + NFMECC0);
	n->expected = (u32)stored[0] | ((u32)stored[1] << 8) |
		      ((u32)stored[2] << 16) | ((u32)stored[3] << 24);
	n->syndrome = tq210_slc_compare(n, n->expected);

	/* Compare the accumulator with itself as a register-path test.
	 * Do not modify RAM data based on unvalidated syndrome locations.
	 */
	n->self_syndrome = tq210_slc_compare(n, n->calculated);
	if (n->expected == 0xffffffffU &&
	    !memchr_inv(data, 0xff, TQ_ECC_STEP))
		return 0;
	if (!(n->syndrome & 3) && n->calculated == n->expected)
		return 0;

	return -EBADMSG;
}

/* Diagnostic only: adapt the tested bare-metal BCH register sequence.
 * Feed parity by NAND random READ, never by writing to NFDATA.
 * Called with CE0 selected, after the normal page read command.
 */
static int tq210_bch_read_sector(struct tq210_nand *n, u8 *data)
{
	u32 saved_conf = readl(n->base + NFCONF);
	u32 saved_bch_conf = readl(n->bch + BCH_CONF);
	u32 saved_bch_cont = readl(n->bch + BCH_CONT) & ~BCH_INIT;
	u8 parity[BCH_PARITY_BYTES];
	unsigned int i;
	int ret;

	writel(NF_ENABLE | NF_CE1 | NF_MECCLOCK, n->base + NFCONT);
	writel(saved_conf | NF_ECC_MODE, n->base + NFCONF);
	writel((511U << 16) | 3U, n->bch + BCH_CONF);
	writel(BCH_DECODE_DONE, n->bch + BCH_STAT);
	writel(saved_bch_cont & ~BCH_ENCODE, n->bch + BCH_CONT);
	writel(NF_ENABLE | NF_CE1 | NF_MECCLOCK | NF_INITMECC,
	       n->base + NFCONT);
	writel((saved_bch_cont & ~BCH_ENCODE) | BCH_INIT,
	       n->bch + BCH_CONT);
	writel(NF_ENABLE | NF_CE1, n->base + NFCONT);

	for (i = 0; i < TQ_ECC_STEP; i++)
		data[i] = readb(n->base + NFDATA);
	writeb(NAND_CMD_RNDOUT, n->base + NFCMD);
	writeb(n->bch_column & 0xff, n->base + NFADDR);
	writeb(n->bch_column >> 8, n->base + NFADDR);
	writeb(NAND_CMD_RNDOUTSTART, n->base + NFCMD);
	udelay(2);
	for (i = 0; i < BCH_PARITY_BYTES; i++)
		parity[i] = readb(n->base + NFDATA);

	writel(NF_ENABLE | NF_CE1 | NF_MECCLOCK, n->base + NFCONT);
	ret = readl_poll_timeout(n->bch + BCH_STAT, n->bch_status,
				 (n->bch_status & (BCH_BUSY | BCH_DECODE_DONE)) ==
				 BCH_DECODE_DONE, 1, 100000);
	n->bch_secstat = readl(n->bch + BCH_SECSTAT);
	for (i = 0; i < ARRAY_SIZE(n->bch_erl); i++)
		n->bch_erl[i] = readl(n->bch + 0xc0 + i * 4);
	for (i = 0; i < ARRAY_SIZE(n->bch_erp); i++)
		n->bch_erp[i] = readl(n->bch + 0xf0 + i * 4);
	n->bch_parity_match = !memcmp(parity, n->bch_expected, sizeof(parity));

	writel(saved_bch_conf, n->bch + BCH_CONF);
	writel(saved_bch_cont, n->bch + BCH_CONT);
	writel(BCH_DECODE_DONE, n->bch + BCH_STAT);
	writel(saved_conf, n->base + NFCONF);
	return ret;
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
			if (n->bch_expected) {
				if (in->ctx.data.len != TQ_ECC_STEP) {
					ret = -EINVAL;
					break;
				}
				ret = tq210_bch_read_sector(n,
							    in->ctx.data.buf.in);
				break;
			}
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


static void tq210_probe_boot_bch(struct nand_chip *chip, const u8 *raw,
				 int page)
{
	struct tq210_nand *n = nand_get_controller_data(chip);
	struct device *dev = nand_to_mtd(chip)->dev.parent;
	u8 data[TQ_ECC_STEP];
	unsigned int step;
	int ret;

	/* Explicit diagnostic samples only, no guessed partition boundary. */
	if (!n->bch || (page != 0 && page != 1 && page != 128))
		return;

	for (step = 0; step < TQ_ECC_STEPS; step++) {
		n->bch_column = 2048 + BCH_PARITY_OFFSET +
				step * BCH_PARITY_BYTES;
		n->bch_expected = chip->oob_poi + BCH_PARITY_OFFSET +
				  step * BCH_PARITY_BYTES;
		n->bch_parity_match = false;
		ret = nand_read_page_op(chip, page, step * TQ_ECC_STEP,
					data, sizeof(data));
		n->bch_expected = NULL;
		dev_info(dev,
			 "BCH8 probe page=%d step=%u ret=%d stat=%08x sec=%08x data_match=%u parity_match=%u ERL=%08x,%08x,%08x,%08x ERP=%08x,%08x\n",
			 page, step, ret, n->bch_status, n->bch_secstat,
			 !ret && !memcmp(data, raw + step * TQ_ECC_STEP,
					 sizeof(data)), n->bch_parity_match,
			 n->bch_erl[0], n->bch_erl[1], n->bch_erl[2],
			 n->bch_erl[3], n->bch_erp[0], n->bch_erp[1]);
	}
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
		if (n->diag_count < TQ_DIAG_LIMIT) {
			n->diag_count++;
			dev_info(mtd->dev.parent,
				 "SLC1 page=%d step=%u stored=%08x calc=%08x xor=%08x syndrome=%08x self=%08x result=%d\n",
				 page, step, n->expected, n->calculated,
				 n->expected ^ n->calculated, n->syndrome,
				 n->self_syndrome, stat);
		}
		if (stat < 0)
			mtd->ecc_stats.failed++;
		else {
			mtd->ecc_stats.corrected += stat;
			max_bitflips = max_t(unsigned int, max_bitflips, stat);
		}
	}
	tq210_probe_boot_bch(chip, buf, page);
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

/* Explicit USB-boot scratch experiment; only before MTD publication.
 * No generic MTD write operation is enabled. Never retry a failed test.
 */
#define TQ_SCRATCH_BLOCK 8159U
#define TQ_SCRATCH_PAGE (TQ_SCRATCH_BLOCK * 64U)
#define TQ_PAGE 2048U
#define TQ_OOB 64U
#define TQ_RAW (TQ_PAGE + TQ_OOB)
#define BCH_ENCODE_DONE BIT(25)

static void tq210_scratch_select(struct tq210_nand *n)
{
	writel(NF_ENABLE | NF_CE1 | NF_MECCLOCK, n->base + NFCONT);
}

static void tq210_scratch_deselect(struct tq210_nand *n)
{
	writel(NF_ENABLE | NF_CE0 | NF_CE1 | NF_MECCLOCK, n->base + NFCONT);
}

static void tq210_scratch_row(struct tq210_nand *n, unsigned int page)
{
	writeb(page & 0xff, n->base + NFADDR);
	writeb((page >> 8) & 0xff, n->base + NFADDR);
	writeb((page >> 16) & 0xff, n->base + NFADDR);
}

static int tq210_scratch_ready(struct tq210_nand *n)
{
	u32 status;

	udelay(2);
	return readl_poll_timeout(n->base + NFSTAT, status,
				  status & NF_READY, 1, 100000);
}

static int tq210_scratch_status(struct tq210_nand *n)
{
	u8 status;
	int ret = tq210_scratch_ready(n);

	if (ret)
		return ret;
	writeb(NAND_CMD_STATUS, n->base + NFCMD);
	udelay(2);
	status = readb(n->base + NFDATA);
	dev_info(nand_to_mtd(&n->chip)->dev.parent, "RWTEST NAND status=%02x\n", status);
	return (status & 0xc1) == 0xc0 ? 0 : -EIO;
}

static int tq210_scratch_blank(struct nand_chip *chip, u8 *raw)
{
	unsigned int i;
	int ret;

	for (i = 0; i < 64; i++) {
		ret = nand_read_page_op(chip, TQ_SCRATCH_PAGE + i, 0, raw, TQ_RAW);
		if (ret)
			return ret;
		if (memchr_inv(raw, 0xff, TQ_RAW))
			return -ENOTEMPTY;
	}
	return 0;
}

static int tq210_scratch_erase(struct tq210_nand *n)
{
	int ret;

	tq210_scratch_select(n);
	ret = tq210_scratch_status(n);
	if (ret)
		goto out;
	writeb(NAND_CMD_ERASE1, n->base + NFCMD);
	tq210_scratch_row(n, TQ_SCRATCH_PAGE);
	writeb(NAND_CMD_ERASE2, n->base + NFCMD);
	ret = tq210_scratch_status(n);
out:
	tq210_scratch_deselect(n);
	return ret;
}

static int tq210_scratch_program(struct tq210_nand *n, unsigned int page,
				  u8 *raw, bool encode)
{
	u32 conf = readl(n->base + NFCONF);
	u32 bconf = readl(n->bch + BCH_CONF);
	u32 bcont = readl(n->bch + BCH_CONT) & ~BCH_INIT;
	u32 status;
	unsigned int step, i;
	int ret;

	if (page != TQ_SCRATCH_PAGE && page != TQ_SCRATCH_PAGE + 1)
		return -EPERM;

	tq210_scratch_select(n);
	ret = tq210_scratch_status(n);
	if (ret)
		goto out;
	writel(conf | NF_ECC_MODE, n->base + NFCONF);
	writeb(NAND_CMD_SEQIN, n->base + NFCMD);
	writeb(0, n->base + NFADDR);
	writeb(0, n->base + NFADDR);
	tq210_scratch_row(n, page);
	udelay(2);
	for (step = 0; step < 4; step++) {
		if (encode) {
			writel((511U << 16) | 3U, n->bch + BCH_CONF);
			writel(BCH_DECODE_DONE | BCH_ENCODE_DONE, n->bch + BCH_STAT);
			writel(BCH_ENCODE, n->bch + BCH_CONT);
			writel(NF_ENABLE | NF_CE1 | NF_MECCLOCK | NF_INITMECC,
			       n->base + NFCONT);
			writel(BCH_ENCODE | BCH_INIT, n->bch + BCH_CONT);
			writel(NF_ENABLE | NF_CE1, n->base + NFCONT);
		}
		for (i = 0; i < TQ_ECC_STEP; i++)
			writeb(raw[step * TQ_ECC_STEP + i], n->base + NFDATA);
		writel(NF_ENABLE | NF_CE1 | NF_MECCLOCK, n->base + NFCONT);
		if (!encode)
			continue;
		ret = readl_poll_timeout(n->bch + BCH_STAT, status,
					 (status & (BCH_BUSY | BCH_ENCODE_DONE)) ==
					 BCH_ENCODE_DONE, 1, 100000);
		if (ret) {
			/* No PAGEPROG issued: abort the unconfirmed input. */
			writeb(NAND_CMD_RESET, n->base + NFCMD);
			(void)tq210_scratch_ready(n);
			goto out;
		}
		for (i = 0; i < BCH_PARITY_BYTES; i++)
			raw[TQ_PAGE + BCH_PARITY_OFFSET + step * BCH_PARITY_BYTES + i] =
				readl(n->bch + 0x90 + (i / 4) * 4) >> ((i % 4) * 8);
	}
	if (encode && !memchr_inv(raw + TQ_PAGE + BCH_PARITY_OFFSET, 0xff, 52)) {
		ret = -EIO;
		writeb(NAND_CMD_RESET, n->base + NFCMD);
		(void)tq210_scratch_ready(n);
		goto out;
	}
	for (i = 0; i < TQ_OOB; i++)
		writeb(raw[TQ_PAGE + i], n->base + NFDATA);
	writeb(NAND_CMD_PAGEPROG, n->base + NFCMD);
	ret = tq210_scratch_status(n);
out:
	tq210_scratch_deselect(n);
	writel(bconf, n->bch + BCH_CONF);
	writel(bcont, n->bch + BCH_CONT);
	writel(BCH_DECODE_DONE | BCH_ENCODE_DONE, n->bch + BCH_STAT);
	writel(conf, n->base + NFCONF);
	return ret;
}

/* Correct scratch data in RAM only, and verify against a known pattern. */
static int tq210_scratch_decode(struct nand_chip *chip, unsigned int page,
				 const u8 *raw, const u8 *expected,
				 unsigned int want_bits)
{
	struct tq210_nand *n = nand_get_controller_data(chip);
	struct device *dev = nand_to_mtd(chip)->dev.parent;
	u8 data[TQ_ECC_STEP];
	unsigned int step, i, count, pos, pat, bits = 0;
	int ret;

	for (step = 0; step < 4; step++) {
		n->bch_column = TQ_PAGE + BCH_PARITY_OFFSET + step * BCH_PARITY_BYTES;
		n->bch_expected = raw + n->bch_column;
		n->bch_parity_match = false;
		ret = nand_read_page_op(chip, page, step * TQ_ECC_STEP, data, sizeof(data));
		n->bch_expected = NULL;
		if (ret)
			return ret;
		count = n->bch_secstat & 15;
		dev_info(dev, "RWTEST decode page=%u sector=%u stat=%08x sec=%08x count=%u\n",
			 page, step, n->bch_status, n->bch_secstat, count);
		if (!n->bch_parity_match ||
		    memcmp(data, raw + step * TQ_ECC_STEP, sizeof(data)) ||
		    count > 8 || (n->bch_secstat & BIT(29)))
			return -EBADMSG;
		for (i = 0; i < count; i++) {
			pos = (n->bch_erl[i / 2] >> ((i % 2) * 16)) & 1023;
			pat = (n->bch_erp[i / 4] >> ((i % 4) * 8)) & 255;
			if (pos >= TQ_ECC_STEP + BCH_PARITY_BYTES || !pat)
				return -EBADMSG;
			bits += hweight8(pat);
			if (pos < TQ_ECC_STEP)
				data[pos] ^= pat;
			dev_info(dev, "RWTEST error position=%u pattern=%02x\n", pos, pat);
		}
		if (memcmp(data, expected + step * TQ_ECC_STEP, sizeof(data)))
			return -EBADMSG;
	}
	return bits == want_bits ? 0 : -EBADMSG;
}

static int tq210_run_scratch(struct nand_chip *chip)
{
	struct tq210_nand *n = nand_get_controller_data(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct device *dev = mtd->dev.parent;
	static const u8 id[] = { 0x01, 0xd3, 0xd1, 0x95, 0x58 };
	u8 *storage, *expected, *injected, *raw;
	unsigned int i;
	int ret;

	if (!n->bch || chip->id.len < sizeof(id) ||
	    memcmp(chip->id.data, id, sizeof(id)))
		return -ENODEV;

	storage = kmalloc(3 * TQ_RAW, GFP_KERNEL);
	if (!storage)
		return -ENOMEM;
	expected = storage;
	injected = storage + TQ_RAW;
	raw = storage + 2 * TQ_RAW;

	dev_warn(dev, "RWTEST opt-in: ONLY block %u offset=%08x; no backup\n",
		 TQ_SCRATCH_BLOCK, TQ_SCRATCH_BLOCK * 131072U);
	/* NAND core has already scanned bad blocks. Check its result, then
	 * require two full raw passes including every OOB byte to be blank.
	 */
	ret = mtd->_block_isbad(mtd, (loff_t)TQ_SCRATCH_BLOCK * 131072);
	if (ret) {
		ret = ret < 0 ? ret : -EIO;
		goto free;
	}
	nand_select_target(chip, 0);
	ret = tq210_scratch_blank(chip, raw);
	if (!ret)
		ret = tq210_scratch_blank(chip, raw);
	if (ret)
		goto finish;
	dev_info(dev, "RWTEST blank block checked twice PASS\n");
	ret = tq210_scratch_erase(n);
	if (!ret)
		ret = tq210_scratch_blank(chip, raw);
	if (ret)
		goto finish;
	dev_info(dev, "RWTEST initial erase + raw blank verify PASS\n");

	for (i = 0; i < TQ_PAGE; i++)
		expected[i] = (u8)(i * 37U + (i >> 3) + 0x5a);
	memset(expected + TQ_PAGE, 0xff, TQ_OOB);
	ret = tq210_scratch_program(n, TQ_SCRATCH_PAGE, expected, true);
	if (!ret)
		ret = nand_read_page_op(chip, TQ_SCRATCH_PAGE, 0, raw, TQ_RAW);
	if (!ret && memcmp(expected, raw, TQ_RAW))
		ret = -EIO;
	if (ret)
		goto finish;
	dev_info(dev, "RWTEST BCH8 encode + program + raw page verify PASS\n");
	ret = tq210_scratch_decode(chip, TQ_SCRATCH_PAGE, raw, expected, 0);
	if (ret)
		goto finish;
	dev_info(dev, "RWTEST clean BCH8 decode PASS\n");

	memcpy(injected, expected, TQ_RAW);
	injected[17] ^= 1;
	for (i = 0; i < 8; i++)
		injected[512 + 23 + i * 17] ^= 1;
	ret = tq210_scratch_program(n, TQ_SCRATCH_PAGE + 1, injected, false);
	if (!ret)
		ret = nand_read_page_op(chip, TQ_SCRATCH_PAGE + 1, 0, raw, TQ_RAW);
	if (!ret && memcmp(injected, raw, TQ_RAW))
		ret = -EIO;
	if (ret)
		goto finish;
	ret = tq210_scratch_decode(chip, TQ_SCRATCH_PAGE + 1, raw, expected, 9);
	if (ret)
		goto finish;
	dev_info(dev, "RWTEST BCH8 injected 1+8 bits corrected in RAM PASS\n");

	ret = tq210_scratch_erase(n);
	if (!ret)
		ret = tq210_scratch_blank(chip, raw);
	if (!ret)
		dev_info(dev, "RWTEST final erase + 64 raw pages FF PASS\n");
finish:
	nand_deselect_target(chip);
	tq210_scratch_deselect(n);
free:
	kfree(storage);
	return ret;
}

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
	n->scratch_test = of_property_read_bool(dev->of_node,
					       "embedsky,experimental-bch8-rwtest");
	if (n->hw_ecc && n->scratch_test)
		return -EINVAL;
	if ((n->hw_ecc || n->scratch_test) &&
	    platform_get_resource(pdev, IORESOURCE_MEM, 1)) {
		n->bch = devm_platform_ioremap_resource(pdev, 1);
		if (IS_ERR(n->bch))
			return PTR_ERR(n->bch);
	}
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

	if (n->scratch_test) {
		ret = tq210_run_scratch(chip);
		if (ret)
			dev_err(dev, "RWTEST FAIL/REFUSED ret=%d; no automatic retry or cleanup erase\n", ret);
		else
			dev_info(dev, "NAND RWTEST PASS\n");
	}
	mtd->flags &= ~MTD_WRITEABLE;
	ret = mtd_device_register(mtd, NULL, 0);
	if (ret) {
		nand_cleanup(chip);
		return ret;
	}
	platform_set_drvdata(pdev, n);
	dev_warn(dev, "1 GiB NAND registered READ ONLY; %s\n",
		 n->hw_ecc ? "experimental 1-bit/512B ECC CHECK ONLY, OOB 40..55" :
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
