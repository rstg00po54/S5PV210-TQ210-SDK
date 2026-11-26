// SPDX-License-Identifier: GPL-2.0
/* Only program four pages of an entirely blank first block, no erase. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#define __user
#include "../kernel-6.18/include/uapi/mtd/mtd-abi.h"
#undef __user

#define PAGE 2048U
#define OOB 64U
#define BLOCK 131072U
static uint8_t image[8192], data[PAGE], oob[OOB];

static int attr(int i, const char *name, char *out, size_t len)
{
	char path[128];
	FILE *f;
	snprintf(path, sizeof(path), "/sys/class/mtd/mtd%d/%s", i, name);
	f = fopen(path, "r");
	if (!f)
		return -1;
	int ok = fgets(out, len, f) != NULL;
	fclose(f);
	if (!ok)
		return -1;
	out[strcspn(out, "\n")] = 0;
	return 0;
}
static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1]<<8 |
		(uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static int all_ff(const uint8_t *p, unsigned n)
{
	while (n--)
		if (*p++ != 0xff)
			return 0;
	return 1;
}
static int read_page(int fd, unsigned p, int mode)
{
	struct mtd_read_req r = {0};
	r.start = (uint64_t)p * PAGE;
	r.len = PAGE;
	r.ooblen = OOB;
	r.usr_data = (uintptr_t)data;
	r.usr_oob = (uintptr_t)oob;
	r.mode = mode;
	if (ioctl(fd, MEMREAD, &r) < 0 || r.len != PAGE ||
	    r.ooblen != OOB || r.ecc_stats.uncorrectable_errors ||
	    r.ecc_stats.corrected_bitflips)
		return -1;
	return 0;
}
int main(int argc, char **argv)
{
	struct mtd_info_user info;
	char buf[128], dev[64];
	int id = -1, fd = -1, rc = 1, program;
	int64_t off = 0;
	unsigned i, p, sum = 0;
	FILE *f;
	const char *stage = "arguments";
	if (argc != 3 || (strcmp(argv[1], "--check") &&
	    strcmp(argv[1], "--program-blank-bl1"))) {
		fprintf(stderr, "Usage: %s --check|--program-blank-bl1 IMAGE\n", argv[0]);
		return 2;
	}
	program = !strcmp(argv[1], "--program-blank-bl1");
	stage = "RAM rescue guard";
	if (!access("/etc/tq210-rootfs-ready", F_OK) ||
	    !access("/sys/class/ubi/ubi0", F_OK))
		goto fail;
	f = fopen("/proc/mounts", "r");
	if (!f)
		goto fail;
	while (fgets(buf, sizeof(buf), f)) {
		if (strstr(buf, " ubifs ")) {
			fclose(f);
			goto fail;
		}
	}
	fclose(f);
	stage = "image header/checksum";
	f = fopen(argv[2], "rb");
	if (!f)
		goto fail;
	i = fread(image, 1, sizeof(image), f);
	int extra = fgetc(f), error = ferror(f);
	fclose(f);
	if (i != sizeof(image) || extra != EOF || error ||
	    le32(image) != sizeof(image) || le32(image+4) ||
	    le32(image+12) || (le32(image+16) >> 24) != 0xea)
		goto fail;
	for (i = 16; i < sizeof(image); i++)
		sum += image[i];
	if (sum != le32(image+8))
		goto fail;
	stage = "unique first-block partition";
	for (i = 0; i < 64; i++) {
		if (attr(i, "name", buf, sizeof(buf)) || strcmp(buf, "bl1-probe"))
			continue;
		if (id >= 0 || attr(i, "offset", buf, sizeof(buf)) ||
		    strtoull(buf, NULL, 0) != 0 ||
		    attr(i, "ecc_strength", buf, sizeof(buf)) ||
		    strtoul(buf, NULL, 10) != 8)
			goto fail;
		id = i;
	}
	if (id < 0)
		goto fail;
	snprintf(dev, sizeof(dev), "/dev/mtd%d", id);
	fd = open(dev, program ? O_RDWR : O_RDONLY);
	stage = "geometry";
	if (fd < 0 || ioctl(fd, MEMGETINFO, &info) ||
	    info.size != BLOCK || info.erasesize != BLOCK ||
	    info.writesize != PAGE || info.oobsize != OOB ||
	    !(info.flags & MTD_WRITEABLE))
		goto fail;
	stage = "bad-block and full blank check";
	if (ioctl(fd, MEMGETBADBLOCK, &off) != 0)
		goto fail;
	for (p = 0; p < 64; p++)
		if (read_page(fd, p, MTD_OPS_RAW) ||
		    !all_ff(data, PAGE) || !all_ff(oob, OOB))
			goto fail;
	printf("BL1 CHECK PASS: %s blank, good, checksum %08x\n", dev, sum);
	if (!program) {
		rc = 0;
		goto out;
	}
	stage = "program first four pages";
	for (p = 0; p < 4; p++) {
		struct mtd_write_req w = {0};
		w.start = (uint64_t)p * PAGE;
		w.len = PAGE;
		w.usr_data = (uintptr_t)(image + p * PAGE);
		w.mode = MTD_OPS_PLACE_OOB;
		if (ioctl(fd, MEMWRITE, &w))
			goto fail;
	}
	stage = "raw and ECC readback";
	for (p = 0; p < 4; p++) {
		if (read_page(fd, p, MTD_OPS_RAW) ||
		    memcmp(data, image+p*PAGE, PAGE) ||
		    !all_ff(oob, 12) || all_ff(oob+12, 52) ||
		    read_page(fd, p, MTD_OPS_PLACE_OOB) ||
		    memcmp(data, image+p*PAGE, PAGE))
			goto fail;
	}
	for (p = 4; p < 64; p++)
		if (read_page(fd, p, MTD_OPS_RAW) ||
		    !all_ff(data, PAGE) || !all_ff(oob, OOB))
			goto fail;
	puts("BL1 PROGRAM + READBACK PASS; no erase performed");
	rc = 0;
	goto out;
fail:
	fprintf(stderr, "BL1 REFUSED/FAILED at %s; no automatic erase\n", stage);
out:
	if (fd >= 0)
		close(fd);
	return rc;
}
