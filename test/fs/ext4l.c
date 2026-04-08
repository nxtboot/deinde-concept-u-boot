// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for ext4l filesystem (Linux ext4 port)
 *
 * Copyright 2025 Canonical Ltd
 * Written by Simon Glass <simon.glass@canonical.com>
 */

#include <command.h>
#include <env.h>
#include <ext4l.h>
#include <fs.h>
#include <fs_legacy.h>
#include <linux/sizes.h>
#include <u-boot/uuid.h>
#include <test/test.h>
#include <test/ut.h>
#include <test/fs.h>

#define EXT4L_ARG_IMAGE		0	/* fs_image: path to filesystem image */

/**
 * fs_test_ext4l_probe_norun() - Test probing an ext4l filesystem
 *
 * This test verifies that the ext4l driver can successfully probe and
 * mount an ext4 filesystem image.
 *
 * Arguments:
 *   fs_image: Path to the ext4 filesystem image
 */
static int fs_test_ext4l_probe_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(EXT4L_ARG_IMAGE);

	ut_assertnonnull(fs_image);
	ut_assertok(run_commandf("host bind 0 %s", fs_image));
	ut_assertok(fs_set_blk_dev("host", "0", FS_TYPE_ANY));

	return 0;
}
FS_TEST_ARGS(fs_test_ext4l_probe_norun, UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });

/**
 * fs_test_ext4l_msgs_norun() - Test ext4l_msgs env var output
 *
 * This test verifies that setting ext4l_msgs=y causes mount messages
 * to be printed when probing an ext4 filesystem.
 *
 * Arguments:
 *   fs_image: Path to the ext4 filesystem image
 */
static int fs_test_ext4l_msgs_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(EXT4L_ARG_IMAGE);
	char uuid_str[UUID_STR_LEN + 1];
	u8 uuid[16];

	ut_assertnonnull(fs_image);
	ut_assertok(env_set("ext4l_msgs", "y"));
	console_record_reset_enable();
	ut_assertok(run_commandf("host bind 0 %s", fs_image));
	ut_assertok(fs_set_blk_dev("host", "0", FS_TYPE_ANY));

	/* Get the UUID and clear the env var now we have the output */
	ut_assertok(ext4l_get_uuid_legacy(uuid));
	uuid_bin_to_str(uuid, uuid_str, UUID_STR_FORMAT_STD);
	ut_assertok(env_set("ext4l_msgs", NULL));

	/*
	 * Check messages. The probe test runs first and doesn't unmount,
	 * so the journal needs recovery. The filesystem may be mounted
	 * multiple times during probe operations. Just verify we see the
	 * expected mount message at least once.
	 */
	ut_assert_skip_to_line("EXT4-fs (ext4l_mmc0): mounted filesystem %s r/w"
			       " with ordered data mode. Quota mode: disabled.",
			       uuid_str);
	/* Skip any remaining messages */

	return 0;
}
FS_TEST_ARGS(fs_test_ext4l_msgs_norun, UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });

/**
 * fs_test_ext4l_ls_norun() - Test ext4l ls command
 *
 * This test verifies that the ext4l driver can list directory contents.
 *
 * Arguments:
 *   fs_image: Path to the ext4 filesystem image
 */
static int fs_test_ext4l_ls_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(EXT4L_ARG_IMAGE);

	ut_assertnonnull(fs_image);
	ut_assertok(run_commandf("host bind 0 %s", fs_image));
	console_record_reset_enable();
	ut_assertok(run_commandf("ls host 0"));
	/*
	 * The Python test adds testfile.txt (12 bytes) to the image.
	 * Directory entries appear in hash order which varies between runs.
	 * Verify the file entry appears with correct size (12 bytes).
	 * Other entries like ., .., subdir, lost+found may also appear.
	 */
	ut_assert_skip_to_line("       12   testfile.txt");
	/* Skip any remaining entries */

	return 0;
}
FS_TEST_ARGS(fs_test_ext4l_ls_norun, UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });

/**
 * fs_test_ext4l_opendir_norun() - Test ext4l opendir/readdir/closedir
 *
 * Verifies that the ext4l driver can iterate through directory entries using
 * the opendir/readdir/closedir interface. It checks:
 * - Regular files (testfile.txt)
 * - Subdirectories (subdir)
 * - Symlinks (link.txt)
 * - Files in subdirectories (subdir/nested.txt)
 *
 * Arguments:
 *   fs_image: Path to the ext4 filesystem image
 */
static int fs_test_ext4l_opendir_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(EXT4L_ARG_IMAGE);
	struct fs_dir_stream *dirs;
	struct fs_dirent *dent;
	bool found_testfile = false;
	bool found_subdir = false;
	bool found_symlink = false;
	bool found_nested = false;
	int count = 0;

	ut_assertnonnull(fs_image);
	ut_assertok(run_commandf("host bind 0 %s", fs_image));
	ut_assertok(fs_set_blk_dev("host", "0", FS_TYPE_ANY));

	/* Open root directory */
	ut_assertok(ext4l_opendir_legacy("/", &dirs));
	ut_assertnonnull(dirs);

	/* Iterate through entries */
	while (!ext4l_readdir_legacy(dirs, &dent)) {
		ut_assertnonnull(dent);
		count++;
		if (!strcmp(dent->name, "testfile.txt")) {
			found_testfile = true;
			ut_asserteq(FS_DT_REG, dent->type);
			ut_asserteq(12, dent->size);
		} else if (!strcmp(dent->name, "subdir")) {
			found_subdir = true;
			ut_asserteq(FS_DT_DIR, dent->type);
		} else if (!strcmp(dent->name, "link.txt")) {
			found_symlink = true;
			ut_asserteq(FS_DT_LNK, dent->type);
		}
	}

	ext4l_closedir_legacy(dirs);

	/* Verify we found expected entries */
	ut_assert(found_testfile);
	ut_assert(found_subdir);
	ut_assert(found_symlink);
	/* At least ., .., testfile.txt, subdir, link.txt */
	ut_assert(count >= 5);

	/* Now test reading the subdirectory */
	ut_assertok(fs_set_blk_dev("host", "0", FS_TYPE_ANY));
	ut_assertok(ext4l_opendir_legacy("/subdir", &dirs));
	ut_assertnonnull(dirs);

	count = 0;
	while (!ext4l_readdir_legacy(dirs, &dent)) {
		ut_assertnonnull(dent);
		count++;
		if (!strcmp(dent->name, "nested.txt")) {
			found_nested = true;
			ut_asserteq(FS_DT_REG, dent->type);
			ut_asserteq(12, dent->size);
		}
	}

	ext4l_closedir_legacy(dirs);

	ut_assert(found_nested);
	/* At least ., .., nested.txt */
	ut_assert(count >= 3);

	return 0;
}
FS_TEST_ARGS(fs_test_ext4l_opendir_norun, UTF_SCAN_FDT | UTF_CONSOLE |
	     UTF_MANUAL, { "fs_image", UT_ARG_STR });

/**
 * fs_test_ext4l_exists_norun() - Test ext4l_exists_legacy function
 *
 * Verifies that ext4l_exists_legacy correctly reports file existence.
 *
 * Arguments:
 *   fs_image: Path to the ext4 filesystem image
 */
static int fs_test_ext4l_exists_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(EXT4L_ARG_IMAGE);

	ut_assertnonnull(fs_image);
	ut_assertok(run_commandf("host bind 0 %s", fs_image));
	ut_assertok(fs_set_blk_dev("host", "0", FS_TYPE_ANY));

	/* Test existing directory */
	ut_asserteq(1, ext4l_exists_legacy("/"));

	/* Test non-existent paths */
	ut_asserteq(0, ext4l_exists_legacy("/no/such/path"));

	return 0;
}
FS_TEST_ARGS(fs_test_ext4l_exists_norun, UTF_SCAN_FDT | UTF_CONSOLE |
	     UTF_MANUAL, { "fs_image", UT_ARG_STR });

