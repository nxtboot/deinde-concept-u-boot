// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the filesystems layer
 *
 * Copyright 2025 Simon Glass <sjg@chromium.org>
 */

#include <console.h>
#include <env.h>
#include <dir.h>
#include <dm.h>
#include <file.h>
#include <fs.h>
#include <mapmem.h>
#include <os.h>
#include <sandbox_host.h>
#include <vfs.h>
#include <dm/device-internal.h>
#include <dm/test.h>
#include <dm/uclass-internal.h>
#include <test/ut.h>

#define READ_SIZE	0x20

/* Test basic filesystem access */
static int dm_test_fs_base(struct unit_test_state *uts)
{
	struct udevice *dev;

	ut_assertok(uclass_first_device_err(UCLASS_FS, &dev));

	ut_assertok(fs_mount(dev));
	ut_asserteq(-EISCONN, fs_mount(dev));

	ut_assertok(fs_unmount(dev));
	ut_asserteq(-ENOTCONN, fs_unmount(dev));

	return 0;
}
DM_TEST(dm_test_fs_base, UTF_SCAN_FDT);

/* Test accessing a directory */
static int dm_test_fs_dir(struct unit_test_state *uts)
{
	struct udevice *fsdev, *dir;
	struct fs_dir_stream *strm;
	struct fs_dirent dent;
	int found;

	ut_assertok(uclass_first_device_err(UCLASS_FS, &fsdev));

	ut_assertok(fs_mount(fsdev));

	ut_asserteq(-ENOENT, fs_lookup_dir(fsdev, "does-not-exit", &dir));
	ut_assertok(fs_lookup_dir(fsdev, "", &dir));
	ut_assertnonnull(dir);
	ut_asserteq_str("hostfs.dir", dir->name);

	ut_assertok(dir_open(dir, &strm));
	found = 0;
	do {
		ut_assertok(dir_read(dir, strm, &dent));
		if (!strcmp("README", dent.name)) {
			ut_asserteq(FS_DT_REG, dent.type);
			found += 1;
		} else if (!strcmp("common", dent.name)) {
			ut_asserteq(FS_DT_DIR, dent.type);
			found += 1;
		}
	} while (found < 2);
	ut_assertok(dir_close(dir, strm));

	ut_assertok(fs_unmount(fsdev));

	return 0;
}
DM_TEST(dm_test_fs_dir, UTF_SCAN_FDT);

/* Test reading a file */
static int dm_test_fs_file(struct unit_test_state *uts)
{
	struct udevice *fsdev, *dir, *fil;
	struct file_uc_priv *uc_priv;
	char buf[READ_SIZE + 1];

	ut_assertok(uclass_first_device_err(UCLASS_FS, &fsdev));

	ut_assertok(fs_mount(fsdev));

	ut_assertok(fs_lookup_dir(fsdev, "", &dir));
	ut_assertnonnull(dir);
	ut_asserteq_str("hostfs.dir", dir->name);

	/* check the start and end of the README, which perhaps won't change */
	ut_assertok(dir_open_file(dir, "README", DIR_O_RDONLY, &fil));
	ut_assertnonnull(fil);
	ut_asserteq_str("hostfs.dir.file.1", fil->name);
	uc_priv = dev_get_uclass_priv(fil);
	ut_asserteq_str("README", uc_priv->leaf);
	ut_asserteq(0, uc_priv->pos);
	ut_assert(uc_priv->size > 0x10000);

	buf[READ_SIZE] = '\0';
	ut_asserteq(READ_SIZE, file_read(fil, buf, READ_SIZE));
	ut_asserteq_str("# SPDX-License-Ident" "ifier: GPL-2", buf);
	ut_asserteq(0x20, uc_priv->pos);

	ut_asserteq(READ_SIZE, file_read_at(fil, buf, uc_priv->size - 0x20, 0));
	ut_asserteq_str("d the patch submission process.\n", buf);
	ut_asserteq(uc_priv->size, uc_priv->pos);

	return 0;
}
DM_TEST(dm_test_fs_file, UTF_SCAN_FDT);

/* Test writing a file */
static int dm_test_fs_file_write(struct unit_test_state *uts)
{
	struct udevice *fsdev, *dir, *fil;
	static const char data[] = "hello world\n";
	char buf[32];

	ut_assertok(uclass_first_device_err(UCLASS_FS, &fsdev));
	ut_assertok(fs_mount(fsdev));
	ut_assertok(fs_lookup_dir(fsdev, "", &dir));

	/* Write a new file */
	ut_assertok(dir_open_file(dir, "_test_write.tmp", DIR_O_WRONLY, &fil));
	ut_asserteq(sizeof(data) - 1,
		    file_write_at(fil, data, 0, sizeof(data) - 1));

	/* Read it back */
	ut_assertok(dir_open_file(dir, "_test_write.tmp", DIR_O_RDONLY, &fil));
	memset(buf, '\0', sizeof(buf));
	ut_asserteq(sizeof(data) - 1, file_read(fil, buf, sizeof(buf)));
	ut_asserteq_str("hello world\n", buf);

	/* Clean up */
	os_unlink("_test_write.tmp");

	return 0;
}
DM_TEST(dm_test_fs_file_write, UTF_SCAN_FDT);

#if IS_ENABLED(CONFIG_VFS)
/* Helper: resolve and compare */
static int check_resolve(struct unit_test_state *uts, const char *cwd,
			 const char *path, const char *expected)
{
	char buf[256];
	const char *ret;

	ret = vfs_path_resolve(cwd, path, buf, sizeof(buf));
	ut_assertnonnull(ret);
	ut_asserteq_str(expected, ret);

	return 0;
}

/* Test vfs_path_resolve() - no VFS device needed */
static int dm_test_vfs_path_resolve(struct unit_test_state *uts)
{
	char buf[512], buf2[512];
	int i;

	/* NULL/empty path returns cwd */
	ut_assertok(check_resolve(uts, "/", NULL, "/"));
	ut_assertok(check_resolve(uts, "/", "", "/"));
	ut_assertok(check_resolve(uts, "/host", NULL, "/host"));

	/* Absolute path ignores cwd */
	ut_assertok(check_resolve(uts, "/host", "/mnt", "/mnt"));
	ut_assertok(check_resolve(uts, "/host", "/a/b", "/a/b"));

	/* Relative path is joined with cwd */
	ut_assertok(check_resolve(uts, "/", "foo", "/foo"));
	ut_assertok(check_resolve(uts, "/host", "file.txt", "/host/file.txt"));
	ut_assertok(check_resolve(uts, "/host", "sub/file", "/host/sub/file"));

	/* . is resolved */
	ut_assertok(check_resolve(uts, "/host", ".", "/host"));
	ut_assertok(check_resolve(uts, "/host", "./a", "/host/a"));

	/* .. is resolved */
	ut_assertok(check_resolve(uts, "/host", "..", "/"));
	ut_assertok(check_resolve(uts, "/host/sub", "..", "/host"));
	ut_assertok(check_resolve(uts, "/host/a", "../b", "/host/b"));

	/* .. at root stays at root */
	ut_assertok(check_resolve(uts, "/", "..", "/"));
	ut_assertok(check_resolve(uts, "/", "../..", "/"));

	/* Absolute path with . and .. */
	ut_assertok(check_resolve(uts, "/", "/host/./sub/..", "/host"));
	ut_assertok(check_resolve(uts, "/", "/a/b/../c", "/a/c"));

	/* Trailing slash */
	ut_assertok(check_resolve(uts, "/", "/host/", "/host"));

	/* Stack overflow returns NULL (path with >64 components) */
	strcpy(buf, "/");
	for (i = 0; i < 65; i++) {
		if (i)
			strcat(buf, "/");
		strcat(buf, "a");
	}
	ut_assertnull(vfs_path_resolve("/", buf, buf2, sizeof(buf2)));

	return 0;
}
DM_TEST(dm_test_vfs_path_resolve, 0);

/* Test vfs_complete() tab completion */
static int dm_test_vfs_complete(struct unit_test_state *uts)
{
	struct udevice *vfs, *fsdev, *dir;
	char buf[2048];
	char *cmdv[16];

	ut_assertok(vfs_init());
	vfs = vfs_root();
	ut_assertnonnull(vfs);

	ut_assertok(uclass_get_device_by_name(UCLASS_FS, "hostfs", &fsdev));
	ut_assertok(vfs_resolve(vfs, "/host", &dir));
	ut_assertok(vfs_mount(vfs, dir, fsdev));

	/* Complete from root - should find "host" mount point */
	ut_assert(vfs_complete(buf, "/", 16, cmdv) >= 1);
	ut_asserteq_str("/host/", cmdv[0]);

	/* Partial mount name */
	ut_assert(vfs_complete(buf, "/h", 16, cmdv) >= 1);
	ut_asserteq_str("/host/", cmdv[0]);

	/* Non-matching prefix should return 0 */
	ut_asserteq(0, vfs_complete(buf, "/z", 16, cmdv));

	/* Create a known file to complete against */
	memcpy(map_sysmem(0x10000, 5), "hello", 5);
	ut_assertok(run_command("save 10000 /host/.comp_test 5", 0));
	console_record_reset_enable();

	/* Complete inside mount */
	ut_assert(vfs_complete(buf, "/host/.comp_", 16, cmdv) >= 1);
	ut_asserteq_str(".comp_test", cmdv[0]);

	/* Complete with empty prefix shows entries */
	ut_assert(vfs_complete(buf, "/host/", 16, cmdv) >= 1);

	os_unlink(".comp_test");
	ut_assertok(vfs_umount_path(vfs, "/host"));

	return 0;
}
DM_TEST(dm_test_vfs_complete, UTF_SCAN_FDT);

/* Test VFS init and root directory operations */
static int dm_test_vfs_init(struct unit_test_state *uts)
{
	struct udevice *vfs, *dir;
	struct fs_dir_stream *strm;
	struct fs_dirent dent;

	ut_assertok(vfs_init());

	vfs = vfs_root();
	ut_assertnonnull(vfs);

	/* Look up the root directory */
	ut_assertok(fs_lookup_dir(vfs, "", &dir));
	ut_assertnonnull(dir);

	/* open should succeed, read should return -ENOENT (root is empty) */
	ut_assertok(dir_open(dir, &strm));
	ut_asserteq(-ENOENT, dir_read(dir, strm, &dent));
	ut_assertok(dir_close(dir, strm));

	/* vfs_resolve("/") should return the root dir */
	ut_assertok(vfs_resolve(vfs, "/", &dir));
	ut_assertnonnull(dir);

	/* vfs_resolve with bad paths should fail */
	ut_asserteq(-EINVAL, vfs_resolve(vfs, NULL, &dir));
	ut_asserteq(-EINVAL, vfs_resolve(vfs, "no_slash", &dir));

	/* rootfs cannot be unmounted */
	ut_asserteq(-EBUSY, fs_unmount(vfs));

	return 0;
}
DM_TEST(dm_test_vfs_init, UTF_SCAN_FDT);

/* Test that the root directory lists mount points */
static int dm_test_vfs_dir(struct unit_test_state *uts)
{
	struct udevice *vfs, *fsdev, *dir, *root_dir;
	struct fs_dir_stream *strm;
	struct fs_dirent dent;

	ut_assertok(vfs_init());
	vfs = vfs_root();
	ut_assertnonnull(vfs);

	/* Root dir should be empty before any mounts */
	ut_assertok(fs_lookup_dir(vfs, "", &root_dir));
	ut_assertok(dir_open(root_dir, &strm));
	ut_asserteq(-ENOENT, dir_read(root_dir, strm, &dent));
	ut_assertok(dir_close(root_dir, strm));

	/* Mount the sandbox FS at /host */
	ut_assertok(uclass_get_device_by_name(UCLASS_FS, "hostfs", &fsdev));
	ut_assertok(vfs_resolve(vfs, "/host", &dir));
	ut_assertok(vfs_mount(vfs, dir, fsdev));

	/* Root dir should now list "host" */
	ut_assertok(fs_lookup_dir(vfs, "", &root_dir));
	ut_assertok(dir_open(root_dir, &strm));
	ut_assertok(dir_read(root_dir, strm, &dent));
	ut_asserteq_str("host", dent.name);
	ut_asserteq(FS_DT_DIR, dent.type);
	ut_asserteq(-ENOENT, dir_read(root_dir, strm, &dent));
	ut_assertok(dir_close(root_dir, strm));

	ut_assertok(vfs_umount_path(vfs, "/host"));

	return 0;
}
DM_TEST(dm_test_vfs_dir, UTF_SCAN_FDT);

/* Test basic VFS mount, find_mount, ls and umount */
static int dm_test_vfs_mount(struct unit_test_state *uts)
{
	struct udevice *vfs, *fsdev, *dir, *mnt;
	const char *subpath;

	ut_assertok(vfs_init());
	vfs = vfs_root();
	ut_assertnonnull(vfs);

	/* Find the sandbox FS (not the vfs_rootfs) */
	ut_assertok(uclass_get_device_by_name(UCLASS_FS, "hostfs", &fsdev));

	/* Resolve /host to a mount-point DIR */
	ut_assertok(vfs_resolve(vfs, "/host", &dir));

	/* Mount the sandbox FS at /host */
	ut_assertok(vfs_mount(vfs, dir, fsdev));

	/* Mounting same FS at another path is OK (-EISCONN ignored) */
	ut_assertok(vfs_resolve(vfs, "/other", &dir));
	ut_assertok(vfs_mount(vfs, dir, fsdev));
	ut_assertok(vfs_umount_path(vfs, "/other"));

	/* vfs_print_mounts() should show the /host mount */
	console_record_reset_enable();
	vfs_print_mounts();
	ut_assert_nextlinen("/host");
	ut_assert_console_end();

	/* find_mount should resolve /host exactly */
	ut_assertok(vfs_find_mount(vfs, "/host", &mnt, &subpath));
	ut_asserteq_str("", subpath);

	/* find_mount should strip mount prefix from subpath */
	ut_assertok(vfs_find_mount(vfs, "/host/some/path", &mnt, &subpath));
	ut_asserteq_str("some/path", subpath);

	/* find_mount should handle trailing component */
	ut_assertok(vfs_find_mount(vfs, "/host/file.txt", &mnt, &subpath));
	ut_asserteq_str("file.txt", subpath);

	/* find_mount should fail for unmounted path */
	ut_asserteq(-ENOENT, vfs_find_mount(vfs, "/nowhere", &mnt, &subpath));

	/* find_mount with partial prefix should not match */
	ut_asserteq(-ENOENT, vfs_find_mount(vfs, "/hostal", &mnt, &subpath));

	/* vfs_resolve with intermediate non-mount should fail */
	ut_asserteq(-ENOENT, vfs_resolve(vfs, "/bogus/sub", &dir));

	/* Unmount */
	ut_assertok(vfs_umount_path(vfs, "/host"));

	/* Should not be mounted any more */
	ut_asserteq(-ENOENT, vfs_find_mount(vfs, "/host", &mnt, &subpath));

	/* Double umount should fail */
	ut_asserteq(-ENOENT, vfs_umount_path(vfs, "/host"));

	/* Umount of never-mounted path should fail */
	ut_asserteq(-ENOENT, vfs_umount_path(vfs, "/bogus"));

	return 0;
}
DM_TEST(dm_test_vfs_mount, UTF_SCAN_FDT);

