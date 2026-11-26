// SPDX-License-Identifier: GPL-2.0
/* BL2 fixed slot installer: blank-only, no erase, no other MTD access. */
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
#define SLOT 0x200000U
#define OFFSET 0x100000U
#define MAX_PAYLOAD 0x100000U
static uint8_t image[PAGE + MAX_PAYLOAD], data[PAGE], oob[OOB];

static int attr(int i, const char *name, char *out, size_t len)
{
	char path[128];
	snprintf(path, sizeof(path), "/sys/class/mtd/mtd%d/%s", i, name);
	FILE *f = fopen(path, "r");
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
static uint32_t crc32(const uint8_t *p, size_t n)
{
	uint32_t c = ~0u;
	while (n--) {
		c ^= *p++;
		for (unsigned i = 0; i < 8; i++)
			c = (c >> 1) ^ (0xedb88320u & (0u-(c&1u)));
	}
	return ~c;
}
static int all_ff(const uint8_t *p, size_t n)
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
	char buf[512], dev[64];
	int id = -1, fd = -1, rc = 1, program;
	unsigned i, p, pages, len;
	size_t n;
	FILE *f;
	const char *stage = "arguments";
	if (argc != 3 || (strcmp(argv[1], "--check") &&
	    strcmp(argv[1], "--program-blank-bl2"))) {
		fprintf(stderr, "Usage: %s --check|--program-blank-bl2 IMAGE\n", argv[0]);
		return 2;
	}
	program = !strcmp(argv[1], "--program-blank-bl2");
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
	stage = "image length/header/CRC";
	f = fopen(argv[2], "rb");
	if (!f)
		goto fail;
	n = fread(image, 1, sizeof(image), f);
	int extra = fgetc(f), error = ferror(f);
	fclose(f);
	if (n < 2*PAGE || extra != EOF || error ||
	    le32(image) != 0x324c4254u || le32(image+12) != 0x24000000u ||
	    le32(image+16) != crc32(image,16))
		goto fail;
	len = le32(image+4);
	if (!len || len > MAX_PAYLOAD || (len&3u))
		goto fail;
	pages = 1 + (len+PAGE-1)/PAGE;
	if (n != (size_t)pages*PAGE || crc32(image+PAGE,len) != le32(image+8) ||
	    !all_ff(image+20,PAGE-20) ||
	    !all_ff(image+PAGE+len,n-PAGE-len))
		goto fail;
	stage = "unique BL2 slot";
	for (i = 0; i < 64; i++) {
		if (attr(i, "name", buf, sizeof(buf)) || strcmp(buf,"bl2-slot"))
			continue;
		if (id >= 0 || attr(i,"offset",buf,sizeof(buf)) ||
		    strtoull(buf,NULL,0) != OFFSET ||
		    attr(i,"ecc_strength",buf,sizeof(buf)) ||
		    strtoul(buf,NULL,10) != 8)
			goto fail;
		id = i;
	}
	if (id < 0)
		goto fail;
	snprintf(dev,sizeof(dev),"/dev/mtd%d",id);
	fd = open(dev,program ? O_RDWR : O_RDONLY);
	stage = "geometry";
	if (fd < 0 || ioctl(fd,MEMGETINFO,&info) ||
	    info.size != SLOT || info.erasesize != BLOCK ||
	    info.writesize != PAGE || info.oobsize != OOB ||
	    !(info.flags & MTD_WRITEABLE))
		goto fail;
	stage = "all slot blocks good and blank";
	for (i = 0; i < SLOT/BLOCK; i++) {
		int64_t off = (int64_t)i*BLOCK;
		if (ioctl(fd,MEMGETBADBLOCK,&off) != 0)
			goto fail;
	}
	for (p = 0; p < SLOT/PAGE; p++)
		if (read_page(fd,p,MTD_OPS_RAW) ||
		    !all_ff(data,PAGE) || !all_ff(oob,OOB))
			goto fail;
	printf("BL2 CHECK PASS: %s, payload=%u, CRC32=%08x, pages=%u\n",
	       dev,len,le32(image+8),pages);
	if (!program) {
		rc = 0;
		goto out;
	}
	stage = "sequential ECC program";
	for (p = 0; p < pages; p++) {
		struct mtd_write_req w = {0};
		w.start = (uint64_t)p*PAGE;
		w.len = PAGE;
		w.usr_data = (uintptr_t)(image+p*PAGE);
		w.mode = MTD_OPS_PLACE_OOB;
		if (ioctl(fd,MEMWRITE,&w))
			goto fail;
	}
	stage = "raw and ECC readback";
	for (p = 0; p < pages; p++) {
		if (read_page(fd,p,MTD_OPS_RAW) ||
		    memcmp(data,image+p*PAGE,PAGE) || !all_ff(oob,12) ||
		    (!all_ff(data,PAGE) && all_ff(oob+12,52)) ||
		    read_page(fd,p,MTD_OPS_PLACE_OOB) ||
		    memcmp(data,image+p*PAGE,PAGE))
			goto fail;
	}
	for (; p < SLOT/PAGE; p++)
		if (read_page(fd,p,MTD_OPS_RAW) ||
		    !all_ff(data,PAGE) || !all_ff(oob,OOB))
			goto fail;
	puts("BL2 PROGRAM + READBACK PASS; no erase performed");
	rc = 0;
	goto out;
fail:
	fprintf(stderr,"BL2 REFUSED/FAILED at %s; no automatic erase\n",stage);
out:
	if (fd >= 0)
		close(fd);
	return rc;
}
