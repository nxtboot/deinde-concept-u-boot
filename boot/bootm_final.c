// SPDX-License-Identifier: GPL-2.0+
/*
 * Cleanup before handing off to the OS
 *
 * Copyright 2025 Simon Glass <sjg@chromium.org>
 */

#include <bootm.h>
#include <bootstage.h>
#include <event.h>
#include <acpi/acpi_table.h>
#include <dm/root.h>

__weak void board_quiesce_devices(void)
{
}

void bootm_final(int flag)
{
	struct event_bootm_final final;
	int ret;

	printf("\nStarting kernel ...%s\n\n",
	       (flag & BOOTM_STATE_OS_FAKE_GO) ?
	       "(fake run for tracing)" : "");

	bootstage_mark_name(BOOTSTAGE_ID_BOOTM_HANDOFF, "start_kernel");

	/* Update FPDT boot performance record if it exists */
	if (IS_ENABLED(CONFIG_GENERATE_ACPI_TABLE) &&
	    IS_ENABLED(CONFIG_ACPIGEN))
		acpi_final_fpdt();

	if (IS_ENABLED(CONFIG_BOOTSTAGE_FDT) && IS_ENABLED(CONFIG_CMD_FDT))
		bootstage_fdt_add_report();
	bootstage_stash_default();
	if (IS_ENABLED(CONFIG_BOOTSTAGE_REPORT))
		bootstage_report();

	board_quiesce_devices();

	/*
	 * Call remove function of all devices with a removal flag set.
	 * This may be useful for last-stage operations, like cancelling
	 * of DMA operation or releasing device internal buffers.
	 * dm_remove_devices_active() ensures that vital devices are removed in
	 * a second round.
	 */
	dm_remove_devices_active();

	final.flag = flag;
	ret = event_notify(EVT_BOOTM_FINAL, &final, sizeof(final));
	if (ret) {
		printf("Event handler failed to finalise (err %dE\n",
		       ret);
		return;
	}
}
