// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 Google LLC
 * Written by Simon Glass <sjg@chromium.org>
 */

#include <efi_api.h>
#include <efi_loader.h>
#include <efi_log.h>
#include <mapmem.h>
#include <test/lib.h>
#include <test/test.h>
#include <test/ut.h>

/* basic test of logging */
static int lib_test_efi_log_base(struct unit_test_state *uts)
{
	void **buf = map_sysmem(0x1000, 0);
	u64 *addr = map_sysmem(0x1010, 0);
	int ofs1, ofs2;

	ut_assertok(efi_log_reset());

	ofs1 = efi_logs_testing(EFI_LOG_TEST0, 123, &buf[0], &addr[0]);

	ofs2 = efi_logs_testing(EFI_LOG_TEST1, 456, &buf[1], &addr[1]);

	/* simulate an EFI call setting the return values */
	addr[0] = 0x100;
	buf[0] = map_sysmem(0x1100, 0);
	addr[1] = 0x200;
	buf[1] = map_sysmem(0x1200, 0);

	ut_assertok(efi_loge_testing(ofs2, EFI_LOAD_ERROR));
	ut_assertok(efi_loge_testing(ofs1, EFI_SUCCESS));

	ut_assertok(efi_log_show());
	ut_assert_nextline("EFI log (size ac)");
	ut_assert_nextline("times are [start_us +duration_us] since boot");
	ut_assert_nextlinen(
		"  0      testing test0 int 7b/123 buf 1000 mem 1010 *buf 1100 *mem 100 ret OK");
	ut_assert_nextlinen(
		"  1      testing test1 int 1c8/456 buf 1008 mem 1018 *buf 1200 *mem 200 ret load");
	ut_assert_nextline("2 records");
	ut_assert_console_end();

	unmap_sysmem(buf);
	unmap_sysmem(addr);

	return 0;
}
LIB_TEST(lib_test_efi_log_base, UTF_CONSOLE);

/* test the memory-function logging */
static int lib_test_efi_log_mem(struct unit_test_state *uts)
{
	void **buf = map_sysmem(0x1000, 0);
	u64 *addr = map_sysmem(0x1010, 0);
	int ofs1, ofs2;

	ut_assertok(efi_log_reset());

	ofs1 = efi_logs_allocate_pool(EFI_BOOT_SERVICES_DATA, 100, buf);
	ofs2 = efi_logs_allocate_pages(EFI_ALLOCATE_ANY_PAGES,
				       EFI_BOOT_SERVICES_CODE, 10, addr);
	ut_assertok(efi_loge_allocate_pages(ofs2, EFI_LOAD_ERROR));
	ut_assertok(efi_loge_allocate_pool(ofs1, 0));

	ofs1 = efi_logs_free_pool(*buf);
	ut_assertok(efi_loge_free_pool(ofs1, EFI_INVALID_PARAMETER));

	ofs2 = efi_logs_free_pages(*addr, 0);
	ut_assertok(efi_loge_free_pool(ofs2, 0));

	ut_assertok(efi_log_show());

	ut_assert_nextline("EFI log (size e4)");
	ut_assert_nextline("times are [start_us +duration_us] since boot");

	/*
	 * We end up with internal sandbox-addresses here since EFI_LOADER
	 * doesn't handle map_sysmem() correctly. So for now, only part of the
	 * string is matched.
	 */
	ut_assert_nextlinen("  0   alloc_pool bt-data size 64/100 buf 10002000 *buf");
	ut_assert_nextlinen("  1  alloc_pages any-pages bt-code pgs a/10 mem 10002010 *mem");
	ut_assert_nextlinen("  2    free_pool buf");
	ut_assert_nextlinen("  3   free_pages mem");

	ut_assert_nextline("4 records");

	unmap_sysmem(buf);
	unmap_sysmem(addr);

	return 0;
}
LIB_TEST(lib_test_efi_log_mem, UTF_CONSOLE);

