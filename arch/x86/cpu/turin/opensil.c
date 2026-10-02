// SPDX-License-Identifier: GPL-2.0+
/*
 * Glue between U-Boot and AMD's openSIL library
 *
 * openSIL does the silicon initialisation in three timepoints: the first,
 * before PCI enumeration, sets up the data fabric, the SMU, the root
 * complexes and their links, the CPU complexes, including starting the other
 * threads, and the FCH; the second runs once PCI resources are assigned and
 * the third before the OS starts. Before the first, the host gives openSIL
 * some memory and fills in its input blocks, much as Dasharo's coreboot does,
 * with the board's settings taken from the devicetree.
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#define LOG_CATEGORY LOGC_ARCH

#include <dm/ofnode.h>
#include <errno.h>
#include <log.h>
#include <malloc.h>
#include <stdarg.h>
#include <sysreset.h>
#include <vsprintf.h>
#include <asm/arch/opensil.h>
#include <linux/sizes.h>

#ifdef OPENSIL_MISSING
/*
 * The build found no openSIL tree but OPENSIL_ALLOW_MISSING let it go ahead,
 * so nothing sets up the SoC
 */
int turin_opensil_init(const void *ucode)
{
	log_err("openSIL: not built in, so the SoC is not set up\n");

	return -ENOSYS;
}

void turin_opensil_tp2(void)
{
}

void turin_opensil_tp3(void)
{
}
#else
#include <opensil_config.h>
#include <xSIM-api.h>
#include <CCX/CcxClass-api.h>
#include <CCX/Common/CcxApic.h>
#include <DF/DfClass-api.h>
#include <FCH/FchClass-api.h>
#include <FCH/FchHwAcpi-api.h>
#include <FCH/FchIsa-api.h>
#include <FCH/FchSata-api.h>
#include <FCH/FchUsb-api.h>
#include <Mpio/MpioClass-api.h>
#include <Mpio/Common/MpioStructs.h>
#include <Nbio/NbioClass-api.h>
#include <Cxl/CxlClass-api.h>
#include <RcMgr/DfX/RcManager4-api.h>
#include <Sdxi/SdxiClass-api.h>

/* The FCH's ACPI I/O blocks, as U-Boot lays them out (see cpu.c) */
#define ACPI_PM1_EVT_BLK	0x400
#define ACPI_PM1_CNT_BLK	0x404
#define ACPI_PM_TMR_BLK		0x408
#define ACPI_CPU_CNT_BLK	0x410
#define ACPI_GPE0_BLK		0x420

#define FCH_IOAPIC_ID		0x80
#define NBIO_IOAPIC_ID_BASE	0xf0

/* Bits of the AOAC device-enable map which openSIL keeps powered */
#define FCH_AOAC_DEV_AMBA	17
#define FCH_AOAC_DEV_ESPI	27

/* Board link flags in the /mpio node, as mpio.c reads them */
#define LINK_FLAG_REVERSED	BIT(0)
#define LINK_FLAG_TYPE_SHIFT	4
#define LINK_FLAG_TYPE_MASK	0xf
#define LINK_TYPE_SATA		1

#define NUM_XHCI		2
#define NUM_SATA		4

/*
 * openSIL's per-board USB PHY table (FCH_USB_OEM_PLATFORM_TABLE in
 * Dasharo's glue, which openSIL takes by address): the version and length,
 * then the USB2 and USB3.1 PHY settings for each socket, which this board
 * leaves disabled
 */
struct fch_usb_oem_table {
	u8 version_major;
	u8 version_minor;
	u8 table_length;
	u8 reserved0;
	u8 phys[173];
} __packed;

static struct fch_usb_oem_table usb_oem_table;

/* Send an openSIL message to the log, at the matching level */
static void opensil_debug(size_t level, const char *prefix, const char *msg,
			  const char *func, size_t line, ...)
{
	char buf[256];
	va_list args;
	int len;

	len = snprintf(buf, sizeof(buf), "%s%s:%zu: ", prefix, func, line);
	va_start(args, line);
	vsnprintf(buf + len, sizeof(buf) - len, msg, args);
	va_end(args);

	switch (level) {
	case SIL_TRACE_ERROR:
		log_err("%s", buf);
		break;
	case SIL_TRACE_WARNING:
		log_warning("%s", buf);
		break;
	case SIL_TRACE_INFO:
		log_debug("%s", buf);
		break;
	default:
		log_content("%s", buf);
		break;
	}
}

/* Read a byte array from the devicetree, leaving @buf as it is if absent */
static void read_bytes(ofnode node, const char *name, u8 *buf, int len)
{
	const void *val;
	int size;

	val = ofnode_read_prop(node, name, &size);
	if (val)
		memcpy(buf, val, min(size, len));
}

