// SPDX-License-Identifier: GPL-2.0
/* Deliberately destructive full-chip erase of GOOD blocks only.
 * Run from USB RAM Linux before any UBI attach/mount. No backup.
 */
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
#define ERASE 131072U
#define BLOCKS 8192U

static unsigned char bad[BLOCKS];

static int verify_blank(int fd, unsigned block)
{
	uint8_t data[PAGE], oob[OOB];
	struct mtd_read_req r;
	unsigned p, i;
	for (p = 0; p < ERASE / PAGE; p++) {
		memset(&r, 0, sizeof(r));
		r.start = (uint64_t)block * ERASE + (uint64_t)p * PAGE;
		r.len = PAGE;
		r.ooblen = OOB;
		r.usr_data = (uintptr_t)data;
		r.usr_oob = (uintptr_t)oob;
		r.mode = MTD_OPS_RAW;
		if (ioctl(fd, MEMREAD, &r) || r.len != PAGE || r.ooblen != OOB)
			return -1;
		for (i = 0; i < PAGE; i++)
			if (data[i] != 0xff)
				goto not_blank;
		for (i = 0; i < OOB; i++)
			if (oob[i] != 0xff)
				goto not_blank;
	}
	return 0;
not_blank:
	fprintf(stderr, "raw verify not FF: block=%u page=%u\n", block, p);
	errno = EIO;
	return -1;
}

static int check_name(void)
{
	char name[80];
	FILE *f = fopen("/sys/class/mtd/mtd0/name", "r");
	int ok;
	if (!f)
		return -1;
	ok = fgets(name, sizeof(name), f) &&
	     !strcmp(name, "tq210-nand-bch8\n");
	fclose(f);
	return ok ? 0 : -1;
}

static int block_bad(int fd, unsigned block)
{
	int64_t off = (int64_t)block * ERASE;
	return ioctl(fd, MEMGETBADBLOCK, &off);
}

int main(int argc, char **argv)
{
	struct mtd_info_user info;
	struct erase_info_user64 e;
	unsigned b, skipped = 0, done = 0;
	int fd, rc, result = 1;

	setvbuf(stdout, NULL, _IOLBF, 0);
	if (argc != 2 || strcmp(argv[1], "--erase-all-good-blocks")) {
		fprintf(stderr, "DESTRUCTIVE: %s --erase-all-good-blocks\n", argv[0]);
		return 2;
	}
	if (check_name()) {
		fprintf(stderr, "Refused: expected whole-device tq210-nand-bch8 MTD\n");
		return 1;
	}
	fd = open("/dev/mtd0", O_RDWR);
	if (fd < 0) {
		perror("open");
		return 1;
	}
	if (ioctl(fd, MEMGETINFO, &info) || info.type != MTD_NANDFLASH ||
	    info.size != 0x40000000U || info.writesize != PAGE ||
	    info.erasesize != ERASE || info.oobsize != OOB ||
	    !(info.flags & MTD_WRITEABLE)) {
		fprintf(stderr, "Refused: wrong NAND geometry or read-only MTD\n");
		goto out;
	}

	/* Preflight every block BEFORE any destructive operation. */
	for (b = 0; b < BLOCKS; b++) {
		rc = block_bad(fd, b);
		if (rc < 0) {
			perror("MEMGETBADBLOCK preflight");
			goto out;
		}
		bad[b] = rc != 0;
		if (bad[b]) {
			skipped++;
			printf("Preserve bad block %u offset=0x%08x\n", b, b * ERASE);
		}
	}
	if (skipped == BLOCKS) {
		fprintf(stderr, "Refused: no good blocks\n");
		goto out;
	}
	printf("ERASING %u good blocks; preserving %u bad blocks. NO BACKUP.\n",
	       BLOCKS - skipped, skipped);
	for (b = 0; b < BLOCKS; b++) {
		if (bad[b])
			continue;
		rc = block_bad(fd, b);
		if (rc != 0) {
			fprintf(stderr, "Bad-block status changed at %u; stopping\n", b);
			goto out;
		}
		e.start = (uint64_t)b * ERASE;
		e.length = ERASE;
		if (ioctl(fd, MEMERASE64, &e)) {
			fprintf(stderr, "Erase failed block %u: %s\n", b, strerror(errno));
			goto out;
		}
		if (verify_blank(fd, b)) {
			fprintf(stderr, "Verify failed block %u: %s\n", b, strerror(errno));
			goto out;
		}
		done++;
		if (!(done % 64) || done == BLOCKS - skipped)
			printf("Erased + raw verified %u/%u good blocks\n",
			       done, BLOCKS - skipped);
	}
	for (b = 0; b < BLOCKS; b++) {
		rc = block_bad(fd, b);
		if (rc < 0 || (rc != 0) != bad[b]) {
			fprintf(stderr, "Bad-block map changed at %u\n", b);
			goto out;
		}
	}
	printf("NAND ERASE ALL GOOD PASS: erased=%u preserved_bad=%u\n", done, skipped);
	result = 0;
out:
	if (result)
		fprintf(stderr, "STOP: %u blocks erased and verified; no retry or markbad\n", done);
	close(fd);
	return result;
}