/**
 * fs_test_ext4l_size_norun() - Test ext4l_size_legacy function
 *
 * Verifies that ext4l_size_legacy correctly reports file size.
 *
 * Arguments:
 *   fs_image: Path to the ext4 filesystem image
 */
static int fs_test_ext4l_size_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(EXT4L_ARG_IMAGE);
	loff_t size;

	ut_assertnonnull(fs_image);
	ut_assertok(run_commandf("host bind 0 %s", fs_image));
	ut_assertok(fs_set_blk_dev("host", "0", FS_TYPE_ANY));

	/* Test root directory size - one block on a 4K block filesystem */
	ut_assertok(ext4l_size_legacy("/", &size));
	ut_asserteq(SZ_4K, size);

	/* Test file size - testfile.txt contains "hello world\n" */
	ut_assertok(ext4l_size_legacy("/testfile.txt", &size));
	ut_asserteq(12, size);

	/* Test non-existent path returns -ENOENT */
	ut_asserteq(-ENOENT, ext4l_size_legacy("/no/such/path", &size));

	return 0;
}
FS_TEST_ARGS(fs_test_ext4l_size_norun, UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });

/**
 * fs_test_ext4l_read_norun() - Test ext4l_read_legacy function
 *
 * Verifies that ext4l can read file contents.
 *
 * Arguments:
 *   fs_image: Path to the ext4 filesystem image
 */
static int fs_test_ext4l_read_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(EXT4L_ARG_IMAGE);
	loff_t actread;
	char buf[32];

	ut_assertnonnull(fs_image);
	ut_assertok(run_commandf("host bind 0 %s", fs_image));
	ut_assertok(fs_set_blk_dev("host", "0", FS_TYPE_ANY));

	/* Read the test file - contains "hello world\n" (12 bytes) */
	memset(buf, '\0', sizeof(buf));
	ut_assertok(ext4l_read_legacy("/testfile.txt", buf, 0, 0, &actread));
	ut_asserteq(12, actread);
	ut_asserteq_str("hello world\n", buf);

	/* Test partial read with offset */
	memset(buf, '\0', sizeof(buf));
	ut_assertok(ext4l_read_legacy("/testfile.txt", buf, 6, 5, &actread));
	ut_asserteq(5, actread);
	ut_asserteq_str("world", buf);

	/* Verify read returns error for non-existent path */
	ut_asserteq(-ENOENT,
		    ext4l_read_legacy("/no/such/file", buf, 0, 10,
				      &actread));

	return 0;
}
FS_TEST_ARGS(fs_test_ext4l_read_norun, UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });

/**
 * fs_test_ext4l_uuid_norun() - Test ext4l_uuid function
 *
 * Verifies that ext4l can return the filesystem UUID.
 *
 * Arguments:
 *   fs_image: Path to the ext4 filesystem image
 */
static int fs_test_ext4l_uuid_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(EXT4L_ARG_IMAGE);
	char uuid_str[UUID_STR_LEN + 1];

	ut_assertnonnull(fs_image);
	ut_assertok(run_commandf("host bind 0 %s", fs_image));
	ut_assertok(fs_set_blk_dev("host", "0", FS_TYPE_ANY));

	/* Get the UUID string */
	ut_assertok(ext4l_uuid(uuid_str));

	/* Verify it's a valid UUID format (xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx) */
	ut_asserteq(UUID_STR_LEN, strlen(uuid_str));
	ut_asserteq('-', uuid_str[8]);
	ut_asserteq('-', uuid_str[13]);
	ut_asserteq('-', uuid_str[18]);
	ut_asserteq('-', uuid_str[23]);

	return 0;
}
FS_TEST_ARGS(fs_test_ext4l_uuid_norun, UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });

/**
 * fs_test_ext4l_fsinfo_norun() - Test fsinfo command
 *
 * Verifies that the fsinfo command displays filesystem statistics.
 *
 * Arguments:
 *   fs_image: Path to the ext4 filesystem image
 */