static void *find_block(SIL_DATA_BLOCK_ID id)
{
	void *blk = SilFindStructure(id, 0);

	if (!blk)
		log_err("openSIL: no input block %x\n", id);

	return blk;
}

/* Let openSIL share out the buses and MMIO among the root complexes */
static int setup_rc_manager(void)
{
	DFX_RCMGR_INPUT_BLK *rc = find_block(SilId_RcManager);
	DFCLASS_INPUT_BLK *df = find_block(SilId_DfClass);

	if (!rc || !df)
		return -ENOENT;
	rc->SetRcBasedOnNv = false;
	rc->SocketNumber = 1;
	rc->RbsPerSocket = 8;
	rc->McptEnable = true;
	rc->PciExpressBaseAddress = CONFIG_PCIE_ECAM_BASE;
	rc->BottomMmioReservedForPrimaryRb = SZ_4G - 32 * SZ_1M;
	rc->MmioSizePerRbForNonPciDevice = 16 * SZ_1M;
	/*
	 * Keep MMIO above 4GB below the 2^46 boundary, since on this board it
	 * reads as all ones at or above that, although CPUID reports more
	 * address bits. Under Dasharo's coreboot, which turns on SME, openSIL
	 * takes the memory-encryption bits off the address size and its
	 * windows stay below that boundary too
	 */
	rc->MmioAbove4GLimit = 1ULL << 46;
	log_debug("openSIL: MMIO limit %llx\n", rc->MmioAbove4GLimit);
	rc->Above4GMmioSizePerRbForNonPciDevice = 0;
	rc->AmdSmee = false;
	df->AmdPciExpressBaseAddress = CONFIG_PCIE_ECAM_BASE;

	return 0;
}

static int setup_ccx(const void *ucode)
{
	CCXCLASS_DATA_BLK *ccx = find_block(SilId_CcxClass);
	CCXCLASS_INPUT_BLK *in;

	if (!ccx)
		return -ENOENT;
	in = &ccx->CcxInputBlock;
	/* U-Boot's ACPI tables describe the CPUs by their xAPIC IDs */
	in->AmdApicMode = xApicMode;
	in->EnableAvx512 = 1;
	in->EnableSvmX2AVIC = true;
	in->EnableSvmAVIC = true;
	in->AmdCStateIoBaseAddress = ACPI_CPU_CNT_BLK;
	in->AmdSmee = false;
	in->AmdReserved = false;
	in->AmdVmplEnable = false;
	in->AmdSnpMemCover = false;
	/* openSIL loads the patch on each of the other threads */
	in->UcodePatchEntryInfo.UcodePatchEntryAddress = (ulong)ucode;

	return 0;
}

