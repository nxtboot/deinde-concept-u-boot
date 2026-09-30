/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * Gigabyte MZ33-AR1: the eight PCIe root complexes of the AMD EPYC Turin
 * processor. The windows match those U-Boot programs into the data fabric,
 * from the root-bus nodes in the devicetree
 */

DefinitionBlock("dsdt.aml", "DSDT", 2, "U-BOOT", "U-BOOTBL", 0x00010000)
{
	/*
	 * Grant the OS native control of PCIe features, given _OSC's UUID and
	 * capabilities buffer
	 */
	Method (POSC, 2)
	{
		CreateDWordField (Arg1, 0, CDW1)
		If (Arg0 != ToUUID ("33db4d5b-1ff7-401c-9657-7441c03dd766")) {
			CDW1 |= 4	/* unrecognised UUID */
		}
		Return (Arg1)
	}

	Scope (\_SB)
	{
		Device (PC00)
		{
			Name (_HID, EisaId ("PNP0A08"))
			Name (_CID, EisaId ("PNP0A03"))
			Name (_UID, 0)
			Name (_SEG, 0)
			Name (_BBN, 0x00)
			Name (_CRS, ResourceTemplate ()
			{
				WordBusNumber (ResourceProducer, MinFixed, MaxFixed,
					PosDecode, 0x0000, 0x0000, 0x001f, 0x0000,
					0x0020)
				IO (Decode16, 0x0cf8, 0x0cf8, 1, 8)
				WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode,
					EntireRange, 0x0000, 0x0000, 0x0cf7, 0x0000,
					0x0cf8)
				WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode,
					EntireRange, 0x0000, 0x0d00, 0x0fff, 0x0000,
					0x0300)
				DWordMemory (ResourceProducer, PosDecode, MinFixed,
					MaxFixed, Cacheable, ReadWrite, 0x00000000,
					0x000a0000, 0x000bffff, 0x00000000, 0x00020000)
				WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode,
					EntireRange, 0x0000, 0x1000, 0x1fff, 0x0000,
					0x1000)
				DWordMemory (ResourceProducer, PosDecode, MinFixed,
					MaxFixed, NonCacheable, ReadWrite, 0x00000000,
					0xf1000000, 0xfebfffff, 0x00000000, 0x0dc00000)
			})
			Method (_OSC, 4)
			{
				Return (POSC (Arg0, Arg3))
			}
		}

		Device (PC20)
		{
			Name (_HID, EisaId ("PNP0A08"))
			Name (_CID, EisaId ("PNP0A03"))
			Name (_UID, 1)
			Name (_SEG, 0)
			Name (_BBN, 0x20)
			Name (_CRS, ResourceTemplate ()
			{
				WordBusNumber (ResourceProducer, MinFixed, MaxFixed,
					PosDecode, 0x0000, 0x0020, 0x003f, 0x0000,
					0x0020)
				WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode,
					EntireRange, 0x0000, 0x2000, 0x2fff, 0x0000,
					0x1000)
				DWordMemory (ResourceProducer, PosDecode, MinFixed,
					MaxFixed, NonCacheable, ReadWrite, 0x00000000,
					0xce000000, 0xd4ffffff, 0x00000000, 0x07000000)
			})
			Method (_OSC, 4)
			{
				Return (POSC (Arg0, Arg3))
			}
		}

		Device (PC40)
		{
			Name (_HID, EisaId ("PNP0A08"))
			Name (_CID, EisaId ("PNP0A03"))
			Name (_UID, 2)
			Name (_SEG, 0)
			Name (_BBN, 0x40)
			Name (_CRS, ResourceTemplate ()
			{
				WordBusNumber (ResourceProducer, MinFixed, MaxFixed,
					PosDecode, 0x0000, 0x0040, 0x005f, 0x0000,
					0x0020)
				WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode,
					EntireRange, 0x0000, 0x3000, 0x3fff, 0x0000,
					0x1000)
				DWordMemory (ResourceProducer, PosDecode, MinFixed,
					MaxFixed, NonCacheable, ReadWrite, 0x00000000,
					0xc7000000, 0xcdffffff, 0x00000000, 0x07000000)
			})
			Method (_OSC, 4)
			{
				Return (POSC (Arg0, Arg3))
			}
		}

		Device (PC60)
		{
			Name (_HID, EisaId ("PNP0A08"))
			Name (_CID, EisaId ("PNP0A03"))
			Name (_UID, 3)
			Name (_SEG, 0)
			Name (_BBN, 0x60)
			Name (_CRS, ResourceTemplate ()
			{
				WordBusNumber (ResourceProducer, MinFixed, MaxFixed,
					PosDecode, 0x0000, 0x0060, 0x007f, 0x0000,
					0x0020)
				WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode,
					EntireRange, 0x0000, 0x4000, 0x4fff, 0x0000,
					0x1000)
				DWordMemory (ResourceProducer, PosDecode, MinFixed,
					MaxFixed, NonCacheable, ReadWrite, 0x00000000,
					0xc0000000, 0xc6ffffff, 0x00000000, 0x07000000)
			})
			Method (_OSC, 4)
			{
				Return (POSC (Arg0, Arg3))
			}
		}

		Device (PC80)
		{
			Name (_HID, EisaId ("PNP0A08"))
			Name (_CID, EisaId ("PNP0A03"))
			Name (_UID, 4)
			Name (_SEG, 0)
			Name (_BBN, 0x80)
			Name (_CRS, ResourceTemplate ()
			{
				WordBusNumber (ResourceProducer, MinFixed, MaxFixed,
					PosDecode, 0x0000, 0x0080, 0x009f, 0x0000,
					0x0020)
				WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode,
					EntireRange, 0x0000, 0x5000, 0x5fff, 0x0000,
					0x1000)
				DWordMemory (ResourceProducer, PosDecode, MinFixed,
					MaxFixed, NonCacheable, ReadWrite, 0x00000000,
					0xd5000000, 0xdbffffff, 0x00000000, 0x07000000)
			})
			Method (_OSC, 4)
			{
				Return (POSC (Arg0, Arg3))
			}
		}

		Device (PCA0)
		{
			Name (_HID, EisaId ("PNP0A08"))
			Name (_CID, EisaId ("PNP0A03"))
			Name (_UID, 5)
			Name (_SEG, 0)
			Name (_BBN, 0xa0)
			Name (_CRS, ResourceTemplate ()
			{
				WordBusNumber (ResourceProducer, MinFixed, MaxFixed,
					PosDecode, 0x0000, 0x00a0, 0x00bf, 0x0000,
					0x0020)
				WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode,
					EntireRange, 0x0000, 0x6000, 0x6fff, 0x0000,
					0x1000)
				DWordMemory (ResourceProducer, PosDecode, MinFixed,
					MaxFixed, NonCacheable, ReadWrite, 0x00000000,
					0xdc000000, 0xe2ffffff, 0x00000000, 0x07000000)
			})
			Method (_OSC, 4)
			{
				Return (POSC (Arg0, Arg3))
			}
		}

		Device (PCC0)
		{
			Name (_HID, EisaId ("PNP0A08"))
			Name (_CID, EisaId ("PNP0A03"))
			Name (_UID, 6)
			Name (_SEG, 0)
			Name (_BBN, 0xc0)
			Name (_CRS, ResourceTemplate ()
			{
				WordBusNumber (ResourceProducer, MinFixed, MaxFixed,
					PosDecode, 0x0000, 0x00c0, 0x00df, 0x0000,
					0x0020)
				WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode,
					EntireRange, 0x0000, 0x7000, 0x7fff, 0x0000,
					0x1000)
				DWordMemory (ResourceProducer, PosDecode, MinFixed,
					MaxFixed, NonCacheable, ReadWrite, 0x00000000,
					0xe3000000, 0xe9ffffff, 0x00000000, 0x07000000)
			})
			Method (_OSC, 4)
			{
				Return (POSC (Arg0, Arg3))
			}
		}

		Device (PCE0)
		{
			Name (_HID, EisaId ("PNP0A08"))
			Name (_CID, EisaId ("PNP0A03"))
			Name (_UID, 7)
			Name (_SEG, 0)
			Name (_BBN, 0xe0)
			Name (_CRS, ResourceTemplate ()
			{
				WordBusNumber (ResourceProducer, MinFixed, MaxFixed,
					PosDecode, 0x0000, 0x00e0, 0x00ff, 0x0000,
					0x0020)
				WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode,
					EntireRange, 0x0000, 0x8000, 0x8fff, 0x0000,
					0x1000)
				DWordMemory (ResourceProducer, PosDecode, MinFixed,
					MaxFixed, NonCacheable, ReadWrite, 0x00000000,
					0xea000000, 0xf0ffffff, 0x00000000, 0x07000000)
			})
			Method (_OSC, 4)
			{
				Return (POSC (Arg0, Arg3))
			}
		}
	}
}
