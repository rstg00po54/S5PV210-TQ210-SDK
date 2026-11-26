// SPDX-License-Identifier: GPL-2.0
/* Explicitly destructive to one known scratch block ONLY. */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#define __user
#include "../kernel-6.18/include/uapi/mtd/mtd-abi.h"
#undef __user
#define PAGE 2048U
#define OOB 64U
#define BLOCK 8159U
#define ERASE 131072U
#define START ((uint64_t)BLOCK * ERASE)

static int read_page(int fd, unsigned page, int mode, uint8_t *d,
		     uint8_t *o, struct mtd_read_req *r)
{
	int err;
	memset(r, 0, sizeof(*r));
	r->start = START + (uint64_t)page * PAGE;
	r->len = PAGE;
	r->ooblen = OOB;
	r->usr_data = (uintptr_t)d;
	r->usr_oob = (uintptr_t)o;
	r->mode = mode;
	err = ioctl(fd, MEMREAD, r) < 0 ? errno : 0;
	if (r->len != PAGE || r->ooblen != OOB)
		return EIO;
	return err;
}

static int write_page(int fd, unsigned page, int mode,
		      uint8_t *d, uint8_t *o)
{
	struct mtd_write_req r = {0};
	r.start = START + (uint64_t)page * PAGE;
	r.len = PAGE;
	r.usr_data = (uintptr_t)d;
	r.mode = mode;
	if (o) {
		r.ooblen = OOB;
		r.usr_oob = (uintptr_t)o;
	}
	return ioctl(fd, MEMWRITE, &r);
}

static int blank(int fd)
{
	uint8_t d[PAGE], o[OOB];
	struct mtd_read_req r;
	unsigned int p, i;
	for (p = 0; p < 64; p++) {
		if (read_page(fd, p, MTD_OPS_RAW, d, o, &r))
			return -1;
		for (i = 0; i < PAGE; i++)
			if (d[i] != 0xff)
				return -1;
		for (i = 0; i < OOB; i++)
			if (o[i] != 0xff)
				return -1;
	}
	return 0;
}

static int erase(int fd)
{
	struct erase_info_user64 e = { .start = START, .length = ERASE };
	return ioctl(fd, MEMERASE64, &e);
}

int main(int argc, char **argv)
{
	struct mtd_info_user info;
	struct mtd_read_req r;
	uint8_t expected[PAGE], data[PAGE], injected[PAGE], oob[OOB], parity[OOB];
	int fd, err, rc = 1;
	int64_t off = START;
	unsigned int i;
	const char *stage = "open";

	if (argc != 2 || strcmp(argv[1], "--block-8159")) {
		fprintf(stderr, "Explicit opt-in: %s --block-8159\n", argv[0]);
		return 2;
	}
	fd = open("/dev/mtd0", O_RDWR);
	if (fd < 0) {
		perror("open");
		return 1;
	}
	stage = "geometry/writable";
	if (ioctl(fd, MEMGETINFO, &info) < 0 || info.size != 0x40000000U ||
	    info.erasesize != ERASE || info.writesize != PAGE ||
	    info.oobsize != OOB || !(info.flags & MTD_WRITEABLE))
		goto fail;
	stage = "bad-block/blank guard";
	if (ioctl(fd, MEMGETBADBLOCK, &off) != 0 || blank(fd) || blank(fd))
		goto fail;
	puts("MTD block 8159 blank and good PASS");
	stage = "initial erase";
	if (erase(fd) || blank(fd))
		goto fail;
	puts("MTD erase + blank verify PASS");

	for (i = 0; i < PAGE; i++)
		expected[i] = (uint8_t)(i * 37U + (i >> 3) + 0x5a);
	stage = "ECC program";
	if (write_page(fd, 0, MTD_OPS_PLACE_OOB, expected, NULL))
		goto fail;
	stage = "raw read";
	if (read_page(fd, 0, MTD_OPS_RAW, data, parity, &r) ||
	    memcmp(data, expected, PAGE))
		goto fail;
	for (i = 0; i < 12; i++)
		if (parity[i] != 0xff)
			goto fail;
	for (i = 12; i < OOB && parity[i] == 0xff; i++)
		;
	if (i == OOB)
		goto fail;
	puts("MTD ECC program + raw parity PASS");
	stage = "clean ECC read";
	err = read_page(fd, 0, MTD_OPS_PLACE_OOB, data, oob, &r);
	if (err || r.ecc_stats.uncorrectable_errors ||
	    r.ecc_stats.corrected_bitflips || memcmp(data, expected, PAGE))
		goto fail;
	puts("MTD clean ECC read PASS");

	memcpy(injected, expected, PAGE);
	injected[17] ^= 1;
	for (i = 0; i < 8; i++)
		injected[512 + 23 + i * 17] ^= 1;
	stage = "raw injected program";
	if (write_page(fd, 1, MTD_OPS_RAW, injected, parity))
		goto fail;
	if (read_page(fd, 1, MTD_OPS_RAW, data, oob, &r) ||
	    memcmp(data, injected, PAGE) || memcmp(oob, parity, OOB))
		goto fail;
	stage = "MTD corrected read";
	err = read_page(fd, 1, MTD_OPS_PLACE_OOB, data, oob, &r);
	printf("ECC errno=%d corrected=%u failed=%u max=%u\n", err,
	       r.ecc_stats.corrected_bitflips, r.ecc_stats.uncorrectable_errors,
	       r.ecc_stats.max_bitflips);
	if ((err && err != EUCLEAN) || r.ecc_stats.corrected_bitflips != 9 ||
	    r.ecc_stats.uncorrectable_errors || r.ecc_stats.max_bitflips != 8 ||
	    memcmp(data, expected, PAGE))
		goto fail;
	puts("MTD injected 1+8 bit correction PASS");
	stage = "final erase";
	if (erase(fd) || blank(fd))
		goto fail;
	puts("MTD final erase + 64 pages FF PASS");
	puts("MTD RWTEST PASS");
	rc = 0;
	goto out;
fail:
	fprintf(stderr, "MTD RWTEST FAIL/REFUSED at %s; no automatic cleanup\n", stage);
out:
	close(fd);
	return rc;
}
