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
	struct efi_console *con;
	struct efi_bs *bs;
	struct efi_system_table *systab;
	struct efi_state st, *old;

	if (!IS_ENABLED(CONFIG_EFI_LOADER))
		return -EAGAIN;

	old = efis;
	ut_assertnonnull(old);

	/* The new state starts out as at boot, whatever the old one holds */
	efi_state_init(&st);
	con = &st.con;
	bs = &st.bs;
	systab = &st.systab;
	ut_asserteq(1, con->mode.max_mode);
	ut_asserteq(80, con->modes[0].columns);
	ut_asserteq(25, con->modes[0].rows);
	ut_asserteq_ptr(&con->mode, con->con_out.mode);
	ut_assert(list_empty(&con->cin_notify));
	ut_assert(list_empty(&bs->obj_list));
	ut_assertnull(bs->root);
	ut_asserteq(TPL_APPLICATION, bs->tpl);
	ut_assert(list_empty(&bs->event_queue));
	ut_assert(bs->timers_enabled);
	ut_assert(list_empty(&bs->register_notify_events));
	ut_assertnull(bs->current_image);
	ut_asserteq(1, bs->entry_count);
	ut_asserteq(0, bs->nesting_level);
	ut_asserteq_64(EFI_SYSTEM_TABLE_SIGNATURE, systab->hdr.signature);
	ut_assertnonnull(systab->fw_vendor);
	ut_assertnonnull(systab->runtime);
	ut_assertnull(systab->boottime);
	ut_asserteq(0, systab->nr_tables);
	ut_asserteq(EFI_OBJ_LIST_NOT_INIT, st.obj_list_initialized);

	/* Select it and check that changes go into it, not the old one */
	ut_asserteq_ptr(old, efi_state_set(&st));
	ut_asserteq_ptr(&st, efis);
	efi_console_set_ansi(false);
	ut_assert(con->no_ansi);
	ut_assert(!old->con.no_ansi);

	/* The old state comes back untouched */
	ut_asserteq_ptr(&st, efi_state_set(old));
	ut_asserteq_ptr(old, efis);
	ut_assert(!efis->con.no_ansi);
	efi_console_set_ansi(true);
	ut_assert(con->no_ansi);

	return 0;
}
LIB_TEST(lib_test_efi_state, 0);