static int fs_test_ext4l_fsinfo_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(EXT4L_ARG_IMAGE);
	struct fs_statfs stats;
	u64 used;

	ut_assertnonnull(fs_image);
	ut_assertok(run_commandf("host bind 0 %s", fs_image));
	ut_assertok(fs_set_blk_dev("host", "0", FS_TYPE_ANY));
	ut_assertok(ext4l_statfs_legacy(&stats));
	used = stats.blocks - stats.bfree;

	console_record_reset_enable();
	ut_assertok(run_commandf("fsinfo host 0"));

	/* Skip any EXT4-fs mount messages, check output format */
	ut_assert_skip_to_line("Block size: %lu bytes", stats.bsize);
	ut_assert_nextlinen("Total blocks: %llu (%llu bytes,",
			    stats.blocks, stats.blocks * stats.bsize);
	ut_assert_nextlinen("Used blocks: %llu (%llu bytes,",
			    used, used * stats.bsize);
	ut_assert_nextlinen("Free blocks: %llu (%llu bytes,",
			    stats.bfree, stats.bfree * stats.bsize);
	ut_assert_console_end();

	return 0;
}
FS_TEST_ARGS(fs_test_ext4l_fsinfo_norun, UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });

/**
 * fs_test_ext4l_statfs_norun() - Test ext4l_statfs_legacy function
 *
 * Verifies that ext4l can return filesystem statistics.
 *
 * Arguments:
 *   fs_image: Path to the ext4 filesystem image
 */
static int fs_test_ext4l_statfs_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(EXT4L_ARG_IMAGE);
	struct fs_statfs stats;

	ut_assertnonnull(fs_image);
	ut_assertok(run_commandf("host bind 0 %s", fs_image));
	ut_assertok(fs_set_blk_dev("host", "0", FS_TYPE_ANY));

	/* Get filesystem statistics */
	ut_assertok(ext4l_statfs_legacy(&stats));

	/* Verify reasonable values for a 64MB filesystem */
	ut_asserteq(SZ_4K, stats.bsize);
	ut_assert(stats.blocks > 0);
	ut_assert(stats.bfree > 0);
	ut_assert(stats.bfree <= stats.blocks);

	return 0;
}
FS_TEST_ARGS(fs_test_ext4l_statfs_norun, UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });

/**
 * fs_test_ext4l_write_norun() - Test ext4l_write_legacy function
 *
 * Verifies that ext4l can write file contents to the filesystem.
 *
 * Arguments:
 *   fs_image: Path to the ext4 filesystem image
 */
static int fs_test_ext4l_write_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(EXT4L_ARG_IMAGE);
	const char *test_data = "test write data\n";
	size_t test_len = strlen(test_data);
	loff_t actwrite, actread;
	char read_buf[32];
	loff_t size;

	ut_assertnonnull(fs_image);
	ut_assertok(run_commandf("host bind 0 %s", fs_image));
	ut_assertok(fs_set_blk_dev("host", "0", FS_TYPE_ANY));

	/* Write a new file */
	ut_assertok(ext4l_write_legacy("/newfile.txt", (void *)test_data,
				       0, test_len, &actwrite));
	ut_asserteq(test_len, actwrite);

	/* Verify the file exists and has correct size */
	ut_asserteq(1, ext4l_exists_legacy("/newfile.txt"));
	ut_assertok(ext4l_size_legacy("/newfile.txt", &size));
	ut_asserteq(test_len, size);

	/* Read back and verify contents */
	memset(read_buf, '\0', sizeof(read_buf));
	ut_assertok(ext4l_read_legacy("/newfile.txt", read_buf, 0, 0,
				      &actread));
	ut_asserteq(test_len, actread);
	ut_asserteq_str(test_data, read_buf);

	return 0;
}
FS_TEST_ARGS(fs_test_ext4l_write_norun, UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });

/**
 * fs_test_ext4l_unlink_norun() - Test ext4l_unlink_legacy function
 *
 * Verifies that ext4l can delete files from the filesystem.
 *
 * Arguments:
 *   fs_image: Path to the ext4 filesystem image
 */