static int setup_fch(ofnode node)
{
	FCHHWACPI_INPUT_BLK *hwacpi = find_block(SilId_FchHwAcpiP);
	FCHCLASS_INPUT_BLK *fch = find_block(SilId_FchClass);
	FCHISA_INPUT_BLK *isa = find_block(SilId_FchIsa);
	FCHUSB_INPUT_BLK *usb = find_block(SilId_FchUsb);
	FCHSATA_INPUT_BLK *sata = find_block(SilId_FchSata);
	u8 usb2_oc[NUM_XHCI] = {}, usb3_oc[NUM_XHCI] = {};
	u8 shutdown[NUM_SATA] = {}, esata[NUM_SATA] = {};
	int i;

	if (!hwacpi || !fch || !isa || !usb || !sata)
		return -ENOENT;

	/* keep the SPI speeds which the PSP set */
	isa->SpiConfig.SpiSpeed = 0;
	isa->SpiConfig.SpiTpmSpeed = 0;
	isa->SpiConfig.WriteSpeed = 0;

	fch->FchBldCfg.CfgSioPmeBaseAddress = 0;
	fch->FchBldCfg.CfgAcpiPm1EvtBlkAddr = ACPI_PM1_EVT_BLK;
	fch->FchBldCfg.CfgAcpiPm1CntBlkAddr = ACPI_PM1_CNT_BLK;
	fch->FchBldCfg.CfgAcpiPmTmrBlkAddr = ACPI_PM_TMR_BLK;
	fch->FchBldCfg.CfgCpuControlBlkAddr = ACPI_CPU_CNT_BLK;
	fch->FchBldCfg.CfgAcpiGpe0BlkAddr = ACPI_GPE0_BLK;
	/* there is no SMI handler, so no SMI command port */
	fch->FchBldCfg.CfgSmiCmdPortAddr = 0;
	fch->CfgIoApicIdPreDefEnable = true;
	fch->FchIoApicId = FCH_IOAPIC_ID;
	fch->WdtEnable = false;
	fch->Misc.NoneSioKbcSupport = true;
	fch->FchRunTime.FchDeviceEnableMap = BIT(FCH_AOAC_DEV_AMBA) |
		BIT(FCH_AOAC_DEV_ESPI);
	hwacpi->PwrFailShadow = AlwaysOff;

	read_bytes(node, "amd,usb2-oc-pins", usb2_oc, NUM_XHCI);
	read_bytes(node, "amd,usb3-oc-pins", usb3_oc, NUM_XHCI);
	usb->Xhci0Enable = true;
	usb->Xhci1Enable = true;
	usb->Xhci2Enable = false;
	for (i = 0; i < NUM_XHCI; i++) {
		usb->XhciOCpinSelect[i].Usb20OcPin = usb2_oc[i];
		usb->XhciOCpinSelect[i].Usb31OcPin = usb3_oc[i];
	}
	usb->XhciOcPolarityCfgLow = ofnode_read_bool(node,
						     "amd,usb-oc-polarity-low");
	usb->Usb3PortForceGen1 = ofnode_read_u32_default(node,
						"amd,usb3-force-gen1", 0);
	usb_oem_table.version_major = 0xd;
	usb_oem_table.version_minor = 0x13;
	usb_oem_table.table_length = sizeof(usb_oem_table);
	usb->OemUsbConfigurationTable = (ulong)&usb_oem_table;

	read_bytes(node, "amd,sata-shutdown-ports", shutdown, NUM_SATA);
	read_bytes(node, "amd,sata-esata-ports", esata, NUM_SATA);
	for (i = 0; i < NUM_SATA; i++) {
		sata[i].SataEnable = true;
		sata[i].SataSetMaxGen2 = false;
		sata[i].SataMsiEnable = true;
		sata[i].SataRasSupport = true;
		sata[i].SataStaggeredSpinupEnable = true;
		sata[i].SataPortPower = shutdown[i];
		sata[i].SataEspPort = esata[i];
	}

	return 0;
}

/*
 * The settings Dasharo's coreboot uses for the root complexes and links, all
 * of them, since openSIL's defaults are not always zero
 */
static void setup_mpio_params(MPIOCLASS_INPUT_BLK *mpio)
{
	mpio->CfgDxioClockGating = 1;
	mpio->PcieDxioTimingControlEnable = 0;
	mpio->PCIELinkReceiverDetectionPolling = 0;
	mpio->PCIELinkResetToTrainingTime = 0;
	mpio->PCIELinkL0Polling = 0;
	mpio->PCIeExactMatchEnable = 0;
	mpio->DxioPhyValid = 1;
	mpio->DxioPhyProgramming = 1;
	mpio->CfgSkipPspMessage = 1;
	mpio->DxioSaveRestoreModes = 0xff;
	mpio->AmdAllowCompliance = 0xf;
	mpio->SrisEnableMode = 0xff;
	mpio->SrisSkipInterval = 0;
	mpio->SrisSkpIntervalSel = 1;
	mpio->SrisCfgType = 0;
	mpio->SrisAutoDetectMode = 0xf;
	mpio->SrisAutodetectFactor = 0;
	mpio->SrisLowerSkpOsGenSup = 0;
	mpio->SrisLowerSkpOsRcvSup = 0;
	mpio->AmdCxlOnAllPorts = 1;
	mpio->CxlCorrectableErrorLogging = 1;
	mpio->CxlUnCorrectableErrorLogging = 1;
	mpio->CfgAEREnable = 1;
	mpio->CfgMcCapEnable = 0;
	mpio->CfgRcvErrEnable = 0;
	mpio->EarlyBmcLinkTraining = 1;
	mpio->SurpriseDownFeature = 1;
	mpio->LcMultAutoSpdChgOnLastRateEnable = 0;
	mpio->AmdRxMarginEnabled = 1;
	mpio->CfgPcieCVTestWA = 0;
	mpio->CfgPcieAriSupport = 1;
	mpio->CfgNbioCTOtoSC = 0;
	mpio->CfgNbioCTOIgnoreError = 1;
	mpio->AmdPcieSubsystemDeviceID = 0x1453;
	mpio->AmdPcieSubsystemVendorID = 0x1022;
	mpio->GppAtomicOps = 1;
	mpio->GfxAtomicOps = 1;
	mpio->AmdNbioReportEdbErrors = 0;
	mpio->OpnSpare = 0;
	mpio->MPIOAncDataSupport = 1;
	mpio->AfterResetDelay = 0;
	mpio->CfgEarlyLink = 0;
	mpio->AmdCfgExposeUnusedPciePorts = 1;
	mpio->CfgForcePcieGenSpeed = 0xff;
	mpio->CfgSataPhyTuning = 0;
	mpio->PcieLinkComplianceModeAllPorts = 0;
	mpio->AmdMCTPEnable = 0;
	mpio->SbrBrokenLaneAvoidanceSup = 1;
	mpio->AutoFullMarginSup = 1;
	mpio->AmdPciePresetMask8GtAllPort = 0xffffffff;
	mpio->AmdPciePresetMask16GtAllPort = 0xffffffff;
	mpio->AmdPciePresetMask32GtAllPort = 0xffffffff;
	mpio->PcieLinkAspmAllPort = 0xff;
	mpio->SyncHeaderByPass = 1;
	mpio->CxlTempGen5AdvertAltPtcl = 0;
	mpio->CfgSevSnpSupport = 0;
	mpio->CfgSevTioSupport = 0;
	mpio->PcieIdeCapSup = 0;
	mpio->Master7bitSteeringTag = 1;
	mpio->AmdFabricSdxi = true;
}

