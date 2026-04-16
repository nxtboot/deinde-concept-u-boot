// SPDX-License-Identifier: GPL-2.0+
/*
 * VFS tests for ISO 9660 filesystems
 *
 * These tests are marked UTF_MANUAL and are called from test_vfs.py
 * which creates the ISO filesystem image using genisoimage.
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <blk.h>
#include <command.h>
#include <dm.h>
#include <env.h>
#include <fs.h>
#include <mapmem.h>
#include <sandbox_host.h>
#include <vfs.h>
#include <dm/device-internal.h>
#include <test/fs.h>
#include <test/test.h>
#include <test/ut.h>

#define VFS_ARG_IMAGE	0	/* fs_image: path to filesystem image */
#define BUF_ADDR	0x10000

/**
 * iso_mount_image() - Bind a host image and mount it via VFS
 *
 * @uts: Unit test state
 * @image: Path to filesystem image
 * @devp: Returns the host device
 * Return: 0 on success
 */
static int iso_mount_image(struct unit_test_state *uts, const char *image,
			   struct udevice **devp)
{
	struct udevice *blk;
	struct blk_desc *desc;

	ut_assertok(vfs_init());
	ut_assertok(host_create_device("isotest", true, DEFAULT_BLKSZ, devp));
	ut_assertok(host_attach_file(*devp, image));
	ut_assertok(blk_get_from_parent(*devp, &blk));
	ut_assertok(device_probe(blk));
	desc = dev_get_uclass_plat(blk);

	ut_assertok(run_commandf("mount host %x:0 /mnt", desc->devnum));
	ut_assert_console_end();

	return 0;
}

/**
 * iso_umount_image() - Unmount and clean up a host image
 *
 * @uts: Unit test state
 * @dev: The host device to detach
 * Return: 0 on success
 */
static int iso_umount_image(struct unit_test_state *uts, struct udevice *dev)
{
	ut_assertok(run_command("umount /mnt", 0));
	ut_assert_console_end();
	ut_assertok(host_detach_file(dev));
	ut_assertok(device_unbind(dev));

	return 0;
}

/* Test ls on VFS-mounted ISO filesystem */
static int fs_test_vfs_iso_ls_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(VFS_ARG_IMAGE);
	struct udevice *dev;

	ut_assertok(iso_mount_image(uts, fs_image, &dev));

	ut_assertok(run_command("ls /mnt", 0));
	ut_assert_nextline("DIR          0 .");
	ut_assert_nextline("DIR          0 ..");
	ut_assert_nextline("DIR          0 subdir");
	ut_assert_nextline("            12 testfile.txt");
	ut_assert_console_end();

	ut_assertok(iso_umount_image(uts, dev));

	return 0;
}
FS_TEST_ARGS(fs_test_vfs_iso_ls_norun,
	     UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });

/* Test cat on VFS-mounted ISO filesystem */
static int fs_test_vfs_iso_cat_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(VFS_ARG_IMAGE);
	struct udevice *dev;

	ut_assertok(iso_mount_image(uts, fs_image, &dev));

	ut_assertok(run_command("cat /mnt/testfile.txt", 0));
	ut_assert_nextline("hello world");
	ut_assert_console_end();

	ut_assertok(iso_umount_image(uts, dev));

	return 0;
}
FS_TEST_ARGS(fs_test_vfs_iso_cat_norun,
	     UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });

/* Test load on VFS-mounted ISO filesystem */
static int fs_test_vfs_iso_load_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(VFS_ARG_IMAGE);
	struct udevice *dev;
	char *buf;

	ut_assertok(iso_mount_image(uts, fs_image, &dev));

	buf = map_sysmem(BUF_ADDR, 0x100);
	memset(buf, '\0', 0x100);
	ut_assertok(run_commandf("load %x /mnt/testfile.txt", BUF_ADDR));
	ut_assert_nextline("12 bytes read");
	ut_assert_console_end();
	ut_asserteq_str("hello world\n", buf);
	unmap_sysmem(buf);

	ut_assertok(iso_umount_image(uts, dev));

	return 0;
}
FS_TEST_ARGS(fs_test_vfs_iso_load_norun,
	     UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });

/* Test size on VFS-mounted ISO filesystem */
static int fs_test_vfs_iso_size_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(VFS_ARG_IMAGE);
	struct udevice *dev;

	ut_assertok(iso_mount_image(uts, fs_image, &dev));

	env_set("filesize", NULL);
	ut_assertok(run_command("size /mnt/testfile.txt", 0));
	ut_assert_console_end();
	ut_asserteq(12, env_get_hex("filesize", 0));

	ut_assertok(iso_umount_image(uts, dev));

	return 0;
}
FS_TEST_ARGS(fs_test_vfs_iso_size_norun,
	     UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });

/* Test nested path access on VFS-mounted ISO filesystem */
static int fs_test_vfs_iso_nested_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(VFS_ARG_IMAGE);
	struct udevice *dev;
	char *buf;

	ut_assertok(iso_mount_image(uts, fs_image, &dev));

	/* Load file from subdirectory */
	buf = map_sysmem(BUF_ADDR, 0x100);
	memset(buf, '\0', 0x100);
	ut_assertok(run_commandf("load %x /mnt/subdir/nested.txt", BUF_ADDR));
	ut_assert_nextline("12 bytes read");
	ut_assert_console_end();
	ut_asserteq_str("hello world\n", buf);
	unmap_sysmem(buf);

	ut_assertok(iso_umount_image(uts, dev));

	return 0;
}
FS_TEST_ARGS(fs_test_vfs_iso_nested_norun,
	     UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });

/* Test cd and pwd on VFS-mounted ISO filesystem */
static int fs_test_vfs_iso_cd_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(VFS_ARG_IMAGE);
	struct udevice *dev;

	ut_assertok(iso_mount_image(uts, fs_image, &dev));

	ut_assertok(run_command("cd /mnt", 0));
	ut_assert_console_end();

	ut_assertok(run_command("pwd", 0));
	ut_assert_nextline("/mnt");
	ut_assert_console_end();

	/* ls without path should list cwd */
	ut_assertok(run_command("ls", 0));
	ut_assert_nextline("DIR          0 .");
	ut_assert_nextline("DIR          0 ..");
	ut_assert_nextline("DIR          0 subdir");
	ut_assert_nextline("            12 testfile.txt");
	ut_assert_console_end();

	/* cd back to root */
	ut_assertok(run_command("cd /", 0));
	ut_assert_console_end();

	ut_assertok(iso_umount_image(uts, dev));

	return 0;
}
FS_TEST_ARGS(fs_test_vfs_iso_cd_norun,
	     UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });
