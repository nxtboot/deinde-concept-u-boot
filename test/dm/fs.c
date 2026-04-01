// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the filesystems layer
 *
 * Copyright 2025 Simon Glass <sjg@chromium.org>
 */

#include <console.h>
#include <dir.h>
#include <dm.h>
#include <file.h>
#include <fs.h>
#include <mapmem.h>
#include <os.h>
#include <vfs.h>
#include <dm/test.h>
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
	ut_asserteq_str("# SPDX-License-Identifier: GPL-2", buf);
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

#endif
