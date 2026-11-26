// SPDX-License-Identifier: GPL-2.0+
/* NAND boot policy for TQ210, preserving tqboot USB rescue. */
#include <command.h>
#include <linux/libfdt.h>
#include <linux/types.h>
#include <stdio.h>
#include <ubifs_uboot.h>

#define FIT_ADDR 0x40000000UL
#define FIT_MAX (32UL * 1024 * 1024)

static int tq_load_fit(const char *path)
{
	loff_t size, got;
	void *fit = (void *)FIT_ADDR;

	if (ubifs_size(path, &size) || size < sizeof(struct fdt_header) ||
	    size > FIT_MAX) {
		printf("TQ210: missing or invalid size: %s\n", path);
		return -1;
	}
	printf("TQ210: NAND loading %s (%lu bytes)\n", path,
	       (unsigned long)size);
	if (ubifs_read(path, fit, 0, size, &got) || got != size)
		return -1;
	/* This SDK uses embedded-data FITs only, with no external data. */
	if (fdt_check_header(fit) || fdt_totalsize(fit) > size ||
	    fdt_totalsize(fit) < sizeof(struct fdt_header))
		return -1;
	/* Check hashes before attempting bootm, so a damaged primary can
	 * fall back without entering the kernel boot state machine.
	 */
	if (run_command("iminfo 0x40000000", 0))
		return -1;
	return 0;
}

static int do_tqnandboot(struct cmd_tbl *cmdtp, int flag, int argc,
			char *const argv[])
{
	int ret;

	printf("TQ210: trying NAND boot volume\n");
	if (run_command("ubi part ubi-data", 0))
		goto failed;
	if (run_command("ubifsmount ubi0:boot", 0))
		goto detach_failed;
	ret = tq_load_fit("/tq210.itb");
	if (ret) {
		printf("TQ210: primary FIT unavailable; trying backup\n");
		ret = tq_load_fit("/tq210-backup.itb");
	}
	if (run_command("ubifsumount", 0))
		ret = -1;
	if (run_command("ubi detach", 0))
		ret = -1;
	if (ret)
		goto failed;
	printf("TQ210: booting verified NAND FIT\n");
	run_command("bootm 0x40000000", 0);
	/* A successful kernel handoff does not return. */
	goto failed;
detach_failed:
	run_command("ubi detach", 0);
failed:
	printf("TQ210: NAND boot failed; use USB rescue\n");
	return CMD_RET_FAILURE;
}

U_BOOT_CMD(tqnandboot, 1, 0, do_tqnandboot,
	   "boot Linux FIT from read-only NAND UBIFS", "");
