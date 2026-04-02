// SPDX-License-Identifier: GPL-2.0+
/*
 * VFS tests on real filesystem images
 *
 * These tests are marked UTF_MANUAL and are called from test_vfs.py
 * which creates the filesystem image.
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

/* Regex for a stat timestamp line, e.g. "Modify: 2026-04-02 08:21:38" */
#define TIMESTAMP_RE(label)	\
	label ": [0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9] [0-9:][0-9:]+"

/**
 * vfs_is_fat() - Check whether /mnt is a FAT filesystem
 *
 * Return: true if the mount at /mnt uses the FAT driver
 */
static bool vfs_is_fat(void)
{
	struct udevice *vfs, *mnt;
	struct vfsmount *m;
	const char *subpath;

	vfs = vfs_root();
	if (!vfs)
		return false;
	if (vfs_find_mount(vfs, "/mnt", &mnt, &subpath) || !mnt)
		return false;
	m = dev_get_uclass_priv(mnt);

	return !strcmp(m->target->driver->name, "fat_fs");
}

/**
 * vfs_check_stat_end() - Check stat timestamps based on filesystem type
 *
 * FAT populates timestamps so we expect Modify/Access/Birth lines.
 * ext4 (via the test images created with mkfs -d) does not.
 */
static int vfs_check_stat_end(struct unit_test_state *uts)
{
	if (vfs_is_fat()) {
		ut_assert_nextline_regex(TIMESTAMP_RE("Modify"));
		ut_assert_nextline_regex(TIMESTAMP_RE("Access"));
		ut_assert_nextline_regex(TIMESTAMP_RE(" Birth"));
	}
	ut_assert_console_end();

	return 0;
}

/**
 * vfs_mount_image() - Bind a host image and mount it via VFS
 *
 * @uts: Unit test state
 * @image: Path to filesystem image
 * @devp: Returns the host device
 * Return: 0 on success
 */
static int vfs_mount_image(struct unit_test_state *uts, const char *image,
			   struct udevice **devp)
{
	struct udevice *blk;
	struct blk_desc *desc;

	ut_assertok(vfs_init());
	ut_assertok(host_create_device("vfstest", true, DEFAULT_BLKSZ, devp));
	ut_assertok(host_attach_file(*devp, image));
	ut_assertok(blk_get_from_parent(*devp, &blk));
	ut_assertok(device_probe(blk));
	desc = dev_get_uclass_plat(blk);

	ut_assertok(run_commandf("mount host %x:0 /mnt", desc->devnum));
	ut_assert_console_end();

	return 0;
}

/**
 * vfs_umount_image() - Unmount and clean up a host image
 *
 * @uts: Unit test state
 * @dev: The host device to detach
 * Return: 0 on success
 */
static int vfs_umount_image(struct unit_test_state *uts, struct udevice *dev)
{
	ut_assertok(run_command("umount /mnt", 0));
	ut_assert_console_end();
	ut_assertok(host_detach_file(dev));
	ut_assertok(device_unbind(dev));

	return 0;
}

/* Test ls on VFS-mounted filesystem */
static int fs_test_vfs_ls_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(VFS_ARG_IMAGE);
	struct udevice *dev;

	ut_assertok(vfs_mount_image(uts, fs_image, &dev));

	ut_assertok(run_command("ls /mnt", 0));
	if (!vfs_is_fat()) {
		ut_assert_nextline("DIR          0 .");
		ut_assert_nextline("DIR          0 ..");
		ut_assert_nextline("DIR          0 lost+found");
		ut_assert_nextline("DIR          0 subdir");
		ut_assert_nextline("            12 testfile.txt");
	} else {
		ut_assert_nextline("DIR          0 subdir");
		ut_assert_nextline("            12 testfile.txt");
	}
	ut_assert_console_end();

	ut_assertok(vfs_umount_image(uts, dev));

	return 0;
}
FS_TEST_ARGS(fs_test_vfs_ls_norun,
	     UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });

/* Test stat on VFS-mounted filesystem */
static int fs_test_vfs_stat_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(VFS_ARG_IMAGE);
	struct udevice *dev;

	ut_assertok(vfs_mount_image(uts, fs_image, &dev));

	ut_assertok(run_command("stat /mnt/testfile.txt", 0));
	ut_assert_nextline("  File: testfile.txt");
	ut_assert_nextline("  Size: 12");
	ut_assert_nextline("  Type: regular file");
	ut_assertok(vfs_check_stat_end(uts));

	/* Stat a directory */
	ut_assertok(run_command("stat /mnt/subdir", 0));
	ut_assert_nextline("  File: subdir");
	ut_assert_nextline("  Size: %d", vfs_is_fat() ? 0 : 4096);
	ut_assert_nextline("  Type: directory");
	ut_assertok(vfs_check_stat_end(uts));

	ut_assertok(vfs_umount_image(uts, dev));

	return 0;
}
FS_TEST_ARGS(fs_test_vfs_stat_norun,
	     UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });

/* Test load from VFS-mounted filesystem */
static int fs_test_vfs_load_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(VFS_ARG_IMAGE);
	struct udevice *dev;
	char *buf;

	ut_assertok(vfs_mount_image(uts, fs_image, &dev));

	buf = map_sysmem(BUF_ADDR, 0x100);
	memset(buf, '\0', 0x100);
	ut_assertok(run_commandf("load %x /mnt/testfile.txt", BUF_ADDR));
	ut_assert_nextline("12 bytes read");
	ut_assert_console_end();
	ut_asserteq_str("hello world\n", buf);
	unmap_sysmem(buf);

	ut_assertok(vfs_umount_image(uts, dev));

	return 0;
}
FS_TEST_ARGS(fs_test_vfs_load_norun,
	     UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });

/* Test save to VFS-mounted filesystem */
static int fs_test_vfs_save_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(VFS_ARG_IMAGE);
	struct udevice *dev;
	char *buf;

	ut_assertok(vfs_mount_image(uts, fs_image, &dev));

	/* Write a file */
	buf = map_sysmem(BUF_ADDR, 0x10);
	memcpy(buf, "test data\n", 10);
	ut_assertok(run_commandf("save %x /mnt/newfile.txt a", BUF_ADDR));
	ut_assert_nextline("10 bytes written");
	ut_assert_console_end();

	/* Read it back */
	memset(buf, '\0', 0x10);
	ut_assertok(run_commandf("load %x /mnt/newfile.txt", BUF_ADDR));
	ut_assert_nextline("10 bytes read");
	ut_assert_console_end();
	ut_asserteq_mem("test data\n", buf, 10);
	unmap_sysmem(buf);

	ut_assertok(vfs_umount_image(uts, dev));

	return 0;
}
FS_TEST_ARGS(fs_test_vfs_save_norun,
	     UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });

/* Test mkdir on VFS-mounted filesystem */
static int fs_test_vfs_mkdir_norun(struct unit_test_state *uts)
{
	static int mkdir_seq;
	const char *fs_image = ut_str(VFS_ARG_IMAGE);
	struct udevice *dev;
	char name[32];

	/*
	 * Use a unique name each run because the test image is shared
	 * between live-tree and flat-tree passes, and VFS has no rmdir
	 * to clean up afterwards.
	 */
	snprintf(name, sizeof(name), ".vfs_mkdir_%d", mkdir_seq++);

	ut_assertok(vfs_mount_image(uts, fs_image, &dev));

	/* Verify it does not exist yet */
	ut_asserteq(1, run_commandf("stat /mnt/%s", name));
	console_record_reset_enable();

	ut_assertok(run_commandf("mkdir /mnt/%s", name));
	ut_assert_console_end();

	ut_assertok(run_commandf("stat /mnt/%s", name));
	ut_assert_nextline("  File: %s", name);
	ut_assert_nextline("  Size: %d", vfs_is_fat() ? 0 : 4096);
	ut_assert_nextline("  Type: directory");
	ut_assertok(vfs_check_stat_end(uts));

	ut_assertok(vfs_umount_image(uts, dev));

	return 0;
}
FS_TEST_ARGS(fs_test_vfs_mkdir_norun,
	     UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });

