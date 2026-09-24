// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for identifying the x86 CPU
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

/* global_data.h first, for the types which asm/cpu.h uses */
#include <asm/global_data.h>
#include <asm/cpu.h>
#include <linux/string.h>
#include <test/lib.h>
#include <test/ut.h>

DECLARE_GLOBAL_DATA_PTR;

/* Test looking up the vendor from its cpuid string */
static int lib_test_x86_vendor(struct unit_test_state *uts)
{
	ut_asserteq(X86_VENDOR_INTEL, x86_vendor_from_name("GenuineIntel"));
	ut_asserteq(X86_VENDOR_AMD, x86_vendor_from_name("AuthenticAMD"));
	ut_asserteq(X86_VENDOR_UNKNOWN, x86_vendor_from_name("NotARealCPU!"));

	return 0;
}
LIB_TEST(lib_test_x86_vendor, 0);

/* Test decoding the family, model and stepping from cpuid leaf 1 */
static int lib_test_x86_decode_fms(struct unit_test_state *uts)
{
	u8 family, model, mask;

	/* QEMU's default CPU: family 0xf, with extended model 6 */
	x86_decode_fms(0x00060fb1, &family, &model, &mask);
	ut_asserteq(0xf, family);
	ut_asserteq(0x6b, model);
	ut_asserteq(1, mask);

	/* family 6 uses the extended model, e.g. an Intel Coffee Lake */
	x86_decode_fms(0x000906ea, &family, &model, &mask);
	ut_asserteq(6, family);
	ut_asserteq(0x9e, model);
	ut_asserteq(0xa, mask);

	/* an AMD Zen 5: family 0xf plus extended family 0xb */
	x86_decode_fms(0x00b00f21, &family, &model, &mask);
	ut_asserteq(0x1a, family);
	ut_asserteq(2, model);
	ut_asserteq(1, mask);

	/* before family 6 the extended model is not used */
	x86_decode_fms(0x000f0543, &family, &model, &mask);
	ut_asserteq(5, family);
	ut_asserteq(4, model);
	ut_asserteq(3, mask);

	return 0;
}
LIB_TEST(lib_test_x86_decode_fms, 0);

/* Test that the CPU identity comes from cpuid, in 32-bit and 64-bit builds */
static int lib_test_x86_identity(struct unit_test_state *uts)
{
	struct cpuid_result res;
	u8 family, model, mask;
	char name[13];

	res = cpuid(0);
	memcpy(name, &res.ebx, 4);
	memcpy(name + 4, &res.edx, 4);
	memcpy(name + 8, &res.ecx, 4);
	name[12] = '\0';
	ut_assert(gd->arch.x86_vendor != X86_VENDOR_UNKNOWN);
	ut_asserteq(x86_vendor_from_name(name), gd->arch.x86_vendor);

	x86_decode_fms(cpuid_eax(1), &family, &model, &mask);
	ut_asserteq(family, gd->arch.x86);
	ut_asserteq(model, gd->arch.x86_model);
	ut_asserteq(mask, gd->arch.x86_mask);

	return 0;
}
LIB_TEST(lib_test_x86_identity, 0);
