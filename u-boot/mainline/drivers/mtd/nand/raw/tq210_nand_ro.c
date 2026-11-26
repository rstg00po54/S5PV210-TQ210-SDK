// SPDX-License-Identifier: GPL-2.0+
/* TQ210 CoreB: read-only S5PV210 NAND, BCH8/512, OOB 12..63.
 * Adapted from the hardware-tested Linux TQ210 driver in this SDK.
 * No NAND program, erase or set-features command can reach the bus.
 */
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/mtd/rawnand.h>
#include <linux/string.h>
#include <nand.h>
#include <stdio.h>

#define NF 0xb0e00000UL
#define BCH 0xb0e20000UL
#define NFCONF (NF + 0x00)
#define NFCONT (NF + 0x04)
#define NFCMD  (NF + 0x08)
#define NFADDR (NF + 0x0c)
#define NFDATA (NF + 0x10)
#define NFSTAT (NF + 0x28)
#define SELECT (BIT(0) | BIT(2) | BIT(7))
#define DESELECT (SELECT | BIT(1))
#define PAGE 2048
#define OOB 64
#define STEP 512
#define PAR 13
#define PAGES 524288
static struct nand_chip tq_chip;
static struct nand_ecclayout tq_layout = {
	.eccbytes = 52, .oobavail = 10,
	.oobfree = { { .offset = 2, .length = 10 } },
};
static u8 raw[PAGE + OOB];
static int io_error;

static int wait_mask(unsigned long reg, u32 mask, u32 expected)
{
	unsigned int i;
	for (i = 0; i < 100000; i++) {
		if ((readl(reg) & mask) == expected)
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;
}

static void select_chip(struct mtd_info *mtd, int chip)
{
	writel(chip == 0 ? SELECT : DESELECT, NFCONT);
	if (chip == 0)
		io_error = 0;
}

static int ready(struct mtd_info *mtd)
{
	return !!(readl(NFSTAT) & BIT(0));
}

static void col_addr(unsigned int col)
{
	writeb(col & 255, NFADDR);
	writeb(col >> 8, NFADDR);
}

/* Only these commands may be sent, including during NAND identification. */
static void command(struct mtd_info *mtd, unsigned int cmd, int col, int page)
{
	if (cmd == NAND_CMD_READOOB) {
		cmd = NAND_CMD_READ0;
		col += PAGE;
	}
	switch (cmd) {
	case NAND_CMD_READ0:
		if (col == -1 && page == -1) {
			writeb(cmd, NFCMD);
			return;
		}
		if (page < 0 || page >= PAGES || col < 0 || col >= PAGE + OOB)
			goto denied;
		writeb(cmd, NFCMD);
		col_addr(col);
		writeb(page & 255, NFADDR);
		writeb((page >> 8) & 255, NFADDR);
		writeb(page >> 16, NFADDR);
		writeb(NAND_CMD_READSTART, NFCMD);
		break;
	case NAND_CMD_RNDOUT:
		if (col < 0 || col >= PAGE + OOB)
			goto denied;
		writeb(cmd, NFCMD);
		col_addr(col);
		writeb(NAND_CMD_RNDOUTSTART, NFCMD);
		udelay(2);
		return;
	case NAND_CMD_READID:
	case NAND_CMD_PARAM:
	case NAND_CMD_GET_FEATURES:
		if (col < 0 || col > 255)
			goto denied;
		writeb(cmd, NFCMD);
		writeb(col, NFADDR);
		if (cmd != NAND_CMD_PARAM) {
			udelay(2);
			return;
		}
		break;
	case NAND_CMD_STATUS:
		writeb(cmd, NFCMD);
		udelay(2);
		return;
	case NAND_CMD_RESET:
		writeb(cmd, NFCMD);
		break;
	default:
		goto denied;
	}
	udelay(2);
	if (wait_mask(NFSTAT, BIT(0), BIT(0)))
		io_error = -ETIMEDOUT;
	return;
denied:
	io_error = -EROFS;
	printf("TQ210 NAND: denied command %02x\n", cmd);
}

static u8 read_byte(struct mtd_info *mtd)
{
	return io_error ? 0xff : readb(NFDATA);
}

static void read_buf(struct mtd_info *mtd, u8 *buf, int len)
{
	while (len--)
		*buf++ = read_byte(mtd);
}

static void deny_byte(struct mtd_info *mtd, u8 value)
{
	io_error = -EROFS;
}

static void deny_buf(struct mtd_info *mtd, const u8 *buf, int len)
{
	io_error = -EROFS;
}

static int deny_page(struct mtd_info *mtd, struct nand_chip *chip,
		     const u8 *buf, int oob_required, int page)
{
	return -EROFS;
}

static int deny_oob(struct mtd_info *mtd, struct nand_chip *chip, int page)
{
	return -EROFS;
}

static int deny_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	return -EROFS;
}