/* Test that a HandleProtocol() call is not mistaken for OpenProtocol() */
static int lib_test_efi_log_handle_prot(struct unit_test_state *uts)
{
	efi_guid_t guid = EFI_DEVICE_PATH_PROTOCOL_GUID;
	void *handle = map_sysmem(0x1000, 0);
	void *intf = map_sysmem(0x1010, 0);
	int ofs;

	ut_assertok(efi_log_reset());

	/* a real OpenProtocol() call shows its attributes */
	ofs = efi_logs_open_protocol(handle, &guid, &intf, handle, NULL,
				     EFI_OPEN_PROTOCOL_GET_PROTOCOL);
	ut_assertok(efi_loge_open_protocol(ofs, EFI_SUCCESS));

	/* HandleProtocol() uses an attribute an app may not use itself */
	ofs = efi_logs_open_protocol(handle, &guid, &intf, handle, NULL,
				     EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
	ut_assertok(efi_loge_open_protocol(ofs, EFI_SUCCESS));

	ut_assertok(efi_log_show());

	ut_assert_nextlinen("EFI log (size ");
	ut_assert_nextline("times are [start_us +duration_us] since boot");

	/* the first shows its attributes, the second says where it came from */
	ut_assert_nextline_regex("  0    open_prot hdl .*Device Path attr 2 .*");
	ut_assert_nextline_regex("  1    open_prot hdl .*Device Path \\(HandleProtocol\\) .*");

	ut_assert_nextline("2 records");

	unmap_sysmem(handle);
	unmap_sysmem(intf);

	return 0;
}
LIB_TEST(lib_test_efi_log_handle_prot, UTF_CONSOLE);

/* Test the summary which the bootstage report shows */
static int lib_test_efi_log_summary(struct unit_test_state *uts)
{
	void **buf = map_sysmem(0x1000, 0);
	u64 *addr = map_sysmem(0x1010, 0);
	int ofs;

	ut_assertok(efi_log_reset());

	/* nothing is shown when the log is empty */
	efi_log_summary();
	ut_assert_console_end();

	ofs = efi_logs_allocate_pool(EFI_BOOT_SERVICES_DATA, 100, buf);
	ut_assertok(efi_loge_allocate_pool(ofs, 0));

	ofs = efi_logs_allocate_pages(EFI_ALLOCATE_ANY_PAGES,
				      EFI_BOOT_SERVICES_CODE, 10, addr);
	ut_assertok(efi_loge_allocate_pages(ofs, EFI_LOAD_ERROR));

	/* this one is left pending, as if the function never returned */
	efi_logs_free_pool(*buf);

	efi_log_summary();
	ut_assert_nextline_empty();
	ut_assert_nextlinen("EFI: 3 calls, 1 returned an error, 1 did not return");
	ut_assert_nextline("      alloc_pages 1");
	ut_assert_nextline("       alloc_pool 1");
	ut_assert_nextline("        free_pool 1");
	ut_assert_console_end();

	unmap_sysmem(buf);
	unmap_sysmem(addr);

	return 0;
}
LIB_TEST(lib_test_efi_log_summary, UTF_CONSOLE);

/* Test the simple boot services which take scalar arguments */
static int lib_test_efi_log_misc(struct unit_test_state *uts)
{
	efi_guid_t guid = EFI_DEVICE_PATH_PROTOCOL_GUID;
	u64 *count = map_sysmem(0x1000, 0);
	u32 *crc32_p = map_sysmem(0x1010, 0);
	void *table = map_sysmem(0x1020, 0);
	int ofs;

	ut_assertok(efi_log_reset());

	ofs = efi_logs_install_configuration_table(&guid, table);
	ut_assertok(efi_loge_install_configuration_table(ofs, EFI_SUCCESS));

	*count = 0x42;
	ofs = efi_logs_get_next_monotonic_count(count);
	ut_assertok(efi_loge_get_next_monotonic_count(ofs, EFI_SUCCESS));

	ofs = efi_logs_stall(1000);
	ut_assertok(efi_loge_stall(ofs, EFI_SUCCESS));

	ofs = efi_logs_set_watchdog_timer(5, 0x99, 0, NULL);
	ut_assertok(efi_loge_set_watchdog_timer(ofs, EFI_UNSUPPORTED));

	*crc32_p = 0xabcd;
	ofs = efi_logs_calculate_crc32(table, 0x100, crc32_p);
	ut_assertok(efi_loge_calculate_crc32(ofs, EFI_SUCCESS));

	ut_assertok(efi_log_show());

	ut_assert_nextlinen("EFI log (size ");
	ut_assert_nextline("times are [start_us +duration_us] since boot");

	/*
	 * As with the memory tests above, pointers show internal
	 * sandbox-addresses, so only part of each line is matched
	 */
	ut_assert_nextlinen("  0 inst_cfg_tab Device Path table ");
	ut_assert_nextlinen("  1 next_mono_cnt count ");
	ut_assert_nextlinen("  2        stall us 3e8/1000 ret OK");
	ut_assert_nextlinen("  3 set_watchdog timeout 5 code 99/153 size 0 data ");
	ut_assert_nextlinen("  4   calc_crc32 data ");

	ut_assert_nextline("5 records");

	unmap_sysmem(count);
	unmap_sysmem(crc32_p);
	unmap_sysmem(table);

	return 0;
}
LIB_TEST(lib_test_efi_log_misc, UTF_CONSOLE);

/* Test the generic record used for protocol member functions */
static int lib_test_efi_log_call(struct unit_test_state *uts)
{
	void *handle = map_sysmem(0x1000, 0);
	int ofs;

	ut_assertok(efi_log_reset());

	ofs = efi_logs_call(EFILP_SIMPLE_FS, EFILS_OPEN_VOLUME, 1,
			    (u64)map_to_sysmem(handle));
	ut_assertok(efi_loge_call(ofs, EFI_SUCCESS, 0));

	/* a read of 0x200 bytes which returns only 0x100 */
	ofs = efi_logs_call(EFILP_FILE, EFILF_READ, 2,
			    (u64)map_to_sysmem(handle), 0x200);
	ut_assertok(efi_loge_call(ofs, EFI_SUCCESS, 0x100));

	/* a boot service belongs to no protocol, so is named on its own */
	ofs = efi_logs_call(EFILP_NONE, EFILBS_RAISE_TPL, 1, 4);
	ut_assertok(efi_loge_call(ofs, EFI_SUCCESS, 0));

	/* an unknown protocol falls back to showing the numbers */
	ofs = efi_logs_call(98, 99, 1, 0x1234);
	ut_assertok(efi_loge_call(ofs, EFI_INVALID_PARAMETER, 0));

	ut_assertok(efi_log_show());

	ut_assert_nextlinen("EFI log (size ");
	ut_assert_nextline("times are [start_us +duration_us] since boot");

	ut_assert_nextlinen("  0 simple_fs.open_volume arg ");
	ut_assert_nextlinen("  1    file.read arg ");
	ut_assert_nextlinen("  2    raise_tpl arg 4 ret OK");
	ut_assert_nextlinen("  3        98.99 arg 1234/4660 ret inval_param");

	ut_assert_nextline("4 records");

	unmap_sysmem(handle);

	return 0;
}
LIB_TEST(lib_test_efi_log_call, UTF_CONSOLE);