/* Test the VFS layer using the 'fs' command */
static int dm_test_vfs_cmd(struct unit_test_state *uts)
{
	ut_assertok(vfs_init());

	/* Mount the sandbox FS at /host */
	ut_assertok(run_command("fs mount hostfs /host", 0));
	ut_assert_console_end();

	/* Verify it appears in the mount list */
	ut_assertok(run_command("fs mount", 0));
	ut_assert_nextlinen("/host");
	ut_assert_console_end();

	/* Root should show the "host" mount point */
	ut_assertok(run_command("fs ls /", 0));
	ut_assert_nextline("DIR %10u host", 0);
	ut_assert_console_end();

	/* Listing /host should show sandbox directory contents */
	ut_assertok(run_command("fs ls /host", 0));
	ut_assert_skip_to_linen("DIR ");
	console_record_reset_enable();

	/* Unmount */
	ut_assertok(run_command("fs umount /host", 0));
	ut_assert_console_end();

	/* Mount list should now be empty */
	ut_assertok(run_command("fs mount", 0));
	ut_assert_console_end();

	return 0;
}
DM_TEST(dm_test_vfs_cmd, UTF_SCAN_FDT);

/* Test current working directory */
static int dm_test_vfs_cwd(struct unit_test_state *uts)
{
	struct udevice *vfs, *fsdev, *dir;

	ut_assertok(vfs_init());
	vfs = vfs_root();
	ut_assertnonnull(vfs);

	/* Default cwd is "/" */
	ut_asserteq_str("/", vfs_getcwd());

	/* Mount sandbox FS at /host */
	ut_assertok(uclass_get_device_by_name(UCLASS_FS, "hostfs", &fsdev));
	ut_assertok(vfs_resolve(vfs, "/host", &dir));
	ut_assertok(vfs_mount(vfs, dir, fsdev));

	/* cd to /host */
	ut_assertok(vfs_chdir("/host"));
	ut_asserteq_str("/host", vfs_getcwd());

	/* ls with NULL should list cwd (i.e. /host) */
	console_record_reset_enable();
	ut_assertok(vfs_ls(NULL));
	ut_assert_skip_to_linen("DIR ");
	console_record_reset_enable();

	/* cd back to root */
	ut_assertok(vfs_chdir("/"));
	ut_asserteq_str("/", vfs_getcwd());

	/* cd to non-existent path should fail */
	ut_asserteq(-ENOENT, vfs_chdir("/nowhere"));
	/* cwd should be unchanged */
	ut_asserteq_str("/", vfs_getcwd());

	ut_assertok(vfs_umount_path(vfs, "/host"));

	return 0;
}
DM_TEST(dm_test_vfs_cwd, UTF_SCAN_FDT);

/* Test umount -a */
static int dm_test_vfs_umount_all(struct unit_test_state *uts)
{
	ut_assertok(vfs_init());

	/* Clean up any leftover mounts */
	run_command("umount -a", 0);
	console_record_reset_enable();

	ut_assertok(run_command("mount hostfs /host", 0));
	ut_assert_console_end();

	/* Verify mount is there */
	ut_assertok(run_command("ls /", 0));
	ut_assert_nextline("DIR %10u host", 0);
	ut_assert_console_end();

	/* Unmount all */
	ut_assertok(run_command("umount -a", 0));
	ut_assert_console_end();

	/* Mount point directory should still exist but mount list is empty */
	console_record_reset_enable();
	vfs_print_mounts();
	ut_assert_console_end();

	return 0;
}
DM_TEST(dm_test_vfs_umount_all, UTF_SCAN_FDT);

/* Test cd and pwd commands */
static int dm_test_vfs_cd(struct unit_test_state *uts)
{
	ut_assertok(vfs_init());

	ut_assertok(run_command("mount hostfs /host", 0));
	ut_assert_console_end();

	/* Default cwd is root */
	ut_assertok(run_command("pwd", 0));
	ut_assert_nextline("/");
	ut_assert_console_end();

	/* cd to an absolute path */
	ut_assertok(run_command("cd /host", 0));
	ut_assert_console_end();
	ut_assertok(run_command("pwd", 0));
	ut_assert_nextline("/host");
	ut_assert_console_end();

	/* ls without a path should list cwd */
	ut_assertok(run_command("ls", 0));
	ut_assert_skip_to_linen("DIR ");
	console_record_reset_enable();

	/* Test relative file access from cwd */
	ut_assertok(run_command("cd /host", 0));
	ut_assert_console_end();

	/* 'cat Kbuild' should resolve to /host/Kbuild */
	ut_assertok(run_command("cat Kbuild", 0));
	ut_assert_skip_to_linen("# SPDX");
	console_record_reset_enable();

	/* cd .. should go back to root */
	ut_assertok(run_command("cd ..", 0));
	ut_assert_console_end();
	ut_assertok(run_command("pwd", 0));
	ut_assert_nextline("/");
	ut_assert_console_end();

	/* cd .. at root stays at root */
	ut_assertok(run_command("cd ..", 0));
	ut_assert_console_end();
	ut_assertok(run_command("pwd", 0));
	ut_assert_nextline("/");
	ut_assert_console_end();

	/* . is a no-op */
	ut_assertok(run_command("cd /host", 0));
	ut_assert_console_end();
	ut_assertok(run_command("cd .", 0));
	ut_assert_console_end();
	ut_assertok(run_command("pwd", 0));
	ut_assert_nextline("/host");
	ut_assert_console_end();

	/* cd back to root and umount */
	ut_assertok(run_command("cd /", 0));
	ut_assert_console_end();
	ut_assertok(run_command("umount /host", 0));
	ut_assert_console_end();

	return 0;
}
DM_TEST(dm_test_vfs_cd, UTF_SCAN_FDT);