static int fs_test_ext4l_unlink_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(EXT4L_ARG_IMAGE);
	const char *test_data = "unlink test\n";
	size_t test_len = strlen(test_data);
	loff_t actwrite;

	ut_assertnonnull(fs_image);
	ut_assertok(run_commandf("host bind 0 %s", fs_image));
	ut_assertok(fs_set_blk_dev("host", "0", FS_TYPE_ANY));

	/* Create a new file to unlink */
	ut_assertok(ext4l_write_legacy("/unlinkme.txt", (void *)test_data,
				       0, test_len, &actwrite));
	ut_asserteq(test_len, actwrite);

	/* Verify file exists (same mount) */
	ut_asserteq(1, ext4l_exists_legacy("/unlinkme.txt"));

	/* Unlink the file */
	ut_assertok(ext4l_unlink_legacy("/unlinkme.txt"));

	/* Verify file no longer exists */
	ut_asserteq(0, ext4l_exists_legacy("/unlinkme.txt"));

	/* Verify unlinking non-existent file returns -ENOENT */
	ut_asserteq(-ENOENT, ext4l_unlink_legacy("/nonexistent"));

	return 0;
}
FS_TEST_ARGS(fs_test_ext4l_unlink_norun, UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });

/**
 * fs_test_ext4l_mkdir_norun() - Test ext4l_mkdir_legacy function
 *
 * Verifies that ext4l can create directories on the filesystem.
 *
 * Arguments:
 *   fs_image: Path to the ext4 filesystem image
 */
static int fs_test_ext4l_mkdir_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(EXT4L_ARG_IMAGE);
	static int test_counter;
	char dir_name[32];
	char subdir_name[64];
	int ret;

	ut_assertnonnull(fs_image);
	ut_assertok(run_commandf("host bind 0 %s", fs_image));
	ut_assertok(fs_set_blk_dev("host", "0", FS_TYPE_ANY));

	/* Use unique directory names to avoid issues with test re-runs */
	snprintf(dir_name, sizeof(dir_name), "/testdir%d", test_counter);
	snprintf(subdir_name, sizeof(subdir_name), "%s/subdir", dir_name);
	test_counter++;

	/* Create a new directory */
	ret = ext4l_mkdir_legacy(dir_name);
	ut_assertok(ret);

	/* Verify directory exists */
	ut_asserteq(1, ext4l_exists_legacy(dir_name));

	/* Verify creating duplicate returns -EEXIST */
	ut_asserteq(-EEXIST, ext4l_mkdir_legacy(dir_name));

	/* Create nested directory */
	ut_assertok(ext4l_mkdir_legacy(subdir_name));
	ut_asserteq(1, ext4l_exists_legacy(subdir_name));

	/* Verify creating directory in non-existent parent returns -ENOENT */
	ut_asserteq(-ENOENT, ext4l_mkdir_legacy("/nonexistent/dir"));

	return 0;
}
FS_TEST_ARGS(fs_test_ext4l_mkdir_norun, UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });

/**
 * fs_test_ext4l_ln_norun() - Test ext4l_ln_legacy function
 *
 * Verifies that ext4l can create symbolic links on the filesystem.
 *
 * Arguments:
 *   fs_image: Path to the ext4 filesystem image
 */
