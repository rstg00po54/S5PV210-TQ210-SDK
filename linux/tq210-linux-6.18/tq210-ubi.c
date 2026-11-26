// SPDX-License-Identifier: GPL-2.0
/* Minimal UBI control utility for partitioned TQ210 RAM boot.
 * No raw erase, volume removal or resize.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#define __packed __attribute__((packed))
#include "../kernel-6.18/include/uapi/mtd/ubi-user.h"
#undef __packed

static int read_attr(const char *path, char *buf, size_t size)
{
	FILE *f = fopen(path, "r");
	int ret;
	if (!f)
		return -1;
	ret = fgets(buf, size, f) ? 0 : -1;
	fclose(f);
	if (!ret)
		buf[strcspn(buf, "\n")] = 0;
	return ret;
}

static int find_mtd(void)
{
	char path[128], buf[80];
	int i, found = -1;
	for (i = 0; i < 64; i++) {
		snprintf(path, sizeof(path), "/sys/class/mtd/mtd%d/name", i);
		if (read_attr(path, buf, sizeof(buf)) || strcmp(buf, "ubi-data"))
			continue;
		snprintf(path, sizeof(path), "/sys/class/mtd/mtd%d/size", i);
		if (read_attr(path, buf, sizeof(buf)) ||
		    strtoull(buf, NULL, 10) != 0x3e000000ULL || found >= 0)
			return -1;
		snprintf(path, sizeof(path), "/sys/class/mtd/mtd%d/erasesize", i);
		if (read_attr(path, buf, sizeof(buf)) ||
		    strtoul(buf, NULL, 10) != 131072)
			return -1;
		snprintf(path, sizeof(path), "/sys/class/mtd/mtd%d/ecc_strength", i);
		if (read_attr(path, buf, sizeof(buf)) ||
		    strtoul(buf, NULL, 10) != 8)
			return -1;
		found = i;
	}
	return found;
}

/* 0: not attached; 1: correct device; -1: wrong/unknown device. */
static int attached(int mtd)
{
	char buf[80];
	if (access("/sys/class/ubi/ubi0", F_OK))
		return errno == ENOENT ? 0 : -1;
	if (read_attr("/sys/class/ubi/ubi0/mtd_num", buf, sizeof(buf)))
		return -1;
	return atoi(buf) == mtd ? 1 : -1;
}

static int ctrl(int mtd, int attach)
{
	struct ubi_attach_req req = {0};
	int32_t num = 0;
	int fd, ret;
	fd = open("/dev/ubi_ctrl", O_RDWR);
	if (fd < 0) {
		perror("open ubi_ctrl");
		return -1;
	}
	req.ubi_num = 0;
	req.mtd_num = mtd;
	ret = attach ? ioctl(fd, UBI_IOCATT, &req) : ioctl(fd, UBI_IOCDET, &num);
	if (ret < 0)
		perror(attach ? "UBI_IOCATT" : "UBI_IOCDET");
	close(fd);
	return ret;
}

static int create(int rootfs)
{
	struct ubi_mkvol_req req = {0};
	char buf[80], path[80];
	const char *name = rootfs ? "rootfs" : "data";
	int id = rootfs ? 1 : 0;
	int fd, ret;
	snprintf(path, sizeof(path), "/sys/class/ubi/ubi0_%d", id);
	if (!access(path, F_OK)) {
		snprintf(path, sizeof(path), "/sys/class/ubi/ubi0_%d/name", id);
		if (read_attr(path, buf, sizeof(buf)) || strcmp(buf, name)) {
			fprintf(stderr, "Refused: volume %d already has another name\n", id);
			return -1;
		}
		printf("Existing %s volume retained\n", name);
		return 0;
	}
	if (errno != ENOENT)
		return -1;
	req.vol_id = id;
	req.alignment = 1;
	req.bytes = (rootfs ? 256LL : 128LL) * 1024 * 1024;
	req.vol_type = UBI_DYNAMIC_VOLUME;
	req.name_len = strlen(name);
	memcpy(req.name, name, req.name_len + 1);
	fd = open("/dev/ubi0", O_RDWR);
	if (fd < 0) {
		perror("open ubi0");
		return -1;
	}
	ret = ioctl(fd, UBI_IOCMKVOL, &req);
	if (ret < 0)
		perror("UBI_IOCMKVOL");
	close(fd);
	if (!ret)
		printf("Created dynamic %s volume (%d MiB requested)\n", name, rootfs ? 256 : 128);
	return ret;
}

int main(int argc, char **argv)
{
	int mtd, state;
	if (argc != 2 || (strcmp(argv[1], "attach") &&
			  strcmp(argv[1], "create-data") &&
			  strcmp(argv[1], "create-rootfs") &&
			  strcmp(argv[1], "detach"))) {
		fprintf(stderr, "Usage: %s attach|create-data|create-rootfs|detach\n", argv[0]);
		return 2;
	}
	mtd = find_mtd();
	if (mtd < 0) {
		fprintf(stderr, "Refused: expected unique BCH8 ubi-data partition\n");
		return 1;
	}
	state = attached(mtd);
	if (state < 0) {
		fprintf(stderr, "Refused: ubi0 belongs to a different or unknown MTD\n");
		return 1;
	}
	if (!strcmp(argv[1], "attach")) {
		if (!state && ctrl(mtd, 1))
			return 1;
		if (attached(mtd) != 1)
			return 1;
		printf("UBI attach PASS: mtd%d -> ubi0\n", mtd);
		return 0;
	}
	if (!state) {
		fprintf(stderr, "Attach ubi-data first\n");
		return 1;
	}
	if (!strcmp(argv[1], "create-data"))
		return create(0) ? 1 : 0;
	if (!strcmp(argv[1], "create-rootfs"))
		return create(1) ? 1 : 0;
	if (ctrl(mtd, 0))
		return 1;
	puts("UBI detach PASS");
	return 0;
}
