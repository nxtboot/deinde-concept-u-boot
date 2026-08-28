// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the osd command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <dm.h>
#include <video_osd.h>
#include <asm/test.h>
#include <test/cmd.h>
#include <test/ut.h>

#define OSD_TEST_FLAGS	(UTF_CONSOLE | UTF_DM | UTF_SCAN_PDATA | UTF_SCAN_FDT)

/* the sandbox OSD is 10 characters square, at two bytes per cell */
#define OSD_MEMSIZE	(2 * 10 * 10)

/* the last line of the help text, which ends the usage message */
#define OSD_USAGE_LAST \
	"size [size_x] [size_y] - set OSD XY size in characters"

/*
 * Select the sandbox OSD. The current OSD is a static, so the driver-model
 * state restored before each test leaves it pointing into the previous test's
 * tree; every test must choose the device again.
 */
static int select_osd(struct unit_test_state *uts)
{
	ut_assertok(run_command("osd dev 0", 0));
	ut_assert_nextline("Setting osd to 0");
	ut_assert_console_end();

	return 0;
}

/*
 * Read the OSD memory back, splitting each cell into its character and its
 * colour. The sandbox driver records a colour as a letter: 'k' for black, 'w'
 * white, 'r' red, 'g' green and 'b' blue.
 */
static int read_osd(struct unit_test_state *uts, char *text, char *colors,
		    uint count)
{
	struct udevice *dev;
	u8 mem[OSD_MEMSIZE];
	uint i;

	ut_assertok(uclass_first_device_err(UCLASS_VIDEO_OSD, &dev));
	ut_assertok(sandbox_osd_get_mem(dev, mem, sizeof(mem)));

	for (i = 0; i < count; i++) {
		colors[i] = mem[2 * i];
		text[i] = mem[2 * i + 1];
	}

	return 0;
}

/* Test 'osd show' and 'osd dev' */
static int cmd_test_osd_base(struct unit_test_state *uts)
{
	ut_assertok(select_osd(uts));

	ut_assertok(run_command("osd dev", 0));
	ut_assert_nextline("Current osd is 0");
	ut_assert_console_end();

	/* the device is probed by now, so it is shown as active */
	ut_assertok(run_command("osd show", 0));
	ut_assert_nextline("OSD 0:\tosd  (active)");
	ut_assert_console_end();

	ut_assertok(run_command("osd show 0", 0));
	ut_assert_nextline("OSD 0:\tosd  (active)");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_osd_base, OSD_TEST_FLAGS);

/* Test 'osd print' writing text in a colour */
static int cmd_test_osd_print(struct unit_test_state *uts)
{
	char text[OSD_MEMSIZE / 2], colors[OSD_MEMSIZE / 2];

	ut_assertok(select_osd(uts));

	/* the display starts out full of black spaces */
	ut_assertok(read_osd(uts, text, colors, 12));
	ut_asserteq_mem("            ", text, 12);
	ut_asserteq_mem("kkkkkkkkkkkk", colors, 12);

	/* colour 3 is green */
	ut_assertok(run_command("osd print 1 1 3 Blah", 0));
	ut_assert_console_end();

	ut_assertok(read_osd(uts, text, colors, 20));
	ut_asserteq_mem("           Blah     ", text, 20);
	ut_asserteq_mem("kkkkkkkkkkkggggkkkkk", colors, 20);

	return 0;
}
CMD_TEST(cmd_test_osd_print, OSD_TEST_FLAGS);

/* Test 'osd write' writing raw cells */
static int cmd_test_osd_write(struct unit_test_state *uts)
{
	char text[OSD_MEMSIZE / 2], colors[OSD_MEMSIZE / 2];

	ut_assertok(select_osd(uts));

	/* the colour comes first in memory, so this is a green '-' */
	ut_assertok(run_command("osd write 0 0 672d", 0));
	ut_assert_console_end();

	ut_assertok(read_osd(uts, text, colors, 4));
	ut_asserteq_mem("-   ", text, 4);
	ut_asserteq_mem("gkkk", colors, 4);

	/* a count repeats the buffer along the display */
	ut_assertok(run_command("osd write 0 1 672d 4", 0));
	ut_assert_console_end();

	ut_assertok(read_osd(uts, text, colors, 16));
	ut_asserteq_mem("-         ----  ", text, 16);
	ut_asserteq_mem("gkkkkkkkkkggggkk", colors, 16);

	return 0;
}
CMD_TEST(cmd_test_osd_write, OSD_TEST_FLAGS);

/* Test 'osd size' changing the size of the display */
static int cmd_test_osd_size(struct unit_test_state *uts)
{
	struct video_osd_info info;
	struct udevice *dev;

	ut_assertok(select_osd(uts));

	ut_assertok(uclass_first_device_err(UCLASS_VIDEO_OSD, &dev));
	video_osd_get_info(dev, &info);
	ut_asserteq(10, info.width);
	ut_asserteq(10, info.height);

	ut_assertok(run_command("osd size 14 5", 0));
	ut_assert_console_end();

	video_osd_get_info(dev, &info);
	ut_asserteq(0x14, info.width);
	ut_asserteq(5, info.height);

	return 0;
}
CMD_TEST(cmd_test_osd_size, OSD_TEST_FLAGS);

/* Test 'osd' with an OSD which is not there */
static int cmd_test_osd_missing(struct unit_test_state *uts)
{
	ut_asserteq(1, run_command("osd dev 3", 0));
	ut_assert_nextline("Setting osd to 3");
	ut_assert_nextline("cmd_osd_set_osd_num: No OSD 3 (err = -19)");
	ut_assert_nextline("Failure changing osd number (err = -19)");
	ut_assert_console_end();

	ut_asserteq(1, run_command("osd show 3", 0));
	ut_assert_nextline("Invalid osd 3: err=-19");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_osd_missing, OSD_TEST_FLAGS);

/* Test 'osd' with a bad command line */
static int cmd_test_osd_usage(struct unit_test_state *uts)
{
	ut_assertok(select_osd(uts));

	/* the buffer must have a whole number of bytes in it */
	ut_asserteq(1, run_command("osd write 0 0 672", 0));
	ut_assert_nextline("osd - OSD sub-system");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_skip_to_line(OSD_USAGE_LAST);
	ut_assert_nextline_empty();
	ut_assert_console_end();

	/* and only hex digits */
	ut_asserteq(1, run_command("osd write 0 0 zzzz", 0));
	ut_assert_nextline("Hexadecimal input contained invalid characters");
	ut_assert_console_end();

	/* the sandbox driver knows five colours */
	ut_asserteq(1, run_command("osd print 0 0 9 hi", 0));
	ut_assert_nextline("Could not print string to osd osd");
	ut_assert_console_end();

	/* a position outside the display is refused */
	ut_asserteq(1, run_command("osd print 20 0 1 hi", 0));
	ut_assert_nextline("Could not print string to osd osd");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_osd_usage, OSD_TEST_FLAGS);
