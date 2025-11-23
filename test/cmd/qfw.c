// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for qfw command
 *
 * Copyright 2025 Simon Glass <sjg@chromium.org>
 */

#include <console.h>
#include <dm.h>
#include <mapmem.h>
#include <qfw.h>
#include <asm/global_data.h>
#include <dm/test.h>
#include <test/cmd.h>
#include <test/ut.h>

DECLARE_GLOBAL_DATA_PTR;

/* Test 'qfw list' command */
static int cmd_test_qfw_list(struct unit_test_state *uts)
{
	struct fw_cfg_file_iter iter;
	struct fw_file *file;
	struct udevice *dev;

	ut_assertok(uclass_first_device_err(UCLASS_QFW, &dev));

	ut_assertok(run_command("qfw list", 0));
	ut_assert_nextline("    Addr     Size Sel Name");
	ut_assert_nextlinen("--");

	for (file = qfw_file_iter_init(dev, &iter); !qfw_file_iter_end(&iter);
	     file = qfw_file_iter_next(&iter)) {
		ut_assert_nextline("%8lx %8x %3x %-56s", file->addr,
				   be32_to_cpu(file->cfg.size),
				   be16_to_cpu(file->cfg.select),
				   file->cfg.name);
	}
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_qfw_list, UTF_CONSOLE);

/* Test 'qfw dump' command */
static int cmd_test_qfw_dump(struct unit_test_state *uts)
{
	if (IS_ENABLED(CONFIG_SANDBOX) || !IS_ENABLED(CONFIG_X86))
		return -EAGAIN;

	ut_assertok(run_command("qfw dump", 0));
	ut_assert_nextline("signature   = QEMU");
	ut_assert_nextlinen("id");
	ut_assert_nextlinen("uuid        = 00000000-0000-0000-0000-000000000000");
	ut_assert_nextline("ram_size    = %#.*lx", 2 * (int)sizeof(ulong),
			   (ulong)gd->ram_size);
	ut_assert_skip_to_linen("file dir le =");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_qfw_dump, UTF_CONSOLE);

/* Test 'qfw table' command */
static int cmd_test_qfw_table(struct unit_test_state *uts)
{
	if (IS_ENABLED(CONFIG_SANDBOX))
		return -EAGAIN;

	ut_assertok(run_command("qfw table", 0));
	ut_assert_nextline("  0 alloc: align 10 zone fseg name 'etc/acpi/rsdp'");
	if (IS_ENABLED(CONFIG_X86))
		ut_assert_nextline("  1 alloc: align 40 zone high name 'etc/acpi/tables'");

	/*
	 * we can't really test anything else as it may vary, so just check that
	 * there is more output after this
	 */
	ut_asserteq(true, ut_check_console_end(uts));

	return 0;
}
CMD_TEST(cmd_test_qfw_table, UTF_CONSOLE);

/* Test 'qfw arch' command */
static int cmd_test_qfw_arch(struct unit_test_state *uts)
{
	/*
	 * this test is really only useful on x86, which has some entries, but
	 * since the implementation of the 'qfw arch' command is generic, we can
	 * expect that it works on ARM too
	 */
	ut_assertok(run_command("qfw arch", 0));
	if (IS_ENABLED(CONFIG_X86)) {
		ut_assert_nextline("acpi tables = 0x00000000");
		ut_asserteq(true, ut_check_console_end(uts));
	}

	return 0;
}
CMD_TEST(cmd_test_qfw_arch, UTF_CONSOLE);

/* Test 'qfw read' command */
static int cmd_test_qfw_read(struct unit_test_state *uts)
{
	char *ptr = map_sysmem(0x1000, 0x100);

	if (IS_ENABLED(CONFIG_SANDBOX) || !IS_ENABLED(CONFIG_X86))
		return -EAGAIN;

	ut_assertok(run_command("qfw read 1000 etc/acpi/rsdp", 0));
	ut_asserteq_strn("RSD PTR ", ptr);

	return 0;
}
CMD_TEST(cmd_test_qfw_read, UTF_CONSOLE);

/* Test 'qfw e820' command */
static int cmd_test_qfw_e820(struct unit_test_state *uts)
{
	int ret;

	ret = run_command("qfw e820", 0);
	if (!IS_ENABLED(CONFIG_X86)) {
		ut_asserteq(1, ret);
		return 0;
	}

	ut_assertok(ret);
	ut_assert_nextline("        Addr        Size  Type");

	return 0;
}
CMD_TEST(cmd_test_qfw_e820, UTF_CONSOLE);
