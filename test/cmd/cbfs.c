// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the cbfs commands
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <cbfs.h>
#include <env.h>
#include <malloc.h>
#include <mapmem.h>
#include <asm/byteorder.h>
#include <test/cmd.h>
#include <test/ut.h>

/* Size of the ROM the tests build, and the alignment of the files in it */
#define ROM_SIZE	0x100
#define ROM_ALIGN	0x40

/* Offset of the first file within the ROM; the master header is at the start */
#define ROM_DATA_OFF	0x40
#define CBFS_LOAD_ADDR	(CONFIG_SYS_LOAD_ADDR + 0x1000)

/* Version coreboot writes into the master header */
#define CBFS_VERSION	0x31313132

/* Contents of the two files */
#define HELLO_SIZE	16
#define HELLO_BYTE	0x11
#define UBOOT_SIZE	32
#define UBOOT_BYTE	0x22

/* A third file with no name and a type the command does not know */
#define ODD_SIZE	16
#define ODD_BYTE	0x33
#define ODD_TYPE	17

/* What build_rom() puts in the ROM */
enum rom_t {
	ROMT_EMPTY,	/* a master header with no files after it */
	ROMT_FILES,	/* a raw file called hello and a payload called u-boot */
	ROMT_ODD,	/* those two files, plus the odd one */
};

/**
 * add_file() - Write a file header and its data into the ROM
 *
 * The name sits directly after the header and the data after that, aligned to
 * 16 bytes as coreboot does it.
 *
 * @ptr: Position in the ROM to write the file at
 * @name: Name of the file
 * @type: CBFS file type
 * @byte: Byte to fill the data with
 * @size: Number of bytes of data
 * Return: Position of the next file, aligned as the header says
 */
static void *add_file(void *ptr, const char *name, uint type, int byte,
		      uint size)
{
	struct cbfs_fileheader *fh = ptr;
	uint offset;

	offset = ALIGN(sizeof(*fh) + strlen(name) + 1, 16);
	memcpy(&fh->magic, "LARCHIVE", sizeof(fh->magic));
	fh->len = cpu_to_be32(size);
	fh->type = cpu_to_be32(type);
	fh->attributes_offset = 0;
	fh->offset = cpu_to_be32(offset);
	memcpy(fh->filename, name, strlen(name) + 1);
	memset(ptr + offset, byte, size);

	return ptr + ALIGN(offset + size, ROM_ALIGN);
}

/**
 * build_rom() - Build a small CBFS in memory
 *
 * The master header lies at the start of the ROM and the offset back to it in
 * the last four bytes, whatever files are asked for. The caller frees the ROM
 * with free_rom() once it has finished with it.
 *
 * @uts: Test state
 * @type: What to put in the ROM
 * @endp: Returns the address of the last byte of the ROM, which is what
 *	cbfsinit takes
 * @romp: Returns the ROM, for free_rom()
 * Return: 0 if OK, other value on error
 */
static int build_rom(struct unit_test_state *uts, enum rom_t type, ulong *endp,
		     void **romp)
{
	struct cbfs_header *hdr;
	void *rom, *ptr;

	rom = memalign(ROM_ALIGN, ROM_SIZE);
	ut_assertnonnull(rom);
	memset(rom, '\0', ROM_SIZE);

	hdr = rom;
	hdr->magic = cpu_to_be32(CBFS_HEADER_MAGIC);
	hdr->version = cpu_to_be32(CBFS_VERSION);
	hdr->rom_size = cpu_to_be32(ROM_SIZE);
	hdr->boot_block_size = 0;
	hdr->align = cpu_to_be32(ROM_ALIGN);
	hdr->offset = cpu_to_be32(ROM_DATA_OFF);

	if (type != ROMT_EMPTY) {
		ptr = add_file(rom + ROM_DATA_OFF, "hello", CBFS_TYPE_RAW,
			       HELLO_BYTE, HELLO_SIZE);
		ptr = add_file(ptr, "u-boot", CBFS_TYPE_PAYLOAD, UBOOT_BYTE,
			       UBOOT_SIZE);
		if (type == ROMT_ODD)
			add_file(ptr, "", ODD_TYPE, ODD_BYTE, ODD_SIZE);
	}

	/*
	 * The last four bytes hold the offset from just past the end of the
	 * ROM back to the master header, in the endianness of the machine
	 */
	*(u32 *)(rom + ROM_SIZE - sizeof(u32)) = -ROM_SIZE;

	*endp = map_to_sysmem(rom) + ROM_SIZE - 1;
	*romp = rom;

	return 0;
}

/**
 * free_rom() - Drop a ROM built by build_rom()
 *
 * The driver caches pointers into the ROM, so this leaves it uninitialised,
 * both to keep it away from the freed memory and so that each test starts from
 * the same state whatever ran before it.
 *
 * @uts: Test state
 * @rom: ROM to free
 * @end: Address of the last byte of the ROM
 * Return: 0 if OK, other value on error
 */
