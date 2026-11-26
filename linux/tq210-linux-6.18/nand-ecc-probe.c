// SPDX-License-Identifier: GPL-2.0
/* Read-only NAND diagnostics. Uses the Linux 6.18 MEMREAD UAPI. */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
/* Direct source-tree UAPI inclusion: headers_install normally strips
 * sparse's __user annotation. Ubuntu 20.04's installed MTD header may
 * predate MEMREAD, so use the matching 6.18 definition.
 */
#define __user
#include "../kernel-6.18/include/uapi/mtd/mtd-abi.h"
#undef __user

#define PAGE 2048
#define OOB 64

static uint32_t fnv(const uint8_t *p, size_t len)
{
	uint32_t v = 2166136261U;
	while (len--)
		v = (v ^ *p++) * 16777619U;
	return v;
}

static int read_page(int fd, unsigned page, unsigned mode,
		     uint8_t *data, uint8_t *oob, struct mtd_read_req *req)
{
	int err;

	memset(req, 0, sizeof(*req));
	req->start = (uint64_t)page * PAGE;
	req->len = PAGE;
	req->ooblen = OOB;
	req->usr_data = (uintptr_t)data;
	req->usr_oob = (uintptr_t)oob;
	req->mode = mode;
	err = ioctl(fd, MEMREAD, req) < 0 ? errno : 0;
	return err;
}

static int probe(int fd, unsigned page)
{
	uint8_t raw[PAGE], oob[OOB], data[PAGE], spare[OOB];
	struct mtd_read_req req;
	unsigned i, diff = 0, used = 0;
	int err;

	err = read_page(fd, page, MTD_OPS_RAW, raw, oob, &req);
	if (err || req.len != PAGE || req.ooblen != OOB) {
		fprintf(stderr, "page %u raw read failed: %s\n", page,
			err ? strerror(err) : "short read");
		return 1;
	}
	for (i = 0; i < PAGE; i++)
		used += raw[i] != 0xff;
	printf("\nPAGE %u offset=0x%08x raw_hash=%08x non_ff=%u\n",
	       page, page * PAGE, fnv(raw, PAGE), used);
	for (i = 0; i < OOB; i++) {
		if (!(i % 16))
			printf("OOB %02u: ", i);
		printf("%02x%s", oob[i], (i % 16 == 15) ? "\n" : " ");
	}
	printf("Stored ECC40..55:");
	for (i = 40; i < 56; i += 4) {
		uint32_t v = (uint32_t)oob[i] | ((uint32_t)oob[i+1] << 8) |
			     ((uint32_t)oob[i+2] << 16) | ((uint32_t)oob[i+3] << 24);
		printf(" %08x", v);
	}
	puts("");

	err = read_page(fd, page, MTD_OPS_PLACE_OOB, data, spare, &req);
	if (req.len == PAGE) {
		for (i = 0; i < PAGE; i++)
			diff += raw[i] != data[i];
		printf("ECC read: errno=%d (%s) failed=%u corrected=%u max=%u changed_bytes=%u hash=%08x\n",
		       err, strerror(err), req.ecc_stats.uncorrectable_errors,
		       req.ecc_stats.corrected_bitflips, req.ecc_stats.max_bitflips,
		       diff, fnv(data, PAGE));
	} else {
		printf("ECC read incomplete: errno=%d (%s) failed=%u corrected=%u\n",
		       err, strerror(err), req.ecc_stats.uncorrectable_errors,
		       req.ecc_stats.corrected_bitflips);
		return 1;
	}
	/* EBADMSG is a diagnostic result, not a transport failure. */
	return err && err != EBADMSG && err != EUCLEAN;
}

int main(int argc, char **argv)
{
	static const unsigned samples[] = {0, 1, 128, 512, 2048, 4096,
					    8192, 16384, 65536, 262144, 524287};
	struct mtd_info_user info;
	int fd, rc = 0, i;

	fd = open("/dev/mtd0ro", O_RDONLY);
	if (fd < 0) {
		perror("open /dev/mtd0ro");
		return 1;
	}
	if (ioctl(fd, MEMGETINFO, &info) < 0 ||
	    info.writesize != PAGE || info.oobsize != OOB ||
	    info.size != 0x40000000U || (info.flags & MTD_WRITEABLE)) {
		fprintf(stderr, "unexpected MTD geometry or writable flags\n");
		close(fd);
		return 1;
	}
	if (argc == 1) {
		for (i = 0; i < (int)(sizeof(samples)/sizeof(samples[0])); i++)
			rc |= probe(fd, samples[i]);
	} else {
		for (i = 1; i < argc; i++) {
			char *end;
			unsigned long n;
			errno = 0;
			n = strtoul(argv[i], &end, 0);
			if (errno || end == argv[i] || *end || n >= info.size / PAGE) {
				fprintf(stderr, "invalid page: %s\n", argv[i]);
				rc = 1;
				continue;
			}
			rc |= probe(fd, n);
		}
	}
	close(fd);
	return rc;
}