static int deny_markbad(struct mtd_info *mtd, loff_t ofs)
{
	return -EROFS;
}

static int page_read(struct mtd_info *mtd, int page, int col, u8 *buf, int len)
{
	command(mtd, NAND_CMD_READ0, col, page);
	if (io_error)
		return io_error;
	read_buf(mtd, buf, len);
	return io_error;
}

static int read_oob(struct mtd_info *mtd, struct nand_chip *chip, int page)
{
	return page_read(mtd, page, PAGE, chip->oob_poi, OOB);
}

static int read_raw(struct mtd_info *mtd, struct nand_chip *chip,
		    u8 *buf, int oob_required, int page)
{
	int ret = page_read(mtd, page, 0, raw, sizeof(raw));
	if (ret)
		return ret;
	memcpy(buf, raw, PAGE);
	if (oob_required)
		memcpy(chip->oob_poi, raw + PAGE, OOB);
	return 0;
}

/* Same first/second-page marker policy as the Linux port. */
static int block_bad(struct mtd_info *mtd, loff_t ofs)
{
	u8 marker;
	int page = (ofs / (128 * 1024)) * 64;
	int i, ret;
	for (i = 0; i < 2; i++) {
		ret = page_read(mtd, page + i, PAGE, &marker, 1);
		if (ret)
			return ret;
		if (marker != 0xff)
			return 1;
	}
	return 0;
}

static int decode(struct mtd_info *mtd, int page, int step, u8 *data,
		  const u8 *original, u8 *parity)
{
	u32 conf = readl(NFCONF);
	u32 bconf = readl(BCH);
	u32 bcont = readl(BCH + 0x20) & ~BIT(2);
	u32 sec, erl[4], erp[2];
	u8 actual[PAR];
	unsigned int i, j, count, bits = 0, pos[8], pat[8];
	int ret;
	command(mtd, NAND_CMD_READ0, step * STEP, page);
	if (io_error)
		return io_error;
	writel(SELECT, NFCONT);
	writel(conf | (3U << 23), NFCONF);
	writel((511U << 16) | 3, BCH);
	writel(BIT(24), BCH + 0x30);
	writel(bcont & ~BIT(16), BCH + 0x20);
	writel(SELECT | BIT(5), NFCONT);
	writel((bcont & ~BIT(16)) | BIT(2), BCH + 0x20);
	writel(SELECT & ~BIT(7), NFCONT);
	read_buf(mtd, data, STEP);
	command(mtd, NAND_CMD_RNDOUT, PAGE + 12 + step * PAR, -1);
	read_buf(mtd, actual, PAR);
	writel(SELECT, NFCONT);
	ret = wait_mask(BCH + 0x30, BIT(31) | BIT(24), BIT(24));
	sec = readl(BCH + 0x40);
	for (i = 0; i < 4; i++)
		erl[i] = readl(BCH + 0xc0 + 4 * i);
	for (i = 0; i < 2; i++)
		erp[i] = readl(BCH + 0xf0 + 4 * i);
	writel(bconf, BCH);
	writel(bcont, BCH + 0x20);
	writel(BIT(24), BCH + 0x30);
	writel(conf, NFCONF);
	if (ret || io_error)
		return ret ? ret : io_error;
	if (memcmp(data, original, STEP) || memcmp(actual, parity, PAR))
		return -EIO;
	count = sec & 15;
	if (count > 8 || (sec & BIT(29)))
		goto erased;
	for (i = 0; i < count; i++) {
		pos[i] = (erl[i / 2] >> ((i % 2) * 16)) & 1023;
		pat[i] = (erp[i / 4] >> ((i % 4) * 8)) & 255;
		if (pos[i] >= STEP + PAR || !pat[i])
			goto erased;
		for (j = 0; j < i; j++)
			if (pos[i] == pos[j])
				goto erased;
		bits += hweight8(pat[i]);
	}
	if (bits > 8)
		goto erased;
	for (i = 0; i < count; i++)
		if (pos[i] < STEP)
			data[pos[i]] ^= pat[i];
	return bits;
erased:
	bits = 0;
	for (i = 0; i < STEP; i++)
		bits += hweight8((u8)~data[i]);
	for (i = 0; i < PAR; i++)
		bits += hweight8((u8)~parity[i]);
	if (bits > 8)
		return -EBADMSG;
	memset(data, 0xff, STEP);
	return bits;
}

