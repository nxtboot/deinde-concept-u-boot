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
	struct efi_mem *mem;
	struct efi_var *var;
	struct efi_rt *rt;
	struct efi_net *net;
	struct efi_tcg2 *tcg2;
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
	mem = &st.mem;
	var = &st.var;
	rt = &st.rt;
	net = &st.net;
	tcg2 = &st.tcg2;
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
	ut_assert(list_empty(&bs->events));
	ut_assert(!bs->keep_devices);
	ut_asserteq_64(0, bs->mono_count);
	ut_asserteq(0, bs->call_count);
	ut_asserteq(EFI_OBJ_LIST_NOT_INIT, st.obj_list_initialized);
	ut_assert(list_empty(&mem->map));
	ut_asserteq(0, mem->map_key);
	ut_assertnull(var->buf);
	ut_assertnull(rt->virtmap);
	ut_assert(list_empty(&rt->mmio));
	ut_assertnull(var->flash);
	ut_assertnull(mem->bounce_buffer);
	ut_assertnull(st.debug.systab_pointer);
	ut_assert(list_empty(&st.hii.package_lists));
	ut_assertnull(st.initrd.handle);
	ut_assert(!st.secure_boot);
	ut_assertnull(st.watchdog_event);
	ut_asserteq(UCLASS_INVALID, st.system_partition.uclass_id);
	ut_assertnull(net->objs[0]);
	ut_assert(!net->dp_cache[0].is_valid);
	ut_assert(!net->dhcp_cache[0].is_valid);
	ut_asserteq(EFI_IP4_CONFIG2_POLICY_STATIC, net->ip4_policy);
	ut_asserteq(0, net->http_instances);
	ut_assertnull(tcg2->log.buffer);
	ut_assert(!tcg2->app_invoked);
	ut_assert(!net->http_last_head);
	ut_assertnull(st.bootefi.device_path);
	ut_asserteq_64(0, st.fdt.addr);

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

/* State which was in use before lib_test_efi_state_flag() ran */
static struct efi_state *flag_test_old;

/* Check that UTF_EFI gives a test a fresh state of its own */
static int lib_test_efi_state_flag(struct unit_test_state *uts)
{
	if (!IS_ENABLED(CONFIG_EFI_LOADER))
		return -EAGAIN;

	ut_assertnonnull(uts->efi_state);
	ut_assertnonnull(uts->saved_efi_state);
	ut_assert(uts->efi_state != uts->saved_efi_state);
	ut_asserteq_ptr(uts->efi_state, efis);

	/* it is as U-Boot leaves it at boot, whatever earlier tests did */
	ut_asserteq(EFI_OBJ_LIST_NOT_INIT, efis->obj_list_initialized);
	ut_assertnonnull(efis->bs.root);
	ut_assert(efis->bs.root != uts->saved_efi_state->bs.root);
	ut_assert(!list_empty(&efis->mem.map));
	ut_assert(!efis->con.no_ansi);

	/* change it, for lib_test_efi_state_flag_after() to look for */
	efi_console_set_ansi(false);
	ut_assert(efis->con.no_ansi);
	ut_assert(!uts->saved_efi_state->con.no_ansi);
	flag_test_old = uts->saved_efi_state;

	return 0;
}
LIB_TEST(lib_test_efi_state_flag, UTF_EFI);

/* Check that the state which a UTF_EFI test replaced is put back untouched */
static int lib_test_efi_state_flag_after(struct unit_test_state *uts)
{
	if (!IS_ENABLED(CONFIG_EFI_LOADER))
		return -EAGAIN;

	ut_assertnull(uts->efi_state);
	ut_assertnull(uts->saved_efi_state);

	/* this only means something if the test above has just run */
	if (flag_test_old) {
		ut_asserteq_ptr(flag_test_old, efis);
		ut_assert(!efis->con.no_ansi);
		flag_test_old = NULL;
	}

	return 0;
}
LIB_TEST(lib_test_efi_state_flag_after, 0);

/* Check that the EFI subsystem can be started in a state of its own */
static int lib_test_efi_state_start(struct unit_test_state *uts)
{
	efi_handle_t old_root = uts->saved_efi_state->bs.root;

	ut_asserteq(EFI_SUCCESS, efi_init_obj_list());
	ut_asserteq(EFI_SUCCESS, efis->obj_list_initialized);
	ut_assertnonnull(efis->systab.boottime);
	ut_assertnonnull(efis->var.buf);

	/* the state which was in use has not been touched */
	ut_asserteq_ptr(old_root, uts->saved_efi_state->bs.root);
	ut_assert(uts->saved_efi_state->var.buf != efis->var.buf);

	return 0;
}
LIB_TEST(lib_test_efi_state_start, UTF_EFI | UTF_SCAN_FDT);