/* Test rm on VFS-mounted filesystem */
static int fs_test_vfs_rm_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(VFS_ARG_IMAGE);
	struct udevice *dev;
	char *buf;

	ut_assertok(vfs_mount_image(uts, fs_image, &dev));

	/* Write a file then delete it */
	buf = map_sysmem(BUF_ADDR, 0x10);
	memcpy(buf, "delete me\n", 10);
	ut_assertok(run_commandf("save %x /mnt/.rm_test a", BUF_ADDR));
	ut_assert_nextline("10 bytes written");
	ut_assert_console_end();

	ut_assertok(run_command("rm /mnt/.rm_test", 0));
	ut_assert_console_end();

	/* Verify it is gone */
	ut_asserteq(1, run_command("stat /mnt/.rm_test", 0));
	ut_assert_nextline("Error: -2: No such file or directory");
	ut_assert_console_end();

	unmap_sysmem(buf);
	ut_assertok(vfs_umount_image(uts, dev));

	return 0;
}
FS_TEST_ARGS(fs_test_vfs_rm_norun,
	     UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });

/* Test mv on VFS-mounted filesystem */
static int fs_test_vfs_mv_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(VFS_ARG_IMAGE);
	struct udevice *dev;
	char *buf;

	ut_assertok(vfs_mount_image(uts, fs_image, &dev));

	/* Write a file */
	buf = map_sysmem(BUF_ADDR, 0x10);
	memcpy(buf, "move me\n", 8);
	ut_assertok(run_commandf("save %x /mnt/.mv_src 8", BUF_ADDR));
	ut_assert_nextline("8 bytes written");
	ut_assert_console_end();

	/* Rename it */
	ut_assertok(run_command("mv /mnt/.mv_src /mnt/.mv_dst", 0));
	ut_assert_console_end();

	/* Old name should be gone */
	ut_asserteq(1, run_command("stat /mnt/.mv_src", 0));
	ut_assert_nextline("Error: -2: No such file or directory");
	ut_assert_console_end();

	/* New name should have the data */
	memset(buf, '\0', 0x10);
	ut_assertok(run_commandf("load %x /mnt/.mv_dst", BUF_ADDR));
	ut_assert_nextline("8 bytes read");
	ut_assert_console_end();
	ut_asserteq_mem("move me\n", buf, 8);

	unmap_sysmem(buf);
	ut_assertok(vfs_umount_image(uts, dev));

	return 0;
}
FS_TEST_ARGS(fs_test_vfs_mv_norun,
	     UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });

/* Test cat on VFS-mounted filesystem */
static int fs_test_vfs_cat_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(VFS_ARG_IMAGE);
	struct udevice *dev;

	ut_assertok(vfs_mount_image(uts, fs_image, &dev));

	ut_assertok(run_command("cat /mnt/testfile.txt", 0));
	ut_assert_nextline("hello world");
	ut_assert_console_end();

	ut_assertok(vfs_umount_image(uts, dev));

	return 0;
}
FS_TEST_ARGS(fs_test_vfs_cat_norun,
	     UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });

/* Test size on VFS-mounted filesystem */
static int fs_test_vfs_size_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(VFS_ARG_IMAGE);
	struct udevice *dev;

	ut_assertok(vfs_mount_image(uts, fs_image, &dev));

	env_set("filesize", NULL);
	ut_assertok(run_command("size /mnt/testfile.txt", 0));
	ut_assert_console_end();
	ut_asserteq(12, env_get_hex("filesize", 0));

	ut_assertok(vfs_umount_image(uts, dev));

	return 0;
}
FS_TEST_ARGS(fs_test_vfs_size_norun,
	     UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });

/* Test df on VFS-mounted filesystem */
static int fs_test_vfs_df_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(VFS_ARG_IMAGE);
	struct udevice *dev;

	ut_assertok(vfs_mount_image(uts, fs_image, &dev));

	ut_assertok(run_command("df /mnt", 0));
	ut_assert_nextline(
		"Filesystem        Blksz      Total       Used       Free  Size");
	ut_assert_nextlinen("/mnt");
	ut_assert_console_end();

	ut_assertok(vfs_umount_image(uts, dev));

	return 0;
}
FS_TEST_ARGS(fs_test_vfs_df_norun,
	     UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });

/* Test nested path access on VFS-mounted filesystem */
static int fs_test_vfs_nested_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(VFS_ARG_IMAGE);
	struct udevice *dev;
	char *buf;

	ut_assertok(vfs_mount_image(uts, fs_image, &dev));

	/* Load file from subdirectory */
	buf = map_sysmem(BUF_ADDR, 0x100);
	memset(buf, '\0', 0x100);
	ut_assertok(run_commandf("load %x /mnt/subdir/nested.txt", BUF_ADDR));
	ut_assert_nextline("12 bytes read");
	ut_assert_console_end();
	ut_asserteq_str("hello world\n", buf);

	/* Stat the subdirectory file */
	ut_assertok(run_command("stat /mnt/subdir/nested.txt", 0));
	ut_assert_nextline("  File: nested.txt");
	ut_assert_nextline("  Size: 12");
	ut_assert_nextline("  Type: regular file");
	ut_assertok(vfs_check_stat_end(uts));

	unmap_sysmem(buf);
	ut_assertok(vfs_umount_image(uts, dev));

	return 0;
}
FS_TEST_ARGS(fs_test_vfs_nested_norun,
	     UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });

/* Test cd and pwd on VFS-mounted filesystem */
static int fs_test_vfs_cd_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(VFS_ARG_IMAGE);
	struct udevice *dev;

	ut_assertok(vfs_mount_image(uts, fs_image, &dev));

	ut_assertok(run_command("cd /mnt", 0));
	ut_assert_console_end();

	ut_assertok(run_command("pwd", 0));
	ut_assert_nextline("/mnt");
	ut_assert_console_end();

	/* ls without path should list cwd */
	ut_assertok(run_command("ls", 0));
	if (!vfs_is_fat()) {
		ut_assert_nextline("DIR          0 .");
		ut_assert_nextline("DIR          0 ..");
		ut_assert_nextline("DIR          0 lost+found");
		ut_assert_nextline("DIR          0 subdir");
		ut_assert_nextline("            12 testfile.txt");
	} else {
		ut_assert_nextline("DIR          0 subdir");
		ut_assert_nextline("            12 testfile.txt");
	}
	ut_assert_console_end();

	/* cd back to root */
	ut_assertok(run_command("cd /", 0));
	ut_assert_console_end();

	ut_assertok(vfs_umount_image(uts, dev));

	return 0;
}
FS_TEST_ARGS(fs_test_vfs_cd_norun,
	     UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });

/* Test ln (symlink) on VFS-mounted ext4 */
static int fs_test_vfs_ext4_ln_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(VFS_ARG_IMAGE);
	struct udevice *dev;

	ut_assertok(vfs_mount_image(uts, fs_image, &dev));

	/* Create a symlink to testfile.txt */
	ut_assertok(run_command("ln testfile.txt /mnt/link.txt", 0));
	ut_assert_console_end();

	/* Stat should show it as a symlink */
	ut_assertok(run_command("stat /mnt/link.txt", 0));
	ut_assert_nextline("  File: link.txt");
	ut_assert_nextline("  Size: 12");
	ut_assert_nextline("  Type: symbolic link");
	ut_assertok(vfs_check_stat_end(uts));

	ut_assertok(vfs_umount_image(uts, dev));

	return 0;
}
FS_TEST_ARGS(fs_test_vfs_ext4_ln_norun,
	     UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });
