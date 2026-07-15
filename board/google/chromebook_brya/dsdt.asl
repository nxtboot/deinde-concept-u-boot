/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 The coreboot project
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * Minimal DSDT for chromebook_brya: enough for the kernel to find the
 * PCI host bridge and boot. The layout follows coreboot's brya
 * dsdt.asl, with the host-bridge resources inlined and trimmed from
 * its common northbridge.asl. Device nodes (GPIO, audio and so on)
 * can be added as the bring-up progresses
 */

#include <acpi/acpi_table.h>
#include <asm/acpi/global_nvs.h>
#include "variant_ec.h"

DefinitionBlock(
	"dsdt.aml",
	"DSDT",
	0x02,		/* DSDT revision: ACPI v2.0 and up */
	OEM_ID,
	OEM_TABLE_ID,
	0x20260714	/* OEM revision */
)
{
	/* global NVS and variables */
	#include <asm/arch/acpi/globalnvs.asl>

	/* CPU */
	#include <asm/acpi/cpu.asl>

	Scope (\_SB) {
		Device (PCI0)
		{
			Name (_HID, EISAID ("PNP0A08"))	/* PCIe */
			Name (_CID, EISAID ("PNP0A03"))	/* PCI */
			Name (_SEG, 0)
			Name (_BBN, 0)

			Name (MCRS, ResourceTemplate ()
			{
				/* Bus numbers */
				WordBusNumber (ResourceProducer, MinFixed,
					       MaxFixed, PosDecode, 0, 0, 255,
					       0, 256)

				/* Legacy I/O and config-access ports */
				DWordIO (ResourceProducer, MinFixed, MaxFixed,
					 PosDecode, EntireRange, 0, 0, 0xcf7,
					 0, 0xcf8)
				IO (Decode16, 0xcf8, 0xcf8, 1, 8)

				/* I/O space */
				DWordIO (ResourceProducer, MinFixed, MaxFixed,
					 PosDecode, EntireRange, 0, 0x1000,
					 0xffff, 0, 0xf000)

				/* VGA memory */
				DWordMemory (ResourceProducer, PosDecode,
					     MinFixed, MaxFixed, Cacheable,
					     ReadWrite, 0, 0xa0000, 0xbffff,
					     0, 0x20000)

				/* PCI memory, matching the devicetree ranges */
				DWordMemory (ResourceProducer, PosDecode,
					     MinFixed, MaxFixed, NonCacheable,
					     ReadWrite, 0, 0xd0000000,
					     0xefffffff, 0, 0x20000000)

				/*
				 * The console UART's fixed BAR: exposing it
				 * as a window stops the kernel reassigning
				 * it, which would silence the console
				 */
				DWordMemory (ResourceProducer, PosDecode,
					     MinFixed, MaxFixed, NonCacheable,
					     ReadWrite, 0, 0xfe03e000,
					     0xfe03efff, 0, 0x1000)

				/* Prefetchable memory */
				QWordMemory (ResourceProducer, PosDecode,
					     MinFixed, MaxFixed, NonCacheable,
					     ReadWrite, 0, 0x90000000,
					     0xbfffffff, 0, 0x30000000)
			})

			Method (_CRS, 0, Serialized)
			{
				Return (MCRS)
			}

			/* The eSPI bridge, which hosts the EC */
			Device (LPCB)
			{
				Name (_ADR, 0x001f0000)
			}

			/* I2C1, which hosts the Cr50 TPM */
			Device (I2C1)
			{
				Name (_ADR, 0x00150001)

				Device (TPM)
				{
					Name (_HID, "GOOG0005")
					Name (_UID, 1)
					Name (_CRS, ResourceTemplate ()
					{
						I2cSerialBusV2 (0x50,
							ControllerInitiated,
							400000,
							AddressingMode7Bit,
							"\\_SB.PCI0.I2C1", 0,
							ResourceConsumer, ,
							Exclusive)
					})
				}
			}
		}
	}

	/* Chrome OS Embedded Controller */
	Scope (\_SB.PCI0.LPCB)
	{
		/* ACPI code for EC SuperIO functions */
		#include <asm/acpi/cros_ec/superio.asl>
		/* ACPI code for EC functions */
		#include <asm/acpi/cros_ec/ec.asl>
	}

	/* Chrome OS specific */
	#include <asm/acpi/chromeos.asl>

	/* Chipset specific sleep states */
	#include <asm/acpi/sleepstates.asl>
}