/* Test multi-component path resolution (files in subdirectories) */
static int dm_test_vfs_path(struct unit_test_state *uts)
{
	ut_assertok(vfs_init());

	ut_assertok(run_command("mount hostfs /host", 0));
	ut_assert_console_end();

	/* stat a file in a subdirectory */
	ut_assertok(run_command("stat /host/cmd/vfs.c", 0));
	ut_assert_nextline("  File: vfs.c");
	ut_assert_nextlinen("  Size: ");
	ut_assert_nextline("  Type: regular file");
	console_record_reset_enable();

	/* load from a subdirectory */
	ut_assertok(run_command("load 1000000 /host/cmd/vfs.c", 0));
	ut_assert_nextlinen("%s", "");
	ut_assert_console_end();

	/* ls a nested subdirectory */
	ut_assertok(run_command("ls /host/arch", 0));
	ut_assert_skip_to_linen("DIR ");
	console_record_reset_enable();

	ut_assertok(run_command("umount /host", 0));
	ut_assert_console_end();

	return 0;
}
DM_TEST(dm_test_vfs_path, UTF_SCAN_FDT);

/* Test the cp command via VFS */
static int dm_test_vfs_cp(struct unit_test_state *uts)
{
	char buf[32];

	ut_assertok(vfs_init());
	ut_assertok(run_command("mount hostfs /host", 0));
	ut_assert_console_end();

	/* Write a source file */
	memcpy(map_sysmem(0x1000, 5), "hello", 5);
	ut_assertok(run_command("save 1000 /host/.cp_src 5", 0));
	ut_assert_nextline("5 bytes written");
	ut_assert_console_end();

	/* Copy it */
	ut_assertok(run_command("fs cp /host/.cp_src /host/.cp_dst", 0));
	ut_assert_nextline("5 bytes copied");
	ut_assert_console_end();

	/* Read back the copy and verify */
	memset(map_sysmem(0x2000, 8), 0, 8);
	ut_assertok(run_command("load 2000 /host/.cp_dst", 0));
	ut_assert_nextline("5 bytes read");
	ut_assert_console_end();

	memcpy(buf, map_sysmem(0x2000, 5), 5);
	buf[5] = '\0';
	ut_asserteq_str("hello", buf);

	os_unlink(".cp_src");
	os_unlink(".cp_dst");

	ut_assertok(run_command("umount /host", 0));
	ut_assert_console_end();

	return 0;
}
DM_TEST(dm_test_vfs_cp, UTF_SCAN_FDT);

/* Test the rm command via VFS */
static int dm_test_vfs_rm(struct unit_test_state *uts)
{
	ut_assertok(vfs_init());

	ut_assertok(run_command("mount hostfs /host", 0));
	ut_assert_console_end();

	/* Create a file to delete */
	memcpy(map_sysmem(0x1000, 4), "test", 4);
	ut_assertok(run_command("save 1000 /host/.rm_test 4", 0));
	ut_assert_nextline("4 bytes written");
	ut_assert_console_end();

	/* Verify it exists */
	ut_assertok(run_command("stat /host/.rm_test", 0));
	ut_assert_nextline("  File: .rm_test");
	ut_assert_nextlinen("  Size: ");
	ut_assert_nextline("  Type: regular file");
	console_record_reset_enable();

	/* Delete it */
	ut_assertok(run_command("rm /host/.rm_test", 0));
	ut_assert_console_end();

	/* Verify it is gone */
	ut_asserteq(1, run_command("stat /host/.rm_test", 0));
	console_record_reset_enable();

	ut_assertok(run_command("umount /host", 0));
	ut_assert_console_end();

	return 0;
}
DM_TEST(dm_test_vfs_rm, UTF_SCAN_FDT);