static int read_bch(struct mtd_info *mtd, struct nand_chip *chip,
		    u8 *buf, int oob_required, int page)
{
	unsigned int step, i, maxbits = 0;
	int ret = page_read(mtd, page, 0, raw, sizeof(raw));
	if (ret)
		return ret;
	memcpy(chip->oob_poi, raw + PAGE, OOB);
	for (step = 0; step < 4; step++) {
		u8 *data = buf + step * STEP;
		u8 *original = raw + step * STEP;
		u8 *parity = raw + PAGE + 12 + step * PAR;
		bool blank = true;
		memcpy(data, original, STEP);
		for (i = 0; i < STEP; i++)
			if (data[i] != 0xff)
				blank = false;
		for (i = 0; i < PAR; i++)
			if (parity[i] != 0xff)
				blank = false;
		if (blank)
			continue;
		ret = decode(mtd, page, step, data, original, parity);
		if (ret == -EBADMSG) {
			mtd->ecc_stats.failed++;
			printf("TQ210 BCH8 uncorrectable page=%d step=%u\n", page, step);
			continue;
		}
		if (ret < 0)
			return ret;
		mtd->ecc_stats.corrected += ret;
		if (ret > maxbits)
			maxbits = ret;
	}
	return maxbits;
}

void board_nand_init(void)
{
	struct nand_chip *chip = &tq_chip;
	struct mtd_info *mtd = nand_to_mtd(chip);
	static const u8 expected[] = { 0x01, 0xd3, 0xd1, 0x95, 0x58 };
	u8 id[5];
	int i, ret;

	setbits_le32(0xe0100464UL, BIT(28));
	writel(0x22222222, 0xe0200320UL);
	writel(0x22222222, 0xe0200380UL);
	clrsetbits_le32(0xe02002e0UL, 15U << 8, 3U << 8);
	writel(DESELECT, NFCONT);
	writel((3U << 23) | (7U << 12) | (7U << 8) | (7U << 4) | BIT(1), NFCONF);
	udelay(10);
	select_chip(mtd, 0);
	command(mtd, NAND_CMD_RESET, -1, -1);
	command(mtd, NAND_CMD_READID, 0, -1);
	read_buf(mtd, id, sizeof(id));
	select_chip(mtd, -1);
	printf("TQ210 NAND ID %02x %02x %02x %02x %02x\n",
	       id[0], id[1], id[2], id[3], id[4]);
	if (io_error || memcmp(id, expected, sizeof(id))) {
		printf("TQ210 NAND: unsupported ID or timeout\n");
		return;
	}
	chip->IO_ADDR_R = chip->IO_ADDR_W = (void __iomem *)NFDATA;
	chip->select_chip = select_chip;
	chip->cmdfunc = command;
	chip->dev_ready = ready;
	chip->read_byte = read_byte;
	chip->read_buf = read_buf;
	chip->write_byte = deny_byte;
	chip->write_buf = deny_buf;
	chip->block_bad = block_bad;
	chip->block_markbad = deny_markbad;
	chip->options = NAND_ROM | NAND_NO_SUBPAGE_WRITE | NAND_SKIP_BBTSCAN;
	ret = nand_scan_ident(mtd, 1, NULL);
	if (ret || io_error || mtd->size != 0x40000000ULL ||
	    mtd->writesize != PAGE || mtd->oobsize != OOB ||
	    mtd->erasesize != 131072) {
		printf("TQ210 NAND geometry/scan failed: %d\n", ret);
		return;
	}
	chip->options |= NAND_ROM | NAND_NO_SUBPAGE_WRITE | NAND_SKIP_BBTSCAN;
	chip->bbt_options &= ~NAND_BBT_USE_FLASH;
	for (i = 0; i < 52; i++)
		tq_layout.eccpos[i] = 12 + i;
	chip->ecc.mode = NAND_ECC_HW;
	chip->ecc.size = STEP;
	chip->ecc.bytes = PAR;
	chip->ecc.strength = 8;
	chip->ecc.layout = &tq_layout;
	chip->ecc.options = NAND_ECC_CUSTOM_PAGE_ACCESS;
	chip->ecc.read_page = read_bch;
	chip->ecc.read_page_raw = read_raw;
	chip->ecc.write_page = deny_page;
	chip->ecc.write_page_raw = deny_page;
	chip->ecc.read_oob = read_oob;
	chip->ecc.write_oob = deny_oob;
	ret = nand_scan_tail(mtd);
	if (ret) {
		printf("TQ210 NAND scan tail failed: %d\n", ret);
		return;
	}
	mtd->flags &= ~MTD_WRITEABLE;
	mtd->_erase = deny_erase;
	mtd->_block_markbad = deny_markbad;
	ret = nand_register(0, mtd);
	if (ret)
		printf("TQ210 NAND registration failed: %d\n", ret);
	else
		printf("TQ210 NAND: BCH8/512 OOB 12..63 READ ONLY\n");
}
