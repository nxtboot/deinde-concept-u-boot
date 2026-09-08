// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the debug uclass
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <debug_dev.h>
#include <dm.h>
#include <asm/test.h>
#include <dm/test.h>
#include <test/test.h>
#include <test/ut.h>

/* Test that a string can be written to a debug device */
static int dm_test_debug_puts(struct unit_test_state *uts)
{
	struct udevice *dev;

	ut_assertok(uclass_first_device_err(UCLASS_DEBUG, &dev));
	sandbox_debug_clear(dev);

	ut_assertok(debug_dev_puts(dev, "hello"));
	ut_asserteq_str("hello", sandbox_debug_get(dev));

	/* a second write should append */
	ut_assertok(debug_dev_puts(dev, " world"));
	ut_asserteq_str("hello world", sandbox_debug_get(dev));

	return 0;
}
DM_TEST(dm_test_debug_puts, UTF_SCAN_FDT);

/*
 * Test that the uclass keeps calling the device until the whole string is
 * written, since a device may accept only part of it at a time
 */
static int dm_test_debug_partial(struct unit_test_state *uts)
{
	char buf[SANDBOX_DEBUG_SIZE];
	struct udevice *dev;
	int i;

	ut_assertok(uclass_first_device_err(UCLASS_DEBUG, &dev));
	sandbox_debug_clear(dev);

	/* exactly fills the device's buffer, in more than one call */
	for (i = 0; i < SANDBOX_DEBUG_SIZE - 1; i++)
		buf[i] = 'a' + (i % 26);
	buf[i] = '\0';

	ut_assertok(debug_dev_puts(dev, buf));
	ut_asserteq_str(buf, sandbox_debug_get(dev));

	/* there is no room left, so this must fail rather than truncate */
	ut_asserteq(-ENOSPC, debug_dev_puts(dev, "x"));

	return 0;
}
DM_TEST(dm_test_debug_partial, UTF_SCAN_FDT);

/* Test finding the first debug device and writing through it */
static int dm_test_debug_first(struct unit_test_state *uts)
{
	struct udevice *dev;

	ut_assertok(debug_dev_first(&dev));
	sandbox_debug_clear(dev);

	ut_assertok(debug_puts("via first"));
	ut_asserteq_str("via first", sandbox_debug_get(dev));

	return 0;
}
DM_TEST(dm_test_debug_first, UTF_SCAN_FDT);

/* Test that a debug device says something when it is probed */
static int dm_test_debug_announce(struct unit_test_state *uts)
{
	struct udevice *dev;

	ut_assertok(uclass_first_device_err(UCLASS_DEBUG, &dev));
	ut_asserteq_str("U-Boot debug device debug\n", sandbox_debug_get(dev));

	return 0;
}
DM_TEST(dm_test_debug_announce, UTF_SCAN_FDT);
