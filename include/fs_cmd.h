/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
 */

#ifndef _FS_CMD_H
#define _FS_CMD_H

#include <fs_common.h>

struct cmd_tbl;

/**
 * do_fat_fsload - Run the fatload command
 *
 * @cmdtp: Command information for fatload
 * @flag: Command flags (CMD_FLAG\_...)
 * @argc: Number of arguments
 * @argv: List of arguments
 * Return: result (see enum command_ret_t)
 */
int do_fat_fsload(struct cmd_tbl *cmdtp, int flag, int argc,
		  char *const argv[]);

/**
 * do_ext2load - Run the ext2load command
 *
 * @cmdtp: Command information for ext2load
 * @flag: Command flags (CMD_FLAG\_...)
 * @argc: Number of arguments
 * @argv: List of arguments
 * Return: result (see enum command_ret_t)
 */
int do_ext2load(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[]);

/*
 * Common implementation for various filesystem commands, optionally limited
 * to a specific filesystem type via the fstype parameter.
 */
int do_size(int argc, char *const argv[], int fstype);
int do_load(int argc, char *const argv[], int fstype);
int do_ls(int argc, char *const argv[], int fstype);
int file_exists(const char *dev_type, const char *dev_part, const char *file,
		int fstype);
int do_save(int argc, char *const argv[], int fstype);
int do_rm(int argc, char *const argv[], int fstype);
int do_mkdir(int argc, char *const argv[], int fstype);
int do_ln(int argc, char *const argv[], int fstype);
int do_mv(int argc, char *const argv[], int fstype);

/*
 * Determine the UUID of the specified filesystem and print it. Optionally it is
 * possible to store the UUID directly in env.
 */
int do_fs_uuid(int argc, char *const argv[], int fstype);

/*
 * Determine the type of the specified filesystem and print it. Optionally it is
 * possible to store the type directly in env.
 */
int do_fs_type(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[]);

/**
 * do_fs_types - List supported filesystems.
 *
 * @cmdtp: Command information for fstypes
 * @flag: Command flags (CMD_FLAG\_...)
 * @argc: Number of arguments
 * @argv: List of arguments
 * Return: result (see enum command_ret_t)
 */
int do_fs_types(struct cmd_tbl *cmdtp, int flag, int argc, char * const argv[]);

/**
 * do_fs_statfs - Get filesystem statistics.
 *
 * @cmdtp: Command information for fsinfo
 * @flag: Command flags (CMD_FLAG\_...)
 * @argc: Number of arguments
 * @argv: List of arguments
 * Return: result (see enum command_ret_t)
 */
int do_fs_statfs(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[]);

#endif
