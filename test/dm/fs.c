// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the filesystems layer
 *
 * Copyright 2025 Simon Glass <sjg@chromium.org>
 */

#include <dir.h>
#include <dm.h>
#include <file.h>
#include <fs.h>
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

#if IS_ENABLED(CONFIG_VFS)
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

	/* rootfs cannot be unmounted */
	ut_asserteq(-EBUSY, fs_unmount(vfs));

	return 0;
}
DM_TEST(dm_test_vfs_init, UTF_SCAN_FDT);
#endif