static int setup_nbio(void)
{
	NBIOCLASS_DATA_BLOCK *nbio = find_block(SilId_NbioClass);
	CXLCLASS_DATA_BLK *cxl = find_block(SilId_CxlClass);
	SDXICLASS_INPUT_BLK *sdxi = find_block(SilId_SdxiClass);
	NBIO_CONFIG_DATA *in;

	if (!nbio || !cxl || !sdxi)
		return -ENOENT;
	cxl->CxlInputBlock.AmdPcieAerReportMechanism = 1;
	sdxi->AmdFabricSdxi = true;

	in = &nbio->NbioConfigData;
	in->EsmEnableAllRootPorts = false;
	in->EsmTargetSpeed = 16;
	in->CfgRxMarginPersistenceMode = 1;
	in->SevSnpSupport = false;
	in->IohcNonPCIBarInitIommuVf = false;
	in->IohcNonPCIBarInitIommuVfCntl = false;
	in->AerEnRccDev0 = false;
	in->CfgAEREnable = true;
	in->AtomicRoutingEnStrap5 = true;
	in->CfgSriovEnDev0F1 = true;
	in->CfgAriEnDev0F1 = true;
	in->CfgAerEnDev0F1 = true;
	in->CfgAcsEnDev0F1 = true;
	in->CfgAtsEnDev0F1 = true;
	in->CfgPasidEnDev0F1 = true;
	in->CfgRtrEnDev0F1 = true;
	in->CfgPriEnDev0F1 = true;
	in->CfgPwrEnDev0F1 = true;
	in->AtcEnable = true;
	in->NbifDev0F1AtomicRequestEn = true;
	in->AcsEnRccDev0 = true;
	in->AcsP2pReq = true;
	in->AcsSourceVal = true;
	in->RccDev0E2EPrefix = true;
	in->RccDev0ExtendedFmtSupported = true;
	in->CfgSyshubMgcgClkGating = 1;
	in->IoApicIdPreDefineEn = true;
	in->IoApicIdBase = NBIO_IOAPIC_ID_BASE;
	in->IommuAvicSupport = true;
	in->AmdApicMode = xApicMode;

	return 0;
}

/*
 * The board's links, from the same /mpio node which U-Boot's own MPIO code
 * reads: each entry is <first-lane lane-count flags devfn>, with the lanes
 * numbered as the MPIO numbers its PHY lanes. openSIL takes the board's own
 * lane numbers and maps them to the PHY itself; they are the same except in
 * a 16-lane group which the board wires in reverse, where they run the other
 * way. The BMC's link, which the ABL trains early, is given separately
 */
