// SPDX-License-Identifier: GPL-2.0+
/*
 * Test of the EFI state
 *
 * Copyright 2026 Google LLC
 */

#include <efi_loader.h>
#include <test/lib.h>
#include <test/test.h>
#include <test/ut.h>

/* Check that a test can use an EFI state of its own and switch back */
static int lib_test_efi_state(struct unit_test_state *uts)
{
	struct efi_state st, *old;

	if (!IS_ENABLED(CONFIG_EFI_LOADER))
		return -EAGAIN;

	old = efis;
	ut_assertnonnull(old);

	/* Select a new state and check that it is the one in use */
	efi_state_init(&st);
	ut_asserteq_ptr(old, efi_state_set(&st));
	ut_asserteq_ptr(&st, efis);

	/* The old state comes back */
	ut_asserteq_ptr(&st, efi_state_set(old));
	ut_asserteq_ptr(old, efis);

	return 0;
}
LIB_TEST(lib_test_efi_state, 0);