static int free_rom(struct unit_test_state *uts, void *rom, ulong end)
{
	/* clearing the magic is enough to make the next init fail */
	*(u32 *)rom = 0;
	ut_asserteq(1, run_commandf("cbfsinit %lx", end));
	ut_assert_nextline("Bad CBFS header.");
	free(rom);

	return 0;
}

/* Test reading a CBFS into memory */
static int cmd_test_cbfsinit_base(struct unit_test_state *uts)
{
	ulong end;
	void *rom;

	ut_assertok(build_rom(uts, ROMT_FILES, &end, &rom));

	/* the command says nothing when it works */
	ut_assertok(run_commandf("cbfsinit %lx", end));
	ut_assert_console_end();

	/* the files are in RAM now, so listing them needs no more of the ROM */
	ut_assertok(run_command("cbfsls", 0));
	ut_assert_skip_to_line("       %d           payload  u-boot",
			       UBOOT_SIZE);
	ut_assert_nextline_empty();
	ut_assert_nextline("2 file(s)");
	ut_assert_nextline_empty();
	ut_assert_console_end();

	ut_assertok(free_rom(uts, rom, end));
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_cbfsinit_base, UTF_CONSOLE);

/* Test the ways cbfsinit can fail */
static int cmd_test_cbfsinit_bad(struct unit_test_state *uts)
{
	ulong end;
	void *rom;

	ut_assertok(build_rom(uts, ROMT_FILES, &end, &rom));

	/* an address which is not a hex number is refused before any reading */
	ut_asserteq(1, run_command("cbfsinit zz", 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("** Invalid end of ROM **");
	ut_assert_console_end();

	/* so is a ROM whose master header has the wrong magic */
	*(u32 *)rom = 0;
	ut_asserteq(1, run_commandf("cbfsinit %lx", end));
	ut_assert_nextline("Bad CBFS header.");
	ut_assert_console_end();

	/* a failed init leaves nothing behind for the other commands */
	ut_asserteq(1, run_command("cbfsls", 0));
	ut_assert_nextline("CBFS not initialized.");
	ut_assert_console_end();

	free(rom);

	return 0;
}
CMD_TEST(cmd_test_cbfsinit_bad, UTF_CONSOLE);

/* Test showing the master header of a CBFS */
static int cmd_test_cbfsinfo_base(struct unit_test_state *uts)
{
	ulong end;
	void *rom;

	ut_assertok(build_rom(uts, ROMT_FILES, &end, &rom));
	ut_assertok(run_commandf("cbfsinit %lx", end));

	ut_assertok(run_command("cbfsinfo", 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("CBFS version: %#x", CBFS_VERSION);
	ut_assert_nextline("ROM size: %#x", ROM_SIZE);

	/* the ROM has no bootblock, so the archive is all of it but the gap */
	ut_assert_nextline("Boot block size: 0x0");
	ut_assert_nextline("CBFS size: %#x", ROM_SIZE - ROM_DATA_OFF);
	ut_assert_nextline("Alignment: %d", ROM_ALIGN);
	ut_assert_nextline("Offset: %#x", ROM_DATA_OFF);
	ut_assert_nextline_empty();
	ut_assert_console_end();

	ut_assertok(free_rom(uts, rom, end));
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_cbfsinfo_base, UTF_CONSOLE);

/* Test cbfsinfo with no CBFS to report on, and with an argument */
static int cmd_test_cbfsinfo_bad(struct unit_test_state *uts)
{
	ulong end;
	void *rom;

	/* make sure nothing an earlier test read is still around */
	ut_assertok(build_rom(uts, ROMT_FILES, &end, &rom));
	ut_assertok(free_rom(uts, rom, end));
	ut_assert_console_end();

	ut_asserteq(1, run_command("cbfsinfo", 0));
	ut_assert_nextline("CBFS not initialized.");
	ut_assert_console_end();

	/* the command takes no arguments, so one is a usage error */
	ut_asserteq(1, run_command("cbfsinfo x", 0));
	ut_assert_nextline("cbfsinfo - print information about filesystem");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");

	/* the rest is the help text, which is not what this test is about */
	console_record_reset();

	return 0;
}
CMD_TEST(cmd_test_cbfsinfo_bad, UTF_CONSOLE);

/* Test listing the files in a CBFS */
static int cmd_test_cbfsls_base(struct unit_test_state *uts)
{
	ulong end;
	void *rom;

	ut_assertok(build_rom(uts, ROMT_FILES, &end, &rom));
	ut_assertok(run_commandf("cbfsinit %lx", end));

	ut_assertok(run_command("cbfsls", 0));
	ut_assert_nextline("     size              type  name");
	ut_assert_nextline("------------------------------------------");
	ut_assert_nextline("       %d               raw  hello", HELLO_SIZE);
	ut_assert_nextline("       %d           payload  u-boot", UBOOT_SIZE);
	ut_assert_nextline_empty();
	ut_assert_nextline("2 file(s)");
	ut_assert_nextline_empty();
	ut_assert_console_end();

	ut_assertok(free_rom(uts, rom, end));
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_cbfsls_base, UTF_CONSOLE);

/* Test a file with no name and a type the command does not know */
static int cmd_test_cbfsls_odd(struct unit_test_state *uts)
{
	ulong end;
	void *rom;

	ut_assertok(build_rom(uts, ROMT_ODD, &end, &rom));
	ut_assertok(run_commandf("cbfsinit %lx", end));

	ut_assertok(run_command("cbfsls", 0));
	ut_assert_skip_to_line("       %d                %d  (empty)", ODD_SIZE,
			       ODD_TYPE);
	ut_assert_nextline_empty();
	ut_assert_nextline("3 file(s)");
	ut_assert_nextline_empty();
	ut_assert_console_end();

	ut_assertok(free_rom(uts, rom, end));
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_cbfsls_odd, UTF_CONSOLE);

/* Test listing a CBFS which holds no files */
static int cmd_test_cbfsls_empty(struct unit_test_state *uts)
{
	ulong end;
	void *rom;

	ut_assertok(build_rom(uts, ROMT_EMPTY, &end, &rom));
	ut_assertok(run_commandf("cbfsinit %lx", end));

	/*
	 * The command cannot tell an archive with no files from a driver which
	 * has nothing to say, so it reports the state it finds and fails
	 */
	ut_asserteq(1, run_command("cbfsls", 0));
	ut_assert_nextline("Success.");
	ut_assert_console_end();

	ut_assertok(free_rom(uts, rom, end));
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_cbfsls_empty, UTF_CONSOLE);

/* Test reading a CBFS file with the generic 'load' command */
static int cmd_test_cbfs_load(struct unit_test_state *uts)
{
	ulong end, addr = CBFS_LOAD_ADDR;
	void *rom, *buf;
	int i;

	ut_assertok(build_rom(uts, ROMT_FILES, &end, &rom));
	ut_assertok(run_commandf("cbfsinit %lx", end));
	ut_assert_console_end();

	buf = map_sysmem(addr, UBOOT_SIZE);
	memset(buf, '\0', UBOOT_SIZE);

	ut_assertok(run_commandf("load cbfs - %lx u-boot", addr));
	ut_assert_nextlinen("%d bytes read", UBOOT_SIZE);
	ut_assert_console_end();

	for (i = 0; i < UBOOT_SIZE; i++)
		ut_asserteq(UBOOT_BYTE, ((u8 *)buf)[i]);
	ut_asserteq(UBOOT_SIZE, env_get_hex("filesize", 0));

	unmap_sysmem(buf);
	ut_assertok(free_rom(uts, rom, end));
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_cbfs_load, UTF_CONSOLE);

/* Test that CBFS is offered as a filesystem, and what 'load' refuses */
static int cmd_test_cbfs_fs(struct unit_test_state *uts)
{
	ulong end, addr = CBFS_LOAD_ADDR;
	void *rom, *buf;
	int i;

	ut_assertok(run_command("fstypes", 0));
	ut_assert_nextlinen("Supported filesystems:");
	ut_assert_console_end();

	ut_assertok(build_rom(uts, ROMT_FILES, &end, &rom));
	ut_assertok(run_commandf("cbfsinit %lx", end));
	ut_assert_console_end();

	/* a byte count shorter than the file stops the read there */
	buf = map_sysmem(addr, UBOOT_SIZE);
	memset(buf, '\0', UBOOT_SIZE);
	ut_assertok(run_commandf("load cbfs - %lx u-boot %x", addr,
				 UBOOT_SIZE / 2));
	ut_assert_nextlinen("%d bytes read", UBOOT_SIZE / 2);
	ut_assert_console_end();

	for (i = 0; i < UBOOT_SIZE / 2; i++)
		ut_asserteq(UBOOT_BYTE, ((u8 *)buf)[i]);
	for (; i < UBOOT_SIZE; i++)
		ut_asserteq(0, ((u8 *)buf)[i]);

	/*
	 * A file which is not there is refused. The message comes from
	 * log_err() in do_load(), whose prefix depends on the log format, so
	 * only the result is checked here
	 */
	ut_asserteq(1, run_commandf("load cbfs - %lx nope", addr));
	console_record_reset();

	unmap_sysmem(buf);
	ut_assertok(free_rom(uts, rom, end));
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_cbfs_fs, UTF_CONSOLE);