/* Test the mv command via VFS */
static int dm_test_vfs_mv(struct unit_test_state *uts)
{
	char buf[8];

	ut_assertok(vfs_init());

	ut_assertok(run_command("mount hostfs /host", 0));
	ut_assert_console_end();

	/* Create a file */
	memcpy(map_sysmem(0x1000, 6), "moved!", 6);
	ut_assertok(run_command("save 1000 /host/.mv_src 6", 0));
	ut_assert_nextline("6 bytes written");
	ut_assert_console_end();

	/* Rename it */
	ut_assertok(run_command("mv /host/.mv_src /host/.mv_dst", 0));
	ut_assert_console_end();

	/* Old name should be gone */
	ut_asserteq(1, run_command("stat /host/.mv_src", 0));
	console_record_reset_enable();

	/* New name should have the data */
	memset(map_sysmem(0x2000, 8), 0, 8);
	ut_assertok(run_command("load 2000 /host/.mv_dst", 0));
	ut_assert_nextline("6 bytes read");
	ut_assert_console_end();

	memcpy(buf, map_sysmem(0x2000, 6), 6);
	buf[6] = '\0';
	ut_asserteq_str("moved!", buf);

	os_unlink(".mv_dst");

	ut_assertok(run_command("umount /host", 0));
	ut_assert_console_end();

	return 0;
}
DM_TEST(dm_test_vfs_mv, UTF_SCAN_FDT);

/* Test the mkdir command via VFS */
static int dm_test_vfs_mkdir(struct unit_test_state *uts)
{
	ut_assertok(vfs_init());

	os_rmdir(".vfs_test_dir");

	ut_assertok(run_command("mount hostfs /host", 0));
	ut_assert_console_end();

	/* Create a directory */
	ut_assertok(run_command("mkdir /host/.vfs_test_dir", 0));
	ut_assert_console_end();

	/* Verify it appears in ls */
	ut_assertok(run_command("stat /host/.vfs_test_dir", 0));
	ut_assert_nextline("  File: .vfs_test_dir");
	ut_assert_nextlinen("  Size: ");
	ut_assert_nextline("  Type: directory");
	console_record_reset_enable();

	/* Clean up */
	os_rmdir(".vfs_test_dir");

	ut_assertok(run_command("umount /host", 0));
	ut_assert_console_end();

	return 0;
}
DM_TEST(dm_test_vfs_mkdir, UTF_SCAN_FDT);

/* Test symlink following and readlink */
static int dm_test_vfs_symlink(struct unit_test_state *uts)
{
	char *buf;

	ut_assertok(vfs_init());

	os_unlink(".vfs_test_link");
	buf = map_sysmem(0x10000, 0x21);

	ut_assertok(run_command("mount hostfs /host", 0));
	ut_assert_console_end();

	/* Create a symlink */
	ut_assertok(os_symlink("README", ".vfs_test_link"));

	/* stat shows it as a symbolic link with target */
	ut_assertok(run_command("stat /host/.vfs_test_link", 0));
	ut_assert_nextline("  File: .vfs_test_link");
	ut_assert_nextlinen("  Size: ");
	ut_assert_nextline("  Type: symbolic link");
	ut_assert_nextline("  Link: README");
	ut_assert_console_end();

	/* load follows the symlink transparently */
	memset(buf, '\0', 0x21);
	ut_assertok(run_command("load 10000 /host/.vfs_test_link 20", 0));
	ut_assert_nextline("32 bytes read");
	ut_assert_console_end();
	ut_asserteq_str("# SPDX-License-Ident" "ifier: GPL-2", buf);

	/* Clean up */
	os_unlink(".vfs_test_link");
	unmap_sysmem(buf);

	ut_assertok(run_command("umount /host", 0));
	ut_assert_console_end();

	return 0;
}
DM_TEST(dm_test_vfs_symlink, UTF_SCAN_FDT);

/* Test source command with VFS path */
static int dm_test_vfs_source(struct unit_test_state *uts)
{
	ut_assertok(vfs_init());

	ut_assertok(run_command("mount hostfs /host", 0));
	ut_assert_console_end();

	/* Create a simple script that sets an env var */
	memcpy(map_sysmem(0x1000, 18), "setenv vfs_test ok", 18);
	ut_assertok(run_command("save 1000 /host/.vfs_script 12", 0));
	ut_assert_nextline("18 bytes written");
	ut_assert_console_end();

	/* Run it via source */
	ut_assertok(run_command("source /host/.vfs_script", 0));
	ut_assert_nextlinen("## Executing script");
	ut_assert_console_end();

	/* Verify the env var was set */
	ut_asserteq_str("ok", env_get("vfs_test"));

	os_unlink(".vfs_script");
	env_set("vfs_test", NULL);

	ut_assertok(run_command("umount /host", 0));
	ut_assert_console_end();

	return 0;
}
DM_TEST(dm_test_vfs_source, UTF_SCAN_FDT);

/* Test the ln command via VFS */
static int dm_test_vfs_ln(struct unit_test_state *uts)
{
	ut_assertok(vfs_init());

	os_unlink(".ln_test");

	ut_assertok(run_command("mount hostfs /host", 0));
	ut_assert_console_end();

	/* Create a symlink */
	ut_assertok(run_command("ln README /host/.ln_test", 0));
	ut_assert_console_end();

	/* Verify it is a symlink pointing to README */
	ut_assertok(run_command("stat /host/.ln_test", 0));
	ut_assert_nextline("  File: .ln_test");
	ut_assert_nextlinen("  Size: ");
	ut_assert_nextline("  Type: symbolic link");
	ut_assert_nextline("  Link: README");
	ut_assert_console_end();

	os_unlink(".ln_test");

	ut_assertok(run_command("umount /host", 0));
	ut_assert_console_end();

	return 0;
}
DM_TEST(dm_test_vfs_ln, UTF_SCAN_FDT);

