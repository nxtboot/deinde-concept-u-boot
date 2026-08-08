// SPDX-License-Identifier: GPL-2.0+
/*
 * Test for smbios command
 *
 * Copyright 2025 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <smbios.h>
#include <asm/global_data.h>
#include <test/cmd.h>
#include <test/ut.h>

DECLARE_GLOBAL_DATA_PTR;

/* Test the 'smbios' command */
static int cmd_smbios_test(struct unit_test_state *uts)
{
	uint hdr_size = ALIGN(sizeof(struct smbios3_entry), 16);

	/* Test basic smbios command and verify expected output */
	ut_assertok(run_command("smbios", 0));

	ut_assert_nextline("SMBIOS 3.7.0 present.");
	ut_assert_nextlinen("7 structures occupying ");
	ut_assert_nextlinen("Table at %lx", gd_smbios_start() + hdr_size);
	ut_assert_nextline_empty();
	ut_assert_nextlinen("Handle 0x0000, DMI type 0, 26 bytes at");
	ut_assert_nextline("BIOS Information");
	ut_assert_nextline("\tVendor: Deinde Concept U-Boot");
	ut_assert_nextlinen("\tBIOS Version: ");
	ut_assert_nextlinen("\tBIOS Release Date:");
	ut_assert_nextline("\tBIOS ROM Size: 0x00");
	ut_assert_nextline("\tBIOS Characteristics: 0x0000000000010880");
	ut_assert_nextline("\tBIOS Characteristics Extension Byte 1: 0x01");
	ut_assert_nextline("\tBIOS Characteristics Extension Byte 2: 0x0c");
	ut_assert_nextlinen("\tSystem BIOS Major Release:");
	ut_assert_nextlinen("\tSystem BIOS Minor Release:");
	ut_assert_nextline("\tEmbedded Controller Firmware Major Release: 0xff");
	ut_assert_nextline("\tEmbedded Controller Firmware Minor Release: 0xff");
	ut_assert_nextline("\tExtended BIOS ROM Size: 0x0000");
	ut_assert_nextline_empty();
	ut_assert_nextlinen("Handle 0x0001, DMI type 1, 27 bytes at");
	ut_assert_nextline("System Information");
	ut_assert_nextline("\tManufacturer: Sandbox Corp");
	ut_assert_nextline("\tProduct Name: Sandbox Computer");
	ut_assert_nextline("\tVersion: 1.0");
	ut_assert_nextline("\tSerial Number: SB12345678");
	ut_assert_nextline("\tUUID: 00000000-0000-0000-0000-000000000000");
	ut_assert_nextline("\tWake-up Type: Unknown");
	ut_assert_nextline("\tSKU Number: SANDBOX-SKU");
	ut_assert_nextline("\tFamily: Sandbox_Family");
	ut_assert_nextline_empty();
	ut_assert_nextlinen("Handle 0x0002, DMI type 2, 15 bytes at");
	ut_assert_nextline("Baseboard Information");
	ut_assert_nextline("\tManufacturer: Sandbox Boards");
	ut_assert_nextline("\tProduct Name: Sandbox Motherboard");
	ut_assert_nextline("\tVersion: ");
	ut_assert_nextline("\tSerial Number: ");
	ut_assert_nextline("\tAsset Tag: SB-ASSET-001");
	ut_assert_nextline("\tFeature Flags: 0x00");
	ut_assert_nextline("\tChassis Location: ");
	ut_assert_nextline("\tChassis Handle: 0x0003");
	ut_assert_nextline("\tBoard Type: Unknown");
	ut_assert_nextline("\tNumber of Contained Object Handles: 0x00");
	ut_assert_nextline_empty();
	ut_assert_nextlinen("Handle 0x0003, DMI type 3, 22 bytes at");
	ut_assert_nextline("Chassis Information");
	ut_assert_nextline("\tManufacturer: Sandbox Chassis Inc");
	ut_assert_nextline("\tType: 0x02");
	ut_assert_nextline("\tVersion: ");
	ut_assert_nextline("\tSerial Number: ");
	ut_assert_nextline("\tAsset Tag: ");
	ut_assert_nextline("\tBoot-up State: Unknown");
	ut_assert_nextline("\tPower Supply State: Unknown");
	ut_assert_nextline("\tThermal State: Unknown");
	ut_assert_nextline("\tSecurity Status: Unknown");
	ut_assert_nextline("\tOEM-defined: 0x00000000");
	ut_assert_nextline("\tHeight: 0x00");
	ut_assert_nextline("\tNumber of Power Cords: 0x00");
	ut_assert_nextline("\tContained Element Count: 0x00");
	ut_assert_nextline("\tContained Element Record Length: 0x00");
	ut_assert_nextline("\tSKU Number: ");
	ut_assert_nextline_empty();
	ut_assert_nextlinen("Handle 0x0004, DMI type 4, 50 bytes at");
	ut_assert_nextline("Processor Information:");
	ut_assert_nextline("\tSocket Designation: ");
	ut_assert_nextline("\tProcessor Type: Unknown");
	ut_assert_nextline("\tProcessor Family: Unknown");
	ut_assert_nextline("\tProcessor Manufacturer: Languid Example Garbage Inc.");
	ut_assert_nextline("\tProcessor ID word 0: 0x00000000");
	ut_assert_nextline("\tProcessor ID word 1: 0x00000000");
	ut_assert_nextline("\tProcessor Version: LEG Inc. SuperMegaUltraTurbo CPU No. 1");
	ut_assert_nextline("\tVoltage: 0x00");
	ut_assert_nextline("\tExternal Clock: 0x0000");
	ut_assert_nextline("\tMax Speed: 0x0000");
	ut_assert_nextline("\tCurrent Speed: 0x0000");
	ut_assert_nextline("\tStatus: 0x00");
	ut_assert_nextline("\tProcessor Upgrade: Unknown");
	ut_assert_nextline("\tL1 Cache Handle: 0xffff");
	ut_assert_nextline("\tL2 Cache Handle: 0xffff");
	ut_assert_nextline("\tL3 Cache Handle: 0xffff");
	ut_assert_nextline("\tSerial Number: ");
	ut_assert_nextline("\tAsset Tag: ");
	ut_assert_nextline("\tPart Number: ");
	ut_assert_nextline("\tCore Count: 0x00");
	ut_assert_nextline("\tCore Enabled: 0x00");
	ut_assert_nextline("\tThread Count: 0x00");
	ut_assert_nextline("\tProcessor Characteristics: 0x0000");
	ut_assert_nextline("\tProcessor Family 2: [0000]");
	ut_assert_nextline("\tCore Count 2: 0x0000");
	ut_assert_nextline("\tCore Enabled 2: 0x0000");
	ut_assert_nextline("\tThread Count 2: 0x0000");
	ut_assert_nextline("\tThread Enabled: 0x0000");
	ut_assert_nextline_empty();
	ut_assert_nextlinen("Handle 0x0005, DMI type 32, 11 bytes at");
	ut_assert_nextline("Header and Data:");
	ut_assert_nextline("\t00000000: 20 0b 05 00 00 00 00 00 00 00 00");
	ut_assert_nextline_empty();
	ut_assert_nextlinen("Handle 0x0006, DMI type 127, 4 bytes at");
	ut_assert_nextline("End Of Table");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_smbios_test, UTF_CONSOLE);

/* Test invalid smbios command */
static int cmd_smbios_invalid_test(struct unit_test_state *uts)
{
	/* Test smbios command with invalid arguments */
	ut_asserteq(1, run_command("smbios invalid", 0));

	return 0;
}
CMD_TEST(cmd_smbios_invalid_test, UTF_CONSOLE);