static int setup_mpio(ofnode node)
{
	MPIOCLASS_INPUT_BLK *mpio = find_block(SilId_MpioClass);
	DFX_RCMGR_INPUT_BLK *rc = find_block(SilId_RcManager);
	u32 cells[MAX_PORTS_SUPPORTED * 4];
	int count, i;
	u32 lane;

	if (!mpio || !rc)
		return -ENOENT;
	setup_mpio_params(mpio);

	count = ofnode_read_size(node, "amd,links");
	if (count <= 0 || count % 16 || count > sizeof(cells))
		return log_msg_ret("lnk", -EINVAL);
	count /= 16;
	if (ofnode_read_u32_array(node, "amd,links", cells, count * 4))
		return log_msg_ret("rd", -EINVAL);

	for (i = 0; i < count; i++) {
		MPIO_PORT_DESCRIPTOR *port = &mpio->PcieTopologyData.PortList[i];
		u32 *cell = &cells[i * 4];
		uint first = cell[0], last = cell[0] + cell[1] - 1;
		uint devfn = cell[3];

		if (cell[2] & LINK_FLAG_REVERSED) {
			uint group = first & ~15;

			first = group + (group + 15 - last);
			last = first + cell[1] - 1;
		}

		memset(port, '\0', sizeof(*port));
		if (((cell[2] >> LINK_FLAG_TYPE_SHIFT) & LINK_FLAG_TYPE_MASK) ==
		    LINK_TYPE_SATA) {
			port->EngineData = (MPIO_ENGINE_DATA)
				MPIO_ENGINE_DATA_INITIALIZER(MpioSATAEngine,
							     first, last, 0, 0);
			port->Port.PortPresent = 1;
		} else {
			port->EngineData = (MPIO_ENGINE_DATA)
				MPIO_ENGINE_DATA_INITIALIZER(MpioPcieEngine,
							     first, last, 0, 0);
			port->Port = (MPIO_PORT_DATA)
				MPIO_PORT_DATA_INITIALIZER_PCIE(MpioPortEnabled,
					devfn >> 3, devfn & 7, 0, 0, 0, 0, 0,
					0, 0);
		}
		port->Port.AlwaysExpose = 1;
		port->Port.SlotNum = i + 1;
	}
	if (count)
		mpio->PcieTopologyData.PortList[count - 1].Flags =
			DESCRIPTOR_TERMINATE_LIST;
	mpio->PcieTopologyData.PlatformData[0].Flags =
		DESCRIPTOR_TERMINATE_LIST;
	mpio->PcieTopologyData.PlatformData[0].PciePortList =
		mpio->PcieTopologyData.PortList;

	if (!ofnode_read_u32(node, "amd,bmc-lane", &lane)) {
		rc->BmcSocket = 0;
		rc->EarlyBmcLinkLaneNum = lane;
		mpio->EarlyBmcLinkSocket = 0;
		mpio->EarlyBmcLinkLaneNum = lane;
		mpio->EarlyBmcLinkDie = 0;
		mpio->EarlyBmcLinkTraining = true;
	}
	log_debug("openSIL: %d MPIO ports\n", count);

	return 0;
}

/* Run a timepoint and carry out any reset which openSIL asks for */
static int run_tp(int tp)
{
	SIL_STATUS ret;

	switch (tp) {
	case 1:
		ret = InitializeAMDSiTp1();
		break;
	case 2:
		ret = InitializeAMDSiTp2();
		break;
	default:
		ret = InitializeAMDSiTp3();
		break;
	}
	log_debug("openSIL: timepoint %d returned %d\n", tp, ret);
	switch (ret) {
	case SilPass:
		return 0;
	case SilResetRequestColdImm:
	case SilResetRequestColdDef:
		log_info("openSIL: cold reset requested\n");
		sysreset_walk_halt(SYSRESET_COLD);
		break;
	case SilResetRequestWarmImm:
	case SilResetRequestWarmDef:
		log_info("openSIL: warm reset requested\n");
		sysreset_walk_halt(SYSRESET_WARM);
		break;
	default:
		break;
	}
	log_err("openSIL: timepoint %d failed (status %d)\n", tp, ret);

	return -EIO;
}

int turin_opensil_init(const void *ucode)
{
	SIL_STATUS status;
	size_t size;
	void *buf;
	int ret;

	status = SilDebugSetup(opensil_debug);
	if (status != SilPass)
		return log_msg_ret("dbg", -EINVAL);
	size = xSimQueryMemoryRequirements();
	buf = memalign(SZ_4K, size);
	if (!buf)
		return log_msg_ret("mem", -ENOMEM);
	memset(buf, '\0', size);
	/* all timepoints run in the same phase, so one assignment does */
	status = xSimAssignMemoryTp1(buf, size);
	if (status != SilPass)
		return log_msg_ret("asn", -EINVAL);
	log_debug("openSIL: %zx bytes at %p\n", size, buf);

	ret = setup_rc_manager();
	if (!ret)
		ret = setup_ccx(ucode);
	if (!ret)
		ret = setup_fch(ofnode_path("/fch"));
	if (!ret)
		ret = setup_nbio();
	if (!ret)
		ret = setup_mpio(ofnode_path("/mpio"));
	if (ret)
		return log_msg_ret("inp", ret);

	return run_tp(1);
}

void turin_opensil_tp2(void)
{
	run_tp(2);
}

void turin_opensil_tp3(void)
{
	run_tp(3);
}
#endif /* OPENSIL_MISSING */