/* Test the tree command via VFS */
static int dm_test_vfs_tree(struct unit_test_state *uts)
{
	ut_assertok(vfs_init());

	ut_assertok(run_command("mount hostfs /host", 0));
	ut_assert_console_end();

	/* tree should show the root with host mount */
	ut_assertok(run_command("tree /", 0));
	ut_assert_nextline("/");
	ut_assert_skip_to_linen("    host/");
	console_record_reset_enable();

	ut_assertok(run_command("umount /host", 0));
	ut_assert_console_end();

	return 0;
}
DM_TEST(dm_test_vfs_tree, UTF_SCAN_FDT);

/* Test 'test -f' and 'test -d' with VFS paths */
static int dm_test_vfs_test(struct unit_test_state *uts)
{
	ut_assertok(vfs_init());

	ut_assertok(run_command("mount hostfs /host", 0));
	ut_assert_console_end();

	/* -f should succeed for regular files */
	ut_assertok(run_command("test -f /host/README", 0));

	/* -f should fail for directories */
	ut_asserteq(1, run_command("test -f /host/cmd", 0));

	/* -d should succeed for directories */
	ut_assertok(run_command("test -d /host/cmd", 0));

	/* -d should fail for regular files */
	ut_asserteq(1, run_command("test -d /host/README", 0));

	/* Both should fail for non-existent paths */
	ut_asserteq(1, run_command("test -f /host/nonexistent", 0));
	ut_asserteq(1, run_command("test -d /host/nonexistent", 0));

	ut_assertok(run_command("umount /host", 0));
	ut_assert_console_end();

	return 0;
}
DM_TEST(dm_test_vfs_test, UTF_SCAN_FDT);

/* Test mount -a (automount from fstab env) */
static int dm_test_vfs_fstab(struct unit_test_state *uts)
{
	ut_assertok(vfs_init());

	env_set("fstab", "hostfs /host");
	ut_assertok(run_command("mount -a", 0));
	ut_assert_console_end();

	/* Verify mount worked */
	ut_assertok(run_command("ls /", 0));
	ut_assert_nextline("DIR %10u host", 0);
	ut_assert_console_end();

	ut_assertok(run_command("umount /host", 0));
	ut_assert_console_end();

	env_set("fstab", NULL);

	return 0;
}
DM_TEST(dm_test_vfs_fstab, UTF_SCAN_FDT);

/* Test the df command on an ext4 mount */
static int dm_test_vfs_df(struct unit_test_state *uts)
{
	struct udevice *dev, *blk;
	struct blk_desc *desc;
	char fname[256];

	ut_assertok(vfs_init());

	ut_assertok(os_persistent_file(fname, sizeof(fname), "2MB.ext2.img"));

	ut_assertok(host_create_device("dftest", true, DEFAULT_BLKSZ, &dev));
	ut_assertok(host_attach_file(dev, fname));
	ut_assertok(blk_get_from_parent(dev, &blk));
	ut_assertok(device_probe(blk));
	desc = dev_get_uclass_plat(blk);

	ut_assertok(run_commandf("mount host %x:0 /df", desc->devnum));
	ut_assert_console_end();

	ut_assertok(run_command("df /df", 0));
	ut_assert_nextlinen("Filesystem");
	ut_assert_nextlinen("/df");
	ut_assert_console_end();

	/* df with no args should list all mounts */
	ut_assertok(run_command("df", 0));
	ut_assert_nextlinen("Filesystem");
	ut_assert_skip_to_linen("/df");
	console_record_reset_enable();

	ut_assertok(run_command("umount /df", 0));
	ut_assert_console_end();

	ut_assertok(host_detach_file(dev));
	ut_assertok(device_unbind(dev));

	return 0;
}
DM_TEST(dm_test_vfs_df, UTF_SCAN_FDT);

/* Test cross-filesystem copy (hostfs ↔ ext4) */
static int dm_test_vfs_cross_cp(struct unit_test_state *uts)
{
	struct udevice *dev, *blk;
	struct blk_desc *desc;
	struct fs_dirent dent;

	ut_assertok(vfs_init());
	char fname[256];

	ut_assertok(os_persistent_file(fname, sizeof(fname), "2MB.ext2.img"));

	ut_assertok(host_create_device("cptest", true, DEFAULT_BLKSZ, &dev));
	ut_assertok(host_attach_file(dev, fname));
	ut_assertok(blk_get_from_parent(dev, &blk));
	ut_assertok(device_probe(blk));
	desc = dev_get_uclass_plat(blk);

	ut_assertok(run_command("mount hostfs /host", 0));
	ut_assert_console_end();
	ut_assertok(run_commandf("mount host %x:0 /mnt", desc->devnum));
	ut_assert_console_end();

	/* Copy hostfs -> ext4 */
	ut_assertok(run_command("fs cp /host/Kbuild /mnt/Kbuild", 0));
	console_record_reset_enable();

	/* Verify the copy exists on ext4 */
	ut_assertok(vfs_stat("/mnt/Kbuild", &dent));
	ut_asserteq(FS_DT_REG, dent.type);
	ut_assert(dent.size > 0);

	/* Copy ext4 -> hostfs */
	ut_assertok(run_command("fs cp /mnt/Kbuild /host/.cross_cp_test", 0));
	console_record_reset_enable();

	/* Verify the copy exists on hostfs */
	ut_assertok(vfs_stat("/host/.cross_cp_test", &dent));
	ut_asserteq(FS_DT_REG, dent.type);
	ut_assert(dent.size > 0);

	os_unlink(".cross_cp_test");

	ut_assertok(run_command("umount /mnt", 0));
	ut_assert_console_end();
	ut_assertok(run_command("umount /host", 0));
	ut_assert_console_end();

	ut_assertok(host_detach_file(dev));
	ut_assertok(device_unbind(dev));

	return 0;
}
DM_TEST(dm_test_vfs_cross_cp, UTF_SCAN_FDT);