static int fs_test_ext4l_ln_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(EXT4L_ARG_IMAGE);
	static int test_counter;
	char link_name[32];
	const char *target = "/testfile.txt";
	loff_t size;
	loff_t actread;
	char buf[32];

	ut_assertnonnull(fs_image);
	ut_assertok(run_commandf("host bind 0 %s", fs_image));
	ut_assertok(fs_set_blk_dev("host", "0", FS_TYPE_ANY));

	/* Use unique symlink names to avoid issues with test re-runs */
	snprintf(link_name, sizeof(link_name), "/testlink%d", test_counter);
	test_counter++;

	/*
	 * Create a symbolic link. ext4l_ln_legacy follows U-Boot's ln command
	 * convention: ext4l_ln_legacy(target, linkname) creates linkname
	 * pointing to target.
	 */
	ut_assertok(ext4l_ln_legacy(target, link_name));

	/* Verify symlink exists */
	ut_asserteq(1, ext4l_exists_legacy(link_name));

	/*
	 * Size through symlink should be target file's size (12 bytes),
	 * since ext4l_resolve_path follows symlinks (like stat, not lstat)
	 */
	ut_assertok(ext4l_size_legacy(link_name, &size));
	ut_asserteq(12, size);

	/* Verify we can read through the symlink */
	memset(buf, '\0', sizeof(buf));
	ut_assertok(ext4l_read_legacy(link_name, buf, 0, 0, &actread));
	ut_asserteq(12, actread);
	ut_asserteq_str("hello world\n", buf);

	/* Verify creating duplicate succeeds (like ln -sf) */
	ut_assertok(ext4l_ln_legacy(target, link_name));

	/* Verify creating symlink in non-existent parent returns -ENOENT */
	ut_asserteq(-ENOENT, ext4l_ln_legacy(target, "/nonexistent/link"));

	return 0;
}
FS_TEST_ARGS(fs_test_ext4l_ln_norun, UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });

/**
 * fs_test_ext4l_rename_norun() - Test ext4l_rename_legacy function
 *
 * Verifies that ext4l can rename files and directories on the filesystem.
 *
 * Arguments:
 *   fs_image: Path to the ext4 filesystem image
 */
static int fs_test_ext4l_rename_norun(struct unit_test_state *uts)
{
	const char *fs_image = ut_str(EXT4L_ARG_IMAGE);
	const char *test_data = "rename test\n";
	size_t test_len = strlen(test_data);
	static int test_counter;
	char old_name[32], new_name[32], subdir_name[32], moved_name[64];
	loff_t actwrite, size;

	ut_assertnonnull(fs_image);
	ut_assertok(run_commandf("host bind 0 %s", fs_image));
	ut_assertok(fs_set_blk_dev("host", "0", FS_TYPE_ANY));

	/* Use unique names to avoid issues with test re-runs */
	snprintf(old_name, sizeof(old_name), "/renameme%d.txt", test_counter);
	snprintf(new_name, sizeof(new_name), "/renamed%d.txt", test_counter);
	snprintf(subdir_name, sizeof(subdir_name), "/renamedir%d", test_counter);
	snprintf(moved_name, sizeof(moved_name), "%s/moved.txt", subdir_name);
	test_counter++;

	/* Create a file to rename */
	ut_assertok(ext4l_write_legacy(old_name, (void *)test_data,
				       0, test_len, &actwrite));
	ut_asserteq(test_len, actwrite);

	/* Verify file exists */
	ut_asserteq(1, ext4l_exists_legacy(old_name));

	/* Rename the file */
	ut_assertok(ext4l_rename_legacy(old_name, new_name));

	/* Verify old name no longer exists, new name does */
	ut_asserteq(0, ext4l_exists_legacy(old_name));
	ut_asserteq(1, ext4l_exists_legacy(new_name));

	/* Verify file size is preserved */
	ut_assertok(ext4l_size_legacy(new_name, &size));
	ut_asserteq(test_len, size);

	/* Verify renaming non-existent file returns -ENOENT */
	ut_asserteq(-ENOENT, ext4l_rename_legacy("/nonexistent", "/newname"));

	/* Test cross-directory rename */
	ut_assertok(ext4l_mkdir_legacy(subdir_name));
	ut_assertok(ext4l_rename_legacy(new_name, moved_name));
	ut_asserteq(0, ext4l_exists_legacy(new_name));
	ut_asserteq(1, ext4l_exists_legacy(moved_name));

	return 0;
}
FS_TEST_ARGS(fs_test_ext4l_rename_norun, UTF_SCAN_FDT | UTF_CONSOLE | UTF_MANUAL,
	     { "fs_image", UT_ARG_STR });
