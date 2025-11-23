// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for font command
 *
 * Copyright 2022 Google LLC
 */

#include <console.h>
#include <dm.h>
#include <video_console.h>
#include <test/ut.h>

/* Declare a new font test */
#define FONT_TEST(_name, _flags)	UNIT_TEST(_name, _flags, font)

/* Test the 'font' command */
static int font_test_base(struct unit_test_state *uts)
{
	struct udevice *dev;
	const char *name;
	int max_metrics;
	uint size;
	int ret;

	ut_assertok(uclass_first_device_err(UCLASS_VIDEO, &dev));
	ut_assertok(uclass_first_device_err(UCLASS_VIDEO_CONSOLE, &dev));

	ut_assertok(run_command("font list", 0));
	if (IS_ENABLED(CONFIG_VIDEO_FONT_8X16))
		ut_assert_nextline("8x16");
	if (IS_ENABLED(CONFIG_VIDEO_FONT_SUN12X22))
		ut_assert_nextline("12x22");
	if (IS_ENABLED(CONFIG_CONSOLE_TRUETYPE_NIMBUS))
		ut_assert_nextline("nimbus_sans_l_regular");
	if (IS_ENABLED(CONFIG_CONSOLE_TRUETYPE_ANKACODER))
		ut_assert_nextline("ankacoder_c75_r");
	if (IS_ENABLED(CONFIG_CONSOLE_TRUETYPE_CANTORAONE))
		ut_assert_nextline("cantoraone_regular");
	if (IS_ENABLED(CONFIG_CONSOLE_TRUETYPE_UBUNTU_LIGHT))
		ut_assert_nextline("ubuntu_light");
	if (IS_ENABLED(CONFIG_CONSOLE_TRUETYPE_UBUNTU_BOLD))
		ut_assert_nextline("ubuntu_bold");

	ut_assert_console_end();

	ut_assertok(vidconsole_get_font_size(dev, NULL, &name, &size));
	if (IS_ENABLED(CONFIG_CONSOLE_TRUETYPE_ANKACODER))
		ut_asserteq_str("ankacoder_c75_r", name);
	else if (IS_ENABLED(CONFIG_CONSOLE_TRUETYPE_NIMBUS))
		ut_asserteq_str("nimbus_sans_l_regular", name);
	else
		ut_asserteq_str("8x16", name);
	ut_asserteq(CONFIG_CONSOLE_TRUETYPE_SIZE, size);

	if (!IS_ENABLED(CONFIG_CONSOLE_TRUETYPE_CANTORAONE))
		return 0;

	max_metrics = 1;
	if (IS_ENABLED(CONFIG_CONSOLE_TRUETYPE))
		max_metrics = IF_ENABLED_INT(CONFIG_CONSOLE_TRUETYPE,
				     CONFIG_CONSOLE_TRUETYPE_MAX_METRICS);

	ret = run_command("font select cantoraone_regular 40", 0);
	if (max_metrics < 2) {
		ut_asserteq(1, ret);
		ut_assert_nextline("Failed (error -7)");
		ut_assert_console_end();
		return 0;
	}

	ut_assertok(ret);
	ut_assert_console_end();

	ut_assertok(vidconsole_get_font_size(dev, NULL, &name, &size));
	ut_asserteq_str("cantoraone_regular", name);
	ut_asserteq(40, size);
	ut_assertok(ut_check_console_end(uts));

	ut_assertok(run_command("font size", 0));
	ut_assert_nextline("40");
	ut_assertok(ut_check_console_end(uts));

	ut_assertok(run_command("font size 30", 0));
	ut_assert_console_end();

	ut_assertok(run_command("font size", 0));
	ut_assert_nextline("30");
	ut_assertok(ut_check_console_end(uts));

	ut_assertok(run_command("font select", 0));
	ut_assertok(ut_check_console_end(uts));

	ut_assertok(vidconsole_get_font_size(dev, NULL, &name, &size));
	ut_asserteq_str("nimbus_sans_l_regular", name);
	ut_asserteq(CONFIG_CONSOLE_TRUETYPE_SIZE, size);

	return 0;
}
FONT_TEST(font_test_base, UTF_SCAN_PDATA | UTF_SCAN_FDT | UTF_CONSOLE |
	  UTF_DM);

/* Test 'font info' command */
static int font_test_info(struct unit_test_state *uts)
{
	int count;

	if (!CONFIG_IS_ENABLED(VIDEO_GLYPH_STATS))
		return -EAGAIN;

	count = gd_glyph_count();
	ut_assertok(run_command("font info", 0));
	ut_assert_nextline("glyphs rendered: %u", count);
	ut_assert_console_end();

	return 0;
}
FONT_TEST(font_test_info, UTF_CONSOLE);