/* Test the stat command via VFS */
static int dm_test_vfs_stat(struct unit_test_state *uts)
{
	ut_assertok(vfs_init());

	ut_assertok(run_command("mount hostfs /host", 0));
	ut_assert_console_end();

	/* stat a regular file */
	ut_assertok(run_command("stat /host/README", 0));
	ut_assert_nextline("  File: README");
	ut_assert_nextlinen("  Size: ");
	ut_assert_nextline("  Type: regular file");
	console_record_reset_enable();

	/* stat a directory */
	ut_assertok(run_command("stat /host/cmd", 0));
	ut_assert_nextline("  File: cmd");
	ut_assert_nextlinen("  Size: ");
	ut_assert_nextline("  Type: directory");
	console_record_reset_enable();

	/* stat non-existent file */
	ut_asserteq(1, run_command("stat /host/does-not-exist", 0));
	console_record_reset_enable();

	ut_assertok(run_command("umount /host", 0));
	ut_assert_console_end();

	return 0;
}
DM_TEST(dm_test_vfs_stat, UTF_SCAN_FDT);

/* Test the -f and -d operators in the test command */
static int dm_test_vfs_test_fd(struct unit_test_state *uts)
{
	ut_assertok(vfs_init());

	ut_assertok(run_command("mount hostfs /host", 0));
	ut_assert_console_end();

	/* -f should succeed for a regular file */
	ut_assertok(run_command("test -f /host/README", 0));

	/* -f should fail for a directory */
	ut_asserteq(1, run_command("test -f /host/cmd", 0));

	/* -d should succeed for a directory */
	ut_assertok(run_command("test -d /host/cmd", 0));

	/* -d should fail for a regular file */
	ut_asserteq(1, run_command("test -d /host/README", 0));

	/* Both should fail for non-existent paths */
	ut_asserteq(1, run_command("test -f /host/no-such-file", 0));
	ut_asserteq(1, run_command("test -d /host/no-such-dir", 0));

	ut_assertok(run_command("umount /host", 0));
	ut_assert_console_end();

	return 0;
}
DM_TEST(dm_test_vfs_test_fd, UTF_SCAN_FDT);

/* Test the cat command via VFS */
static int dm_test_vfs_cat(struct unit_test_state *uts)
{
	ut_assertok(vfs_init());

	ut_assertok(run_command("mount hostfs /host", 0));
	ut_assert_console_end();

	/* Write a small file and cat it */
	memcpy(map_sysmem(0x1000, 11), "hello world", 11);
	ut_assertok(run_command("save 1000 /host/.cat_test 0xb", 0));
	ut_assert_nextlinen("11 bytes");
	ut_assert_console_end();

	ut_assertok(run_command("cat /host/.cat_test", 0));
	ut_assert_nextline("hello world");
	ut_assert_console_end();

	os_unlink(".cat_test");

	ut_assertok(run_command("umount /host", 0));
	ut_assert_console_end();

	return 0;
}
DM_TEST(dm_test_vfs_cat, UTF_SCAN_FDT);

/* Test save and load round-trip via VFS */
static int dm_test_vfs_save(struct unit_test_state *uts)
{
	char buf[32];

	ut_assertok(vfs_init());

	/* Mount hostfs */
	ut_assertok(run_command("mount hostfs /host", 0));
	ut_assert_console_end();

	/* Write a known pattern to a temp file */
	memset(map_sysmem(0x1000, 16), 0, 16);
	memcpy(map_sysmem(0x1000, 11), "hello world", 11);
	ut_assertok(run_command("save 1000 /host/.vfs_test_tmp 0xb", 0));
	ut_assert_nextline("11 bytes written");
	ut_assert_console_end();

	/* Read it back and verify */
	memset(map_sysmem(0x2000, 16), 0, 16);
	ut_assertok(run_command("load 2000 /host/.vfs_test_tmp", 0));
	ut_assert_nextline("11 bytes read");
	ut_assert_console_end();

	memcpy(buf, map_sysmem(0x2000, 11), 11);
	buf[11] = '\0';
	ut_asserteq_str("hello world", buf);

	/* Clean up the temp file */
	os_unlink(".vfs_test_tmp");

	ut_assertok(run_command("umount /host", 0));
	ut_assert_console_end();

	return 0;
}
DM_TEST(dm_test_vfs_save, UTF_SCAN_FDT);

#endif
