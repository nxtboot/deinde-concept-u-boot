/* SPDX-License-Identifier: Intel */
/*
 * Copyright (c) 2022, Intel Corporation. All rights reserved.
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * FSP-M Updateable Product Data (UPD) for Alder Lake. The layout must match
 * the FspmUpd.h shipped with the FSP binary in use; it was converted from
 * that header, whose fields carry their offsets, so each one is checked.
 */

#ifndef _ASM_ARCH_FSP_M_UPD_H
#define _ASM_ARCH_FSP_M_UPD_H

#include <asm/fsp2/fsp_api.h>
#include <asm/arch/fsp/fsp_configs.h>
#include <asm/arch/fsp/fsp_t_upd.h>

/* Values for fsp_m_config user_bd: the board type, from FSP's MemInfoHob.h */
enum fsp_user_bd_t {
	USER_BD_MOBILE,
	USER_BD_DESKTOP_2DPC,
	USER_BD_DESKTOP_1DPC,
	USER_BD_SERVER,
	USER_BD_HEDT,
	USER_BD_ULT_MOBILE,
};

/* Values for fsp_m_config sa_gv: system-agent geyserville (freq switching) */
enum fsp_sa_gv_t {
	SA_GV_DISABLED,
	SA_GV_FIXED_POINT0,
	SA_GV_FIXED_POINT1,
	SA_GV_FIXED_POINT2,
	SA_GV_FIXED_POINT3,
	SA_GV_ENABLED,
};

/**
 * struct fspm_arch_upd - architectural settings for FSP-M
 *
 * @revision: Revision of this structure
 * @nvs_buffer_ptr: Non-volatile storage (memory-training) data, or NULL
 * @stack_base: Temporary stack for FspMemoryInit() to use
 * @stack_size: Size of that stack
 * @boot_loader_tolum_size: Memory to reserve below the top of low usable
 *	memory, for the bootloader
 * @boot_mode: One of FSP_BOOT_...
 * @fsp_event_handler: Called by the FSP to report events (status codes and
 *	debug messages), or NULL. See fspm_event_handler()
 */
struct __packed fspm_arch_upd {
	u8 revision;
	u8 reserved[3];
	void *nvs_buffer_ptr;
	void *stack_base;
	u32 stack_size;
	u32 boot_loader_tolum_size;
	u32 boot_mode;
	void *fsp_event_handler;
	u8 reserved1[4];
};

/**
 * struct fsp_m_config - memory-init settings
 *
 * These are the board's memory configuration: the SPD data for each channel,
 * the DQ/DQS mapping and the many tuning parameters which the FSP uses to
 * train the DRAM
 */
struct __packed fsp_m_config {
	/* 0x0040: Platform Reserved Memory Size */
	u64 platform_memory_size;
	/* 0x0048: SPD Data Length */
	u16 memory_spd_data_len;
	/* 0x004a: Enable above 4GB MMIO resource support */
	u8 enable_above4_gb_mmio;
	/* 0x004b: Enable/Disable CrashLog Device 10 */
	u8 cpu_crash_log_device;
	/* 0x004c: Memory SPD Pointer Controller 0 Channel 0 Dimm 0 */
	u32 memory_spd_ptr000;
	/* 0x0050: Memory SPD Pointer Controller 0 Channel 0 Dimm 1 */
	u32 memory_spd_ptr001;
	/* 0x0054: Memory SPD Pointer Controller 0 Channel 1 Dimm 0 */
	u32 memory_spd_ptr010;
	/* 0x0058: Memory SPD Pointer Controller 0 Channel 1 Dimm 1 */
	u32 memory_spd_ptr011;
	/* 0x005c: Memory SPD Pointer Controller 0 Channel 2 Dimm 0 */
	u32 memory_spd_ptr020;
	/* 0x0060: Memory SPD Pointer Controller 0 Channel 2 Dimm 1 */
	u32 memory_spd_ptr021;
	/* 0x0064: Memory SPD Pointer Controller 0 Channel 3 Dimm 0 */
	u32 memory_spd_ptr030;
	/* 0x0068: Memory SPD Pointer Controller 0 Channel 3 Dimm 1 */
	u32 memory_spd_ptr031;
	/* 0x006c: Memory SPD Pointer Controller 1 Channel 0 Dimm 0 */
	u32 memory_spd_ptr100;
	/* 0x0070: Memory SPD Pointer Controller 1 Channel 0 Dimm 1 */
	u32 memory_spd_ptr101;
	/* 0x0074: Memory SPD Pointer Controller 1 Channel 1 Dimm 0 */
	u32 memory_spd_ptr110;
	/* 0x0078: Memory SPD Pointer Controller 1 Channel 1 Dimm 1 */
	u32 memory_spd_ptr111;
	/* 0x007c: Memory SPD Pointer Controller 1 Channel 2 Dimm 0 */
	u32 memory_spd_ptr120;
	/* 0x0080: Memory SPD Pointer Controller 1 Channel 2 Dimm 1 */
	u32 memory_spd_ptr121;
	/* 0x0084: Memory SPD Pointer Controller 1 Channel 3 Dimm 0 */
	u32 memory_spd_ptr130;
	/* 0x0088: Memory SPD Pointer Controller 1 Channel 3 Dimm 1 */
	u32 memory_spd_ptr131;
	/* 0x008c: RcompResistor settings */
	u16 rcomp_resistor;
	/* 0x008e: RcompTarget settings */
	u16 rcomp_target[5];
	/* 0x0098: Dqs Map CPU to DRAM MC 0 CH 0 */
	u8 dqs_map_cpu2_dram_mc0_ch0[2];
	/* 0x009a: Dqs Map CPU to DRAM MC 0 CH 1 */
	u8 dqs_map_cpu2_dram_mc0_ch1[2];
	/* 0x009c: Dqs Map CPU to DRAM MC 0 CH 2 */
	u8 dqs_map_cpu2_dram_mc0_ch2[2];
	/* 0x009e: Dqs Map CPU to DRAM MC 0 CH 3 */
	u8 dqs_map_cpu2_dram_mc0_ch3[2];
	/* 0x00a0: Dqs Map CPU to DRAM MC 1 CH 0 */
	u8 dqs_map_cpu2_dram_mc1_ch0[2];
	/* 0x00a2: Dqs Map CPU to DRAM MC 1 CH 1 */
	u8 dqs_map_cpu2_dram_mc1_ch1[2];
	/* 0x00a4: Dqs Map CPU to DRAM MC 1 CH 2 */
	u8 dqs_map_cpu2_dram_mc1_ch2[2];
	/* 0x00a6: Dqs Map CPU to DRAM MC 1 CH 3 */
	u8 dqs_map_cpu2_dram_mc1_ch3[2];
	/* 0x00a8: Dq Map CPU to DRAM MC 0 CH 0 */
	u8 dq_map_cpu2_dram_mc0_ch0[16];
	/* 0x00b8: Dq Map CPU to DRAM MC 0 CH 1 */
	u8 dq_map_cpu2_dram_mc0_ch1[16];
	/* 0x00c8: Dq Map CPU to DRAM MC 0 CH 2 */
	u8 dq_map_cpu2_dram_mc0_ch2[16];
	/* 0x00d8: Dq Map CPU to DRAM MC 0 CH 3 */
	u8 dq_map_cpu2_dram_mc0_ch3[16];
	/* 0x00e8: Dq Map CPU to DRAM MC 1 CH 0 */
	u8 dq_map_cpu2_dram_mc1_ch0[16];
	/* 0x00f8: Dq Map CPU to DRAM MC 1 CH 1 */
	u8 dq_map_cpu2_dram_mc1_ch1[16];
	/* 0x0108: Dq Map CPU to DRAM MC 1 CH 2 */
	u8 dq_map_cpu2_dram_mc1_ch2[16];
	/* 0x0118: Dq Map CPU to DRAM MC 1 CH 3 */
	u8 dq_map_cpu2_dram_mc1_ch3[16];
	/* 0x0128: Dqs Pins Interleaved Setting */
	u8 dq_pins_interleaved;
	/* 0x0129: Smram Mask */
	u8 smram_mask;
	/* 0x012a: Ibecc */
	u8 ibecc;
	/* 0x012b: IbeccOperationMode */
	u8 ibecc_operation_mode;
	/* 0x012c: IbeccProtectedRangeEnable */
	u8 ibecc_protected_range_enable[8];
	/* 0x0134: IbeccProtectedRangeBase */
	u32 ibecc_protected_range_base[8];
	/* 0x0154: IbeccProtectedRangeMask */
	u32 ibecc_protected_range_mask[8];
	/* 0x0174: MRC Fast Boot */
	u8 mrc_fast_boot;
	/* 0x0175: Rank Margin Tool per Task */
	u8 rmt_per_task;
	/* 0x0176: Training Trace */
	u8 train_trace;
	u8 rsvd00;
	/* 0x0178: Tseg Size */
	u32 tseg_size;
	/* 0x017c: MMIO Size */
	u16 mmio_size;
	/* 0x017e: Probeless Trace */
	u8 probeless_trace;
	/* 0x017f: Enable SMBus */
	u8 smbus_enable;
	/* 0x0180: Spd Address Tabl */
	u8 spd_address_table[16];
	/* 0x0190: Platform Debug Consent */
	u8 platform_debug_consent;
	/* 0x0191: DCI Enable */
	u8 dci_en;
	/* 0x0192: DCI DbC Mode */
	u8 dci_dbc_mode;
	/* 0x0193: Enable DCI ModPHY Power Gate */
	u8 dci_modphy_pg;
	/* 0x0194: USB3 Type-C UFP2DFP Kernel/Platform Debug Support */
	u8 dci_usb3_typec_ufp_dbg;
	/* 0x0195: PCH Trace Hub Mode */
	u8 pch_trace_hub_mode;
	/* 0x0196: PCH Trace Hub Memory Region 0 buffer Size */
	u8 pch_trace_hub_mem_reg0_size;
	/* 0x0197: PCH Trace Hub Memory Region 1 buffer Size */
	u8 pch_trace_hub_mem_reg1_size;
	/* 0x0198: HD Audio DMIC Link Clock Select */
	u8 pch_hda_audio_link_dmic_clock_select[2];
	/* 0x019a: Disable Tccold Handshake */
	u8 disable_dynamic_tccold_handshake;
	/* 0x019b: PchPreMemRsvd */
	u8 pch_pre_mem_rsvd[4];
	/* 0x019f: State of X2APIC_OPT_OUT bit in the DMAR table */
	u8 x2_apic_opt_out;
	/* 0x01a0: State of DMA_CONTROL_GUARANTEE bit in the DMAR table */
	u8 dma_control_guarantee;
	u8 rsvd01[3];
	/* 0x01a4: Base addresses for VT-d function MMIO access */
	u32 vtd_base_address[9];
	/* 0x01c8: Disable VT-d */
	u8 vtd_disable;
	/* 0x01c9: Vtd Programming for Igd */
	u8 vtd_igd_enable;
	/* 0x01ca: Vtd Programming for Ipu */
	u8 vtd_ipu_enable;
	/* 0x01cb: Vtd Programming for Iop */
	u8 vtd_iop_enable;
	/* 0x01cc: Vtd Programming for ITbt */
	u8 vtd_itbt_enable;
	/* 0x01cd: Internal Graphics Pre-allocated Memory */
	u8 igd_dvmt50_pre_alloc;
	/* 0x01ce: Internal Graphics */
	u8 internal_gfx;
	/* 0x01cf: Aperture Size */
	u8 aperture_size;
	/* 0x01d0: Board Type */
	u8 user_bd;
	/* 0x01d1: MRC Retraining on RTC Power Loss */
	u8 disable_mrc_retraining_on_rtc_power_loss;
	/* 0x01d2: DDR Frequency Limit */
	u16 ddr_freq_limit;
	/* 0x01d4: SA GV */
	u8 sa_gv;
	/* 0x01d5: Memory Test on Warm Boot */
	u8 mem_test_on_warm_boot;
	/* 0x01d6: DDR Speed Control */
	u8 ddr_speed_control;
	/* 0x01d7: Rank Margin Tool */
	u8 rmt;
	/* 0x01d8: Controller 0 Channel 0 DIMM Control */
	u8 disable_mc0_ch0;
	/* 0x01d9: Controller 0 Channel 1 DIMM Control */
	u8 disable_mc0_ch1;
	/* 0x01da: Controller 0 Channel 2 DIMM Control */
	u8 disable_mc0_ch2;
	/* 0x01db: Controller 0 Channel 3 DIMM Control */
	u8 disable_mc0_ch3;
	/* 0x01dc: Controller 1 Channel 0 DIMM Control */
	u8 disable_mc1_ch0;
	/* 0x01dd: Controller 1 Channel 1 DIMM Control */
	u8 disable_mc1_ch1;
	/* 0x01de: Controller 1 Channel 2 DIMM Control */
	u8 disable_mc1_ch2;
	/* 0x01df: Controller 1 Channel 3 DIMM Control */
	u8 disable_mc1_ch3;
	/* 0x01e0: Scrambler Support */
	u8 scrambler_support;
	/* 0x01e1: SPD Profile Selected */
	u8 spd_profile_selected;
	/* 0x01e2: Memory Reference Clock */
	u8 ref_clk;
	u8 rsvd02;
	/* 0x01e4: Memory Voltage */
	u16 vdd_voltage;
	/* 0x01e6: Memory Ratio */
	u8 ratio;
	/* 0x01e7: tCL */
	u8 t_cl;
	/* 0x01e8: tCWL */
	u8 t_cwl;
	u8 rsvd03;
	/* 0x01ea: tFAW */
	u16 t_faw;
	/* 0x01ec: tRAS */
	u16 t_ras;
	/* 0x01ee: tRCD/tRP */
	u8 t_rc_dt_rp;
	u8 rsvd04;
	/* 0x01f0: tREFI */
	u16 t_refi;
	/* 0x01f2: tRFC */
	u16 t_rfc;
	/* 0x01f4: tRRD */
	u8 t_rrd;
	/* 0x01f5: tRTP */
	u8 t_rtp;
	/* 0x01f6: tWR */
	u8 t_wr;
	/* 0x01f7: tWTR */
	u8 t_wtr;
	/* 0x01f8: NMode */
	u8 n_mode_support;
	/* 0x01f9: Enable Intel HD Audio (Azalia) */
	u8 pch_hda_enable;
	/* 0x01fa: Enable PCH ISH Controller */
	u8 pch_ish_enable;
	/* 0x01fb: CPU Trace Hub Mode */
	u8 cpu_trace_hub_mode;
	/* 0x01fc: CPU Trace Hub Memory Region 0 */
	u8 cpu_trace_hub_mem_reg0_size;
	/* 0x01fd: CPU Trace Hub Memory Region 1 */
	u8 cpu_trace_hub_mem_reg1_size;
	/* 0x01fe: SAGV Gear Ratio */
	u8 sa_gv_gear[4];
	/* 0x0202: SAGV Frequency */
	u16 sa_gv_freq[4];
	/* 0x020a: SAGV Disabled Gear Ratio */
	u8 gear_ratio;
	/* 0x020b: HECI Timeouts */
	u8 heci_timeouts;
	/* 0x020c: HECI1 BAR address */
	u32 heci1_bar_address;
	/* 0x0210: HECI2 BAR address */
	u32 heci2_bar_address;
	/* 0x0214: HECI3 BAR address */
	u32 heci3_bar_address;
	/* 0x0218: HG dGPU Power Delay */
	u16 hg_delay_after_pwr_en;
	/* 0x021a: HG dGPU Reset Delay */
	u16 hg_delay_after_hold_reset;
	/* 0x021c: MMIO size adjustment for AUTO mode */
	u16 mmio_size_adjustment;
	/* 0x021e: PCIe ASPM programming will happen in relation to the Oprom */
	u8 init_pcie_aspm_after_oprom;
	/* 0x021f: Selection of the primary display device */
	u8 primary_display;
	/* 0x0220: Selection of PSMI Region size */
	u8 psmi_region_size;
	/* 0x0221: Enable Program PSF0 Grant Count Reload value */
	u8 grant_count;
	u8 rsvd05[2];
	/* 0x0224: Temporary MMIO address for GMADR */
	u32 gm_adr;
	/* 0x0228: Temporary MMIO address for GTTMMADR */
	u32 gtt_mm_adr;
	/* 0x022c: Selection of iGFX GTT Memory size */
	u16 gtt_size;
	/* 0x022e: Hybrid Graphics GPIO information for PEG 0 */
	u8 cpu_pcie0_rtd3_gpio[24];
	/* 0x0246: Enable/Disable MRC TXT dependency */
	u8 txt_implemented;
	/* 0x0247: Enable/Disable SA OcSupport */
	u8 sa_oc_support;
	/* 0x0248: GT slice Voltage Mode */
	u8 gt_voltage_mode;
	/* 0x0249: Maximum GTs turbo ratio override */
	u8 gt_max_oc_ratio;
	/* 0x024a: The voltage offset applied to GT slice */
	u16 gt_voltage_offset;
	/*
	 * 0x024c: The GT slice voltage override which is applied to the entire
	 * range of GT frequencies
	 */
	u16 gt_voltage_override;
	/* 0x024e: adaptive voltage applied during turbo frequencies */
	u16 gt_extra_turbo_voltage;
	/* 0x0250: voltage offset applied to the SA */
	u16 sa_voltage_offset;
	/* 0x0252: PCIe root port Function number for Hybrid Graphics dGPU */
	u8 root_port_index;
	/* 0x0253: Realtime Memory Timing */
	u8 realtime_memory_timing;
	/* 0x0254: iTBT PCIe Multiple Segment setting */
	u8 pcie_multiple_segment_enabled;
	/* 0x0255: Enable/Disable SA IPU */
	u8 sa_ipu_enable;
	/* 0x0256: Lane Used of CSI port */
	u8 ipu_lane_used[8];
	/* 0x025e: Lane Used of CSI port */
	u8 csi_speed[8];
	/* 0x0266: IMGU CLKOUT Configuration */
	u8 imgu_clk_out_en[6];
	/* 0x026c: Enable PCIE RP Mask */
	u32 cpu_pcie_rp_enable_mask;
	/* 0x0270: Assertion on Link Down GPIOs */
	u8 cpu_pcie_rp_link_down_gpios;
	/* 0x0271: Enable ClockReq Messaging */
	u8 cpu_pcie_rp_clock_req_msg_enable[3];
	/* 0x0274: PCIE RP Pcie Speed */
	u8 cpu_pcie_rp_pcie_speed[4];
	/* 0x0278: Selection of PSMI Support On/Off */
	u8 gt_psmi_support;
	/* 0x0279: Program GPIOs for LFP on DDI port-A device */
	u8 ddi_port_a_config;
	/* 0x027a: Program GPIOs for LFP on DDI port-B device */
	u8 ddi_port_b_config;
	/* 0x027b: Enable or disable HPD of DDI port A */
	u8 ddi_port_a_hpd;
	/* 0x027c: Enable or disable HPD of DDI port B */
	u8 ddi_port_b_hpd;
	/* 0x027d: Enable or disable HPD of DDI port C */
	u8 ddi_port_c_hpd;
	/* 0x027e: Enable or disable HPD of DDI port 1 */
	u8 ddi_port1_hpd;
	/* 0x027f: Enable or disable HPD of DDI port 2 */
	u8 ddi_port2_hpd;
	/* 0x0280: Enable or disable HPD of DDI port 3 */
	u8 ddi_port3_hpd;
	/* 0x0281: Enable or disable HPD of DDI port 4 */
	u8 ddi_port4_hpd;
	/* 0x0282: Enable or disable DDC of DDI port A */
	u8 ddi_port_a_ddc;
	/* 0x0283: Enable or disable DDC of DDI port B */
	u8 ddi_port_b_ddc;
	/* 0x0284: Enable or disable DDC of DDI port C */
	u8 ddi_port_c_ddc;
	/* 0x0285: Enable DDC setting of DDI Port 1 */
	u8 ddi_port1_ddc;
	/* 0x0286: Enable DDC setting of DDI Port 2 */
	u8 ddi_port2_ddc;
	/* 0x0287: Enable DDC setting of DDI Port 3 */
	u8 ddi_port3_ddc;
	/* 0x0288: Enable DDC setting of DDI Port 4 */
	u8 ddi_port4_ddc;
	u8 rsvd06[7];
	/* 0x0290: Temporary MMIO address for GMADR */
	u64 gm_adr64;
	/* 0x0298: Per-core HT Disable */
	u16 per_core_ht_disable;
	/* 0x029a: SA/Uncore voltage mode */
	u8 sa_voltage_mode;
	u8 rsvd07;
	/* 0x029c: SA/Uncore Voltage Override */
	u16 sa_voltage_override;
	/* 0x029e: SA/Uncore Extra Turbo voltage */
	u16 sa_extra_turbo_voltage;
	/* 0x02a0: Thermal Velocity Boost Ratio clipping */
	u8 tvb_ratio_clipping;
	/* 0x02a1: Thermal Velocity Boost voltage optimization */
	u8 tvb_voltage_optimization;
	/* 0x02a2: Enable/Disable Display Audio Link in Pre-OS */
	u8 display_audio_link;
	u8 rsvd08;
	/* 0x02a4: Memory VDDQ Voltage */
	u16 vddq_voltage;
	/* 0x02a6: Memory VPP Voltage */
	u16 vpp_voltage;
	/* 0x02a8: CPU PCIe New FOM */
	u8 cpu_pcie_new_fom[4];
	/* 0x02ac: DMI DEKEL New FOM */
	u8 dmi_new_fom;
	/* 0x02ad: Dynamic Memory Boost */
	u8 dynamic_memory_boost;
	/* 0x02ae: Hybrid Graphics Support */
	u8 hg_support;
	/* 0x02af: Realtime Memory Frequency */
	u8 realtime_memory_frequency;
	/* 0x02b0: OC Safe Mode */
	u8 oc_safe_mode;
	/* 0x02b1: SaPreMemProductionRsvd */
	u8 sa_pre_mem_production_rsvd[96];
	/* 0x0311: Enable Gt CLOS */
	u8 gt_clos_enable;
	/* 0x0312: DMI Max Link Speed */
	u8 dmi_max_link_speed;
	/* 0x0313: DMI Equalization Phase 2 */
	u8 dmi_gen3_eq_ph2_enable;
	/* 0x0314: DMI Gen3 Equalization Phase3 */
	u8 dmi_gen3_eq_ph3_method;
	/* 0x0315: Enable/Disable DMI GEN3 Static EQ Phase1 programming */
	u8 dmi_gen3_program_static_eq;
	/* 0x0316: DeEmphasis control for DMI */
	u8 dmi_de_emphasis;
	/* 0x0317: DMI Gen3 Root port preset values per lane */
	u8 dmi_gen3_root_port_preset[8];
	/* 0x031f: DMI Gen3 End port preset values per lane */
	u8 dmi_gen3_end_point_preset[8];
	/* 0x0327: DMI Gen3 End port Hint values per lane */
	u8 dmi_gen3_end_point_hint[8];
	/* 0x032f: DMI Gen3 RxCTLEp per-Bundle control */
	u8 dmi_gen3_rx_ctle_peaking[4];
	/* 0x0333: DMI ASPM Configuration:{Combo */
	u8 dmi_aspm;
	/* 0x0334: Enable/Disable DMI GEN3 Hardware Eq */
	u8 dmi_hweq;
	/* 0x0335: Enable/Disable CPU DMI GEN3 Phase 23 Bypass */
	u8 gen3_eq_phase23_bypass;
	/* 0x0336: Enable/Disable CPU DMI GEN3 Phase 3 Bypass */
	u8 gen3_eq_phase3_bypass;
	/*
	 * 0x0337: Enable/Disable CPU DMI Gen3 EQ Local Transmitter Coefficient
	 * Override Enable
	 */
	u8 gen3_ltco_enable;
	/*
	 * 0x0338: Enable/Disable CPU DMI Gen3 EQ Remote Transmitter
	 * Coefficient/Preset Override Enable
	 */
	u8 gen3_rtco_rtpo_enable;
	/* 0x0339: DMI Gen3 Transmitter Pre-Cursor Coefficient */
	u8 dmi_gen3_ltcpre[8];
	/* 0x0341: DMI Gen3 Transmitter Post-Cursor Coefficient */
	u8 dmi_gen3_ltcpo[8];
	/* 0x0349: PCIE Hw Eq Gen3 CoeffList Cm */
	u8 cpu_dmi_hw_eq_gen3_coeff_list_cm[8];
	/* 0x0351: PCIE Hw Eq Gen3 CoeffList Cp */
	u8 cpu_dmi_hw_eq_gen3_coeff_list_cp[8];
	/* 0x0359: Enable/Disable DMI GEN3 DmiGen3DsPresetEnable */
	u8 dmi_gen3_ds_preset_enable;
	/* 0x035a: DMI Gen3 Root port preset Rx values per lane */
	u8 dmi_gen3_ds_port_rx_preset[8];
	/* 0x0362: DMI Gen3 Root port preset Tx values per lane */
	u8 dmi_gen3_ds_port_tx_preset[8];
	/* 0x036a: Enable/Disable DMI GEN3 DmiGen3UsPresetEnable */
	u8 dmi_gen3_us_preset_enable;
	/* 0x036b: DMI Gen3 Root port preset Rx values per lane */
	u8 dmi_gen3_us_port_rx_preset[8];
	/* 0x0373: DMI Gen3 Root port preset Tx values per lane */
	u8 dmi_gen3_us_port_tx_preset[8];
	/* 0x037b: DMI Hw Eq Gen4 CoeffList Cm */
	u8 cpu_dmi_hw_eq_gen4_coeff_list_cm[8];
	/* 0x0383: DMI Hw Eq Gen4 CoeffList Cp */
	u8 cpu_dmi_hw_eq_gen4_coeff_list_cp[8];
	/* 0x038b: Enable/Disable CPU DMI GEN4 Phase 23 Bypass */
	u8 gen4_eq_phase23_bypass;
	/* 0x038c: Enable/Disable CPU DMI GEN4 Phase 3 Bypass */
	u8 gen4_eq_phase3_bypass;
	/* 0x038d: Enable/Disable DMI GEN4 DmiGen4DsPresetEnable */
	u8 dmi_gen4_ds_preset_enable;
	/* 0x038e: DMI Gen4 Root port preset Tx values per lane */
	u8 dmi_gen4_ds_port_tx_preset[8];
	/*
	 * 0x0396: Enable/Disable CPU DMI Gen4 EQ Remote Transmitter
	 * Coefficient/Preset Override Enable
	 */
	u8 gen4_rtco_rtpo_enable;
	/*
	 * 0x0397: Enable/Disable CPU DMI Gen4 EQ Local Transmitter Coefficient
	 * Override Enable
	 */
	u8 gen4_ltco_enable;
	/* 0x0398: DMI Gen4 Transmitter Pre-Cursor Coefficient */
	u8 dmi_gen4_ltcpre[8];
	/* 0x03a0: DMI Gen4 Transmitter Post-Cursor Coefficient */
	u8 dmi_gen4_ltcpo[8];
	/* 0x03a8: Enable/Disable DMI GEN4 DmiGen4UsPresetEnable */
	u8 dmi_gen4_us_preset_enable;
	/* 0x03a9: DMI Gen4 Root port preset Tx values per lane */
	u8 dmi_gen4_us_port_tx_preset[8];
	/* 0x03b1: DMI ASPM Control Configuration:{Combo */
	u8 dmi_aspm_ctrl;
	/* 0x03b2: DMI ASPM L1 exit Latency */
	u8 dmi_aspm_l1_exit_latency;
	/* 0x03b3: BIST on Reset */
	u8 bist_on_reset;
	/* 0x03b4: Skip Stop PBET Timer Enable/Disable */
	u8 skip_stop_pbet;
	/* 0x03b5: C6DRAM power gating feature */
	u8 enable_c6_dram;
	/* 0x03b6: Over clocking support */
	u8 oc_support;
	/* 0x03b7: Over clocking Lock */
	u8 oc_lock;
	/* 0x03b8: Maximum Core Turbo Ratio Override */
	u8 core_max_oc_ratio;
	/* 0x03b9: Core voltage mode */
	u8 core_voltage_mode;
	/* 0x03ba: Maximum clr turbo ratio override */
	u8 ring_max_oc_ratio;
	/* 0x03bb: Hyper Threading Enable/Disable */
	u8 hyper_threading;
	/* 0x03bc: Enable or Disable CPU Ratio Override */
	u8 cpu_ratio_override;
	/* 0x03bd: CPU ratio value */
	u8 cpu_ratio;
	/* 0x03be: Boot frequency */
	u8 boot_frequency;
	/* 0x03bf: Number of active big cores */
	u8 active_core_count;
	/* 0x03c0: Processor Early Power On Configuration FCLK setting */
	u8 f_clk_frequency;
	/* 0x03c1: Set JTAG power in C10 and deeper power states */
	u8 jtag_c10_power_gate_disable;
	/* 0x03c2: Enable or Disable VMX */
	u8 vmx_enable;
	/* 0x03c3: AVX2 Ratio Offset */
	u8 avx2_ratio_offset;
	/* 0x03c4: AVX3 Ratio Offset */
	u8 avx3_ratio_offset;
	/* 0x03c5: BCLK Adaptive Voltage Enable */
	u8 bclk_adaptive_voltage;
	/* 0x03c6: core voltage override */
	u16 core_voltage_override;
	/* 0x03c8: Core Turbo voltage Adaptive */
	u16 core_voltage_adaptive;
	/* 0x03ca: Core Turbo voltage Offset */
	u16 core_voltage_offset;
	/* 0x03cc: Core PLL voltage offset */
	u8 core_pll_voltage_offset;
	/* 0x03cd: Atom Core PLL voltage offset */
	u8 atom_pll_voltage_offset;
	/* 0x03ce: Ring Downbin */
	u8 ring_down_bin;
	/* 0x03cf: Ring voltage mode */
	u8 ring_voltage_mode;
	/* 0x03d0: TjMax Offset */
	u8 tj_max_offset;
	/* 0x03d1: FastThrottleThreshold */
	u8 fast_throttle_threshold;
	/* 0x03d2: Ring voltage override */
	u16 ring_voltage_override;
	/* 0x03d4: Ring Turbo voltage Adaptive */
	u16 ring_voltage_adaptive;
	/* 0x03d6: Ring Turbo voltage Offset */
	u16 ring_voltage_offset;
	/* 0x03d8: Enable or Disable TME */
	u8 tme_enable;
	/* 0x03d9: Enable CPU CrashLog */
	u8 cpu_crash_log_enable;
	/* 0x03da: CPU Run Control */
	u8 debug_interface_enable;
	/* 0x03db: CPU Run Control Lock */
	u8 debug_interface_lock_enable;
	/* 0x03dc: Atom L2 voltage mode */
	u8 atom_l2_voltage_mode;
	u8 rsvd10;
	/* 0x03de: Atom L2 Voltage Override */
	u16 atom_l2_voltage_override;
	/* 0x03e0: Atom L2 Turbo voltage Adaptive */
	u16 atom_l2_voltage_adaptive;
	/* 0x03e2: Atom L2 Turbo voltage Offset */
	u16 atom_l2_voltage_offset;
	/* 0x03e4: Per-Atom-Cluster VF Offset */
	u16 per_atom_cluster_voltage_offset[4];
	/* 0x03ec: Per-Atom-Cluster VF Offset Prefix */
	u8 per_atom_cluster_voltage_offset_prefix[4];
	/* 0x03f0: Enable IA CEP */
	u8 ia_cep_enable;
	/* 0x03f1: Enable GT CEP */
	u8 gt_cep_enable;
	/* 0x03f2: Enable CPU DLVR bypass mode support */
	u8 dlvr_bypass_mode_enable;
	/* 0x03f3: Number of active small cores */
	u8 active_small_core_count;
	/* 0x03f4: Core VF Point Offset Mode */
	u8 core_vf_point_offset_mode;
	u8 rsvd11[1];
	/* 0x03f6: Core VF Point Offset */
	u16 core_vf_point_offset[15];
	/* 0x0414: Core VF Point Offset Prefix */
	u8 core_vf_point_offset_prefix[15];
	/* 0x0423: Core VF Point Ratio */
	u8 core_vf_point_ratio[15];
	/* 0x0432: Core VF Point Count */
	u8 core_vf_point_count;
	/* 0x0433: Core VF Configuration Scope */
	u8 core_vf_config_scope;
	/* 0x0434: Per-core VF Offset */
	u16 per_core_voltage_offset[8];
	/* 0x0444: Per-core VF Offset Prefix */
	u8 per_core_voltage_offset_prefix[8];
	/* 0x044c: Per Core Max Ratio override */
	u8 per_core_ratio_override;
	/* 0x044d: Per Core Current Max Ratio */
	u8 per_core_ratio[8];
	/* 0x0455: Atom Cluster Max Ratio */
	u8 atom_cluster_ratio[4];
	/* 0x0459: Core Ratio Extension Mode */
	u8 core_ratio_extension_mode;
	/* 0x045a: Pvd Ratio Threshold */
	u8 pvd_ratio_threshold;
	/* 0x045b: Support Unlimited ICCMAX */
	u8 unlimited_icc_max;
	/* 0x045c: Enable CPU CrashLog GPRs dump */
	u8 crash_log_gprs;
	/* 0x045d: Ring VF Point Offset Mode */
	u8 ring_vf_point_offset_mode;
	/* 0x045e: Ring VF Point Offset */
	u16 ring_vf_point_offset[15];
	/* 0x047c: Ring VF Point Offset Prefix */
	u8 ring_vf_point_offset_prefix[15];
	/* 0x048b: Ring VF Point Ratio */
	u8 ring_vf_point_ratio[15];
	/* 0x049a: Ring VF Point Count */
	u8 ring_vf_point_count;
	/* 0x049b: BCLK Frequency Source */
	u8 bclk_source;
	/* 0x049c: GPIO Override */
	u8 gpio_override;
	u8 rsvd12[3];
	/* 0x04a0: CPU BCLK OC Frequency */
	u32 cpu_bclk_oc_frequency;
	/* 0x04a4: Bitmask of disable cores */
	u32 disable_per_core_mask;
	/* 0x04a8: Bitmask of disable atoms */
	u32 disable_per_atom_mask;
	/* 0x04ac: Sa PLL Frequency */
	u8 sa_pll_freq_override;
	/* 0x04ad: Skip override boot mode When Fw Update. */
	u8 si_skip_override_boot_mode_when_fw_update;
	/* 0x04ae: TSC HW Fixup disable */
	u8 tsc_disable_hw_fixup;
	/* 0x04af: Support IA Unlimited ICCMAX */
	u8 ia_icc_unlimited_mode;
	/* 0x04b0: IA ICCMAX */
	u16 ia_icc_max;
	/* 0x04b2: Support GT Unlimited ICCMAX */
	u8 gt_icc_unlimited_mode;
	u8 rsvd13;
	/* 0x04b4: GT ICCMAX */
	u16 gt_icc_max;
	/* 0x04b6: TVB Down Bins for Temp Threshold 0 */
	u8 tvb_down_bins_temp_threshold0;
	/* 0x04b7: TVB Temperature Threshold 0 */
	u8 tvb_temp_threshold0;
	/* 0x04b8: TVB Temperature Threshold 1 */
	u8 tvb_temp_threshold1;
	/* 0x04b9: TVB Down Bins for Temp Threshold 1 */
	u8 tvb_down_bins_temp_threshold1;
	/* 0x04ba: FLL Overclock Mode Enable */
	u8 fll_oc_mode_en;
	/* 0x04bb: FLL Overclock Mode */
	u8 fll_overclock_mode;
	/* 0x04bc: Configuration for boot TDP selection */
	u8 config_tdp_level;
	u8 rsvd14[3];
	/* 0x04c0: Short term Power Limit value for custom cTDP level 1 */
	u32 custom_power_limit1;
	/* 0x04c4: Enhanced Thermal Turbo Mode */
	u8 etvb;
	/* 0x04c5: UnderVolt Protection */
	u8 under_volt_protection;
	/* 0x04c6: ReservedCpuPreMem */
	u8 reserved_cpu_pre_mem[6];
	/* 0x04cc: BiosGuard */
	u8 bios_guard;
	u8 bios_guard_tools_interface;
	/* 0x04ce: Txt */
	u8 txt;
	u8 rsvd15;
	/* 0x04d0: PrmrrSize */
	u32 prmrr_size;
	/* 0x04d4: SinitMemorySize */
	u32 sinit_memory_size;
	/* 0x04d8: TxtDprMemoryBase */
	u64 txt_dpr_memory_base;
	/* 0x04e0: TxtHeapMemorySize */
	u32 txt_heap_memory_size;
	/* 0x04e4: TxtDprMemorySize */
	u32 txt_dpr_memory_size;
	/* 0x04e8: BiosAcmBase */
	u32 bios_acm_base;
	/* 0x04ec: BiosAcmSize */
	u32 bios_acm_size;
	/* 0x04f0: ApStartupBase */
	u32 ap_startup_base;
	/* 0x04f4: TgaSize */
	u32 tga_size;
	/* 0x04f8: TxtLcpPdBase */
	u64 txt_lcp_pd_base;
	/* 0x0500: TxtLcpPdSize */
	u64 txt_lcp_pd_size;
	/* 0x0508: IsTPMPresence */
	u8 is_tpm_presence;
	/* 0x0509: ReservedSecurityPreMem */
	u8 reserved_security_pre_mem[32];
	/* 0x0529: Enable PCH HSIO PCIE Rx Set Ctle */
	u8 pch_pcie_hsio_rx_set_ctle_enable[28];
	/* 0x0545: PCH HSIO PCIE Rx Set Ctle Value */
	u8 pch_pcie_hsio_rx_set_ctle[28];
	/*
	 * 0x0561: Enable PCH HSIO PCIE TX Gen 1 Downscale Amplitude Adjustment
	 * value override
	 */
	u8 pch_pcie_hsio_tx_gen1_downscale_amp_enable[28];
	/*
	 * 0x057d: PCH HSIO PCIE Gen 2 TX Output Downscale Amplitude Adjustment
	 * value
	 */
	u8 pch_pcie_hsio_tx_gen1_downscale_amp[28];
	/*
	 * 0x0599: Enable PCH HSIO PCIE TX Gen 2 Downscale Amplitude Adjustment
	 * value override
	 */
	u8 pch_pcie_hsio_tx_gen2_downscale_amp_enable[28];
	/*
	 * 0x05b5: PCH HSIO PCIE Gen 2 TX Output Downscale Amplitude Adjustment
	 * value
	 */
	u8 pch_pcie_hsio_tx_gen2_downscale_amp[28];
	/*
	 * 0x05d1: Enable PCH HSIO PCIE TX Gen 3 Downscale Amplitude Adjustment
	 * value override
	 */
	u8 pch_pcie_hsio_tx_gen3_downscale_amp_enable[28];
	/*
	 * 0x05ed: PCH HSIO PCIE Gen 3 TX Output Downscale Amplitude Adjustment
	 * value
	 */
	u8 pch_pcie_hsio_tx_gen3_downscale_amp[28];
	/*
	 * 0x0609: Enable PCH HSIO PCIE Gen 1 TX Output De-Emphasis Adjustment
	 * Setting value override
	 */
	u8 pch_pcie_hsio_tx_gen1_de_emph_enable[28];
	/* 0x0625: PCH HSIO PCIE Gen 1 TX Output De-Emphasis Adjustment value */
	u8 pch_pcie_hsio_tx_gen1_de_emph[28];
	/*
	 * 0x0641: Enable PCH HSIO PCIE Gen 2 TX Output -3.5dB De-Emphasis
	 * Adjustment Setting value override
	 */
	u8 pch_pcie_hsio_tx_gen2_de_emph3p5_enable[28];
	/*
	 * 0x065d: PCH HSIO PCIE Gen 2 TX Output -3.5dB De-Emphasis Adjustment
	 * value
	 */
	u8 pch_pcie_hsio_tx_gen2_de_emph3p5[28];
	/*
	 * 0x0679: Enable PCH HSIO PCIE Gen 2 TX Output -6.0dB De-Emphasis
	 * Adjustment Setting value override
	 */
	u8 pch_pcie_hsio_tx_gen2_de_emph6p0_enable[28];
	/*
	 * 0x0695: PCH HSIO PCIE Gen 2 TX Output -6.0dB De-Emphasis Adjustment
	 * value
	 */
	u8 pch_pcie_hsio_tx_gen2_de_emph6p0[28];
	/*
	 * 0x06b1: Enable PCH HSIO SATA Receiver Equalization Boost Magnitude
	 * Adjustment Value override
	 */
	u8 pch_sata_hsio_rx_gen1_eq_boost_mag_enable[8];
	/*
	 * 0x06b9: PCH HSIO SATA 1.5 Gb/s Receiver Equalization Boost Magnitude
	 * Adjustment value
	 */
	u8 pch_sata_hsio_rx_gen1_eq_boost_mag[8];
	/*
	 * 0x06c1: Enable PCH HSIO SATA Receiver Equalization Boost Magnitude
	 * Adjustment Value override
	 */
	u8 pch_sata_hsio_rx_gen2_eq_boost_mag_enable[8];
	/*
	 * 0x06c9: PCH HSIO SATA 3.0 Gb/s Receiver Equalization Boost Magnitude
	 * Adjustment value
	 */
	u8 pch_sata_hsio_rx_gen2_eq_boost_mag[8];
	/*
	 * 0x06d1: Enable PCH HSIO SATA Receiver Equalization Boost Magnitude
	 * Adjustment Value override
	 */
	u8 pch_sata_hsio_rx_gen3_eq_boost_mag_enable[8];
	/*
	 * 0x06d9: PCH HSIO SATA 6.0 Gb/s Receiver Equalization Boost Magnitude
	 * Adjustment value
	 */
	u8 pch_sata_hsio_rx_gen3_eq_boost_mag[8];
	/*
	 * 0x06e1: Enable PCH HSIO SATA 1.5 Gb/s TX Output Downscale Amplitude
	 * Adjustment value override
	 */
	u8 pch_sata_hsio_tx_gen1_downscale_amp_enable[8];
	/*
	 * 0x06e9: PCH HSIO SATA 1.5 Gb/s TX Output Downscale Amplitude
	 * Adjustment value
	 */
	u8 pch_sata_hsio_tx_gen1_downscale_amp[8];
	/*
	 * 0x06f1: Enable PCH HSIO SATA 3.0 Gb/s TX Output Downscale Amplitude
	 * Adjustment value override
	 */
	u8 pch_sata_hsio_tx_gen2_downscale_amp_enable[8];
	/*
	 * 0x06f9: PCH HSIO SATA 3.0 Gb/s TX Output Downscale Amplitude
	 * Adjustment value
	 */
	u8 pch_sata_hsio_tx_gen2_downscale_amp[8];
	/*
	 * 0x0701: Enable PCH HSIO SATA 6.0 Gb/s TX Output Downscale Amplitude
	 * Adjustment value override
	 */
	u8 pch_sata_hsio_tx_gen3_downscale_amp_enable[8];
	/*
	 * 0x0709: PCH HSIO SATA 6.0 Gb/s TX Output Downscale Amplitude
	 * Adjustment value
	 */
	u8 pch_sata_hsio_tx_gen3_downscale_amp[8];
	/*
	 * 0x0711: Enable PCH HSIO SATA 1.5 Gb/s TX Output De-Emphasis
	 * Adjustment Setting value override
	 */
	u8 pch_sata_hsio_tx_gen1_de_emph_enable[8];
	/*
	 * 0x0719: PCH HSIO SATA 1.5 Gb/s TX Output De-Emphasis Adjustment
	 * Setting
	 */
	u8 pch_sata_hsio_tx_gen1_de_emph[8];
	/*
	 * 0x0721: Enable PCH HSIO SATA 3.0 Gb/s TX Output De-Emphasis
	 * Adjustment Setting value override
	 */
	u8 pch_sata_hsio_tx_gen2_de_emph_enable[8];
	/*
	 * 0x0729: PCH HSIO SATA 3.0 Gb/s TX Output De-Emphasis Adjustment
	 * Setting
	 */
	u8 pch_sata_hsio_tx_gen2_de_emph[8];
	/*
	 * 0x0731: Enable PCH HSIO SATA 6.0 Gb/s TX Output De-Emphasis
	 * Adjustment Setting value override
	 */
	u8 pch_sata_hsio_tx_gen3_de_emph_enable[8];
	/*
	 * 0x0739: PCH HSIO SATA 6.0 Gb/s TX Output De-Emphasis Adjustment
	 * Setting
	 */
	u8 pch_sata_hsio_tx_gen3_de_emph[8];
	/* 0x0741: PCH LPC Enhanced Port 80 Decoding */
	u8 pch_lpc_enhance_port8xh_decoding;
	/* 0x0742: PCH Port80 Route */
	u8 pch_port80_route;
	/* 0x0743: Enable SMBus ARP support */
	u8 smbus_arp_enable;
	/* 0x0744: Number of RsvdSmbusAddressTable. */
	u8 pch_num_rsvd_smbus_addresses;
	u8 rsvd16;
	/* 0x0746: SMBUS Base Address */
	u16 pch_smbus_io_base;
	/* 0x0748: Enable SMBus Alert Pin */
	u8 pch_smb_alert_enable;
	/* 0x0749: Usage type for ClkSrc */
	u8 pcie_clk_src_usage[18];
	u8 pcie_clk_src_usage_rsvd[14];
	/* 0x0769: ClkReq-to-ClkSrc mapping */
	u8 pcie_clk_src_clk_req[18];
	u8 pcie_clk_src_clk_req_rsvd[14];
	u8 rsvd17[3];
	/* 0x078c: Clk Req GPIO Pin */
	u32 pcie_clk_req_gpio_mux[18];
	/* 0x07d4: Point of RsvdSmbusAddressTable */
	u32 rsvd_smbus_address_table_ptr;
	/* 0x07d8: Enable PCIE RP Mask */
	u32 pcie_rp_enable_mask;
	/* 0x07dc: VC Type */
	u8 pch_hda_vc_type;
	/*
	 * 0x07dd: Universal Audio Architecture compliance for DSP enabled
	 * system
	 */
	u8 pch_hda_dsp_uaa_compliance;
	/* 0x07de: Enable HD Audio Link */
	u8 pch_hda_audio_link_hda_enable;
	/* 0x07df: Enable HDA SDI lanes */
	u8 pch_hda_sdi_enable[2];
	/* 0x07e1: HDA Power/Clock Gating (PGD/CGD) */
	u8 pch_hda_test_power_clock_gating;
	/* 0x07e2: Enable HD Audio DMIC_N Link */
	u8 pch_hda_audio_link_dmic_enable[2];
	/* 0x07e4: DMIC<N> ClkA Pin Muxing (N - DMIC number) */
	u32 pch_hda_audio_link_dmic_clk_a_pin_mux[2];
	/* 0x07ec: DMIC<N> ClkB Pin Muxing */
	u32 pch_hda_audio_link_dmic_clk_b_pin_mux[2];
	/* 0x07f4: Enable HD Audio DSP */
	u8 pch_hda_dsp_enable;
	u8 rsvd18[3];
	/* 0x07f8: DMIC<N> Data Pin Muxing */
	u32 pch_hda_audio_link_dmic_data_pin_mux[2];
	/* 0x0800: Enable HD Audio SSP0 Link */
	u8 pch_hda_audio_link_ssp_enable[6];
	/* 0x0806: Enable HD Audio SoundWire#N Link */
	u8 pch_hda_audio_link_sndw_enable[4];
	/* 0x080a: iDisp-Link Frequency */
	u8 pch_hda_i_disp_link_frequency;
	/* 0x080b: iDisp-Link T-mode */
	u8 pch_hda_i_disp_link_tmode;
	/* 0x080c: iDisplay Audio Codec disconnection */
	u8 pch_hda_i_disp_codec_disconnect;
	/* 0x080d: CNVi DDR RFI Mitigation */
	u8 cnvi_ddr_rfim;
	/* 0x080e: Debug Interfaces */
	u8 pcd_debug_interface_flags;
	/* 0x080f: Serial Io Uart Debug Controller Number */
	u8 serial_io_uart_debug_controller_number;
	/* 0x0810: Serial Io Uart Debug Auto Flow */
	u8 serial_io_uart_debug_auto_flow;
	u8 rsvd19[3];
	/* 0x0814: Serial Io Uart Debug BaudRate */
	u32 serial_io_uart_debug_baud_rate;
	/* 0x0818: Serial Io Uart Debug Parity */
	u8 serial_io_uart_debug_parity;
	/* 0x0819: Serial Io Uart Debug Stop Bits */
	u8 serial_io_uart_debug_stop_bits;
	/* 0x081a: Serial Io Uart Debug Data Bits */
	u8 serial_io_uart_debug_data_bits;
	u8 rsvd20;
	/* 0x081c: Serial Io Uart Debug Mmio Base */
	u32 serial_io_uart_debug_mmio_base;
	/* 0x0820: ISA Serial Base selection */
	u8 pcd_isa_serial_uart_base;
	/* 0x0821: GT PLL voltage offset */
	u8 gt_pll_voltage_offset;
	/* 0x0822: Ring PLL voltage offset */
	u8 ring_pll_voltage_offset;
	/* 0x0823: System Agent PLL voltage offset */
	u8 sa_pll_voltage_offset;
	/* 0x0824: Memory Controller PLL voltage offset */
	u8 mc_pll_voltage_offset;
	/* 0x0825: MRC Safe Config */
	u8 mrc_safe_config;
	/* 0x0826: TCSS Thunderbolt PCIE Root Port 0 Enable */
	u8 tcss_itbt_pcie0_en;
	/* 0x0827: TCSS Thunderbolt PCIE Root Port 1 Enable */
	u8 tcss_itbt_pcie1_en;
	/* 0x0828: TCSS Thunderbolt PCIE Root Port 2 Enable */
	u8 tcss_itbt_pcie2_en;
	/* 0x0829: TCSS Thunderbolt PCIE Root Port 3 Enable */
	u8 tcss_itbt_pcie3_en;
	/* 0x082a: TCSS USB HOST (xHCI) Enable */
	u8 tcss_xhci_en;
	/* 0x082b: TCSS USB DEVICE (xDCI) Enable */
	u8 tcss_xdci_en;
	/* 0x082c: TCSS DMA0 Enable */
	u8 tcss_dma0_en;
	/* 0x082d: TCSS DMA1 Enable */
	u8 tcss_dma1_en;
	/* 0x082e: PcdSerialDebugBaudRate */
	u8 pcd_serial_debug_baud_rate;
	/* 0x082f: HobBufferSize */
	u8 hob_buffer_size;
	/* 0x0830: Early Command Training */
	u8 ect;
	/* 0x0831: SenseAmp Offset Training */
	u8 sot;
	/* 0x0832: Early ReadMPR Timing Centering 2D */
	u8 erdmprtc2_d;
	/* 0x0833: Read MPR Training */
	u8 rdmprt;
	/* 0x0834: Receive Enable Training */
	u8 rcvet;
	/* 0x0835: Jedec Write Leveling */
	u8 jwrl;
	/* 0x0836: Early Write Time Centering 2D */
	u8 ewrtc2_d;
	/* 0x0837: Early Read Time Centering 2D */
	u8 erdtc2_d;
	/* 0x0838: Write Timing Centering 1D */
	u8 wrtc1_d;
	/* 0x0839: Write Voltage Centering 1D */
	u8 wrvc1_d;
	/* 0x083a: Read Timing Centering 1D */
	u8 rdtc1_d;
	/* 0x083b: Dimm ODT Training */
	u8 dimmodtt;
	/* 0x083c: DIMM RON Training */
	u8 dimmront;
	/* 0x083d: Write Drive Strength/Equalization 2D */
	u8 wrdseqt;
	/* 0x083e: Write Slew Rate Training */
	u8 wrsrt;
	/* 0x083f: Read ODT Training */
	u8 rdodtt;
	/* 0x0840: Read Equalization Training */
	u8 rdeqt;
	/* 0x0841: Read Amplifier Training */
	u8 rdapt;
	/* 0x0842: Write Timing Centering 2D */
	u8 wrtc2_d;
	/* 0x0843: Read Timing Centering 2D */
	u8 rdtc2_d;
	/* 0x0844: Write Voltage Centering 2D */
	u8 wrvc2_d;
	/* 0x0845: Read Voltage Centering 2D */
	u8 rdvc2_d;
	/* 0x0846: Command Voltage Centering */
	u8 cmdvc;
	/* 0x0847: Late Command Training */
	u8 lct;
	/* 0x0848: Round Trip Latency Training */
	u8 rtl;
	/* 0x0849: Turn Around Timing Training */
	u8 tat;
	/* 0x084a: Memory Test */
	u8 memtst;
	/* 0x084b: DIMM SPD Alias Test */
	u8 aliaschk;
	/* 0x084c: Receive Enable Centering 1D */
	u8 rcvenc1_d;
	/* 0x084d: Retrain Margin Check */
	u8 rmc;
	/* 0x084e: Write Drive Strength Up/Dn independently */
	u8 wrdsudt;
	/* 0x084f: ECC Support */
	u8 ecc_support;
	/* 0x0850: Memory Remap */
	u8 remap_enable;
	/* 0x0851: Rank Interleave support */
	u8 rank_interleave;
	/* 0x0852: Enhanced Interleave support */
	u8 enhanced_interleave;
	/* 0x0853: Ch Hash Support */
	u8 ch_hash_enable;
	/* 0x0854: Ch Hash Settings Override */
	u8 ch_hash_override;
	/* 0x0855: Extern Therm Status */
	u8 enable_extts;
	/* 0x0856: DDR PowerDown and idle counter */
	u8 enable_pwr_dn;
	/* 0x0857: DDR PowerDown and idle counter */
	u8 enable_pwr_dn_lpddr;
	/* 0x0858: SelfRefresh Enable */
	u8 sref_cfg_ena;
	/* 0x0859: Throttler CKEMin Defeature */
	u8 thrt_cke_min_defeat_lpddr;
	/* 0x085a: Throttler CKEMin Defeature */
	u8 thrt_cke_min_defeat;
	/* 0x085b: Row Hammer Select */
	u8 rh_select;
	/* 0x085c: Exit On Failure (MRC) */
	u8 exit_on_failure;
	/* 0x085d: New Features 1 - MRC */
	u8 new_feature_enable1;
	/* 0x085e: New Features 2 - MRC */
	u8 new_feature_enable2;
	/* 0x085f: Duty Cycle Correction Training */
	u8 dcc;
	/* 0x0860: Read Voltage Centering 1D */
	u8 rdvc1_d;
	/* 0x0861: TxDqTCO Comp Training */
	u8 txtco;
	/* 0x0862: ClkTCO Comp Training */
	u8 clktco;
	/* 0x0863: CMD Slew Rate Training */
	u8 cmdsr;
	/* 0x0864: CMD Drive Strength and Tx Equalization */
	u8 cmddseq;
	/* 0x0865: DIMM CA ODT Training */
	u8 dimmodtca;
	/* 0x0866: TxDqsTCO Comp Training */
	u8 txtcodqs;
	/* 0x0867: CMD/CTL Drive Strength Up/Dn 2D */
	u8 cmddrud;
	/* 0x0868: VccDLL Bypass Training */
	u8 vccdllbp;
	/* 0x0869: PanicVttDnLp Training */
	u8 pvttdnlp;
	/* 0x086a: Read Vref Decap Training* */
	u8 rdvrefdc;
	/* 0x086b: Vddq Training */
	u8 vddqt;
	/* 0x086c: Rank Margin Tool Per Bit */
	u8 rmtbit;
	/* 0x086d: ECC DFT feature */
	u8 ecc_dft_en;
	/* 0x086e: Write0 feature */
	u8 write0;
	/*
	 * 0x086f: Select if CLK0 is shared between Rank0 and Rank1 in DDR4 DDP
	 */
	u8 ddr4_ddp_shared_clock;
	/*
	 * 0x0870: Select if ZQ pin is shared between Rank0 and Rank1 in DDR4
	 * DDP
	 */
	u8 ddr4_ddp_shared_zq;
	/* 0x0871: Ch Hash Interleaved Bit */
	u8 ch_hash_interleave_bit;
	/* 0x0872: Ch Hash Mask */
	u16 ch_hash_mask;
	/* 0x0874: Base reference clock value */
	u32 b_clk_frequency;
	/* 0x0878: EPG DIMM Idd3N */
	u16 idd3n;
	/* 0x087a: EPG DIMM Idd3P */
	u16 idd3p;
	/* 0x087c: CMD Normalization */
	u8 cmdnorm;
	/* 0x087d: Early DQ Write Drive Strength and Equalization Training */
	u8 ewrdseq;
	/* 0x087e: MC_REFRESH_2X_MODE */
	u8 mc_refresh2_x;
	/* 0x087f: Idle Energy Mc0Ch0Dimm0 */
	u8 idle_energy_mc0_ch0_dimm0;
	/* 0x0880: Idle Energy Mc0Ch0Dimm1 */
	u8 idle_energy_mc0_ch0_dimm1;
	/* 0x0881: Idle Energy Mc0Ch1Dimm0 */
	u8 idle_energy_mc0_ch1_dimm0;
	/* 0x0882: Idle Energy Mc0Ch1Dimm1 */
	u8 idle_energy_mc0_ch1_dimm1;
	/* 0x0883: Idle Energy Mc1Ch0Dimm0 */
	u8 idle_energy_mc1_ch0_dimm0;
	/* 0x0884: Idle Energy Mc1Ch0Dimm1 */
	u8 idle_energy_mc1_ch0_dimm1;
	/* 0x0885: Idle Energy Mc1Ch1Dimm0 */
	u8 idle_energy_mc1_ch1_dimm0;
	/* 0x0886: Idle Energy Mc1Ch1Dimm1 */
	u8 idle_energy_mc1_ch1_dimm1;
	/* 0x0887: PowerDown Energy Mc0Ch0Dimm0 */
	u8 pd_energy_mc0_ch0_dimm0;
	/* 0x0888: PowerDown Energy Mc0Ch0Dimm1 */
	u8 pd_energy_mc0_ch0_dimm1;
	/* 0x0889: PowerDown Energy Mc0Ch1Dimm0 */
	u8 pd_energy_mc0_ch1_dimm0;
	/* 0x088a: PowerDown Energy Mc0Ch1Dimm1 */
	u8 pd_energy_mc0_ch1_dimm1;
	/* 0x088b: PowerDown Energy Mc1Ch0Dimm0 */
	u8 pd_energy_mc1_ch0_dimm0;
	/* 0x088c: PowerDown Energy Mc1Ch0Dimm1 */
	u8 pd_energy_mc1_ch0_dimm1;
	/* 0x088d: PowerDown Energy Mc1Ch1Dimm0 */
	u8 pd_energy_mc1_ch1_dimm0;
	/* 0x088e: PowerDown Energy Mc1Ch1Dimm1 */
	u8 pd_energy_mc1_ch1_dimm1;
	/* 0x088f: Activate Energy Mc0Ch0Dimm0 */
	u8 act_energy_mc0_ch0_dimm0;
	/* 0x0890: Activate Energy Mc0Ch0Dimm1 */
	u8 act_energy_mc0_ch0_dimm1;
	/* 0x0891: Activate Energy Mc0Ch1Dimm0 */
	u8 act_energy_mc0_ch1_dimm0;
	/* 0x0892: Activate Energy Mc0Ch1Dimm1 */
	u8 act_energy_mc0_ch1_dimm1;
	/* 0x0893: Activate Energy Mc1Ch0Dimm0 */
	u8 act_energy_mc1_ch0_dimm0;
	/* 0x0894: Activate Energy Mc1Ch0Dimm1 */
	u8 act_energy_mc1_ch0_dimm1;
	/* 0x0895: Activate Energy Mc1Ch1Dimm0 */
	u8 act_energy_mc1_ch1_dimm0;
	/* 0x0896: Activate Energy Mc1Ch1Dimm1 */
	u8 act_energy_mc1_ch1_dimm1;
	/* 0x0897: Read Energy Mc0Ch0Dimm0 */
	u8 rd_energy_mc0_ch0_dimm0;
	/* 0x0898: Read Energy Mc0Ch0Dimm1 */
	u8 rd_energy_mc0_ch0_dimm1;
	/* 0x0899: Read Energy Mc0Ch1Dimm0 */
	u8 rd_energy_mc0_ch1_dimm0;
	/* 0x089a: Read Energy Mc0Ch1Dimm1 */
	u8 rd_energy_mc0_ch1_dimm1;
	/* 0x089b: Read Energy Mc1Ch0Dimm0 */
	u8 rd_energy_mc1_ch0_dimm0;
	/* 0x089c: Read Energy Mc1Ch0Dimm1 */
	u8 rd_energy_mc1_ch0_dimm1;
	/* 0x089d: Read Energy Mc1Ch1Dimm0 */
	u8 rd_energy_mc1_ch1_dimm0;
	/* 0x089e: Read Energy Mc1Ch1Dimm1 */
	u8 rd_energy_mc1_ch1_dimm1;
	/* 0x089f: Write Energy Mc0Ch0Dimm0 */
	u8 wr_energy_mc0_ch0_dimm0;
	/* 0x08a0: Write Energy Mc0Ch0Dimm1 */
	u8 wr_energy_mc0_ch0_dimm1;
	/* 0x08a1: Write Energy Mc0Ch1Dimm0 */
	u8 wr_energy_mc0_ch1_dimm0;
	/* 0x08a2: Write Energy Mc0Ch1Dimm1 */
	u8 wr_energy_mc0_ch1_dimm1;
	/* 0x08a3: Write Energy Mc1Ch0Dimm0 */
	u8 wr_energy_mc1_ch0_dimm0;
	/* 0x08a4: Write Energy Mc1Ch0Dimm1 */
	u8 wr_energy_mc1_ch0_dimm1;
	/* 0x08a5: Write Energy Mc1Ch1Dimm0 */
	u8 wr_energy_mc1_ch1_dimm0;
	/* 0x08a6: Write Energy Mc1Ch1Dimm1 */
	u8 wr_energy_mc1_ch1_dimm1;
	/* 0x08a7: Throttler CKEMin Timer */
	u8 thrt_cke_min_tmr;
	/* 0x08a8: Allow Opp Ref Below Write Threhold */
	u8 allow_opp_ref_below_write_threhold;
	/* 0x08a9: Write Threshold */
	u8 write_threshold;
	/* 0x08aa: Rapl Power Floor Ch0 */
	u8 rapl_pwr_fl_ch0;
	/* 0x08ab: Rapl Power Floor Ch1 */
	u8 rapl_pwr_fl_ch1;
	/* 0x08ac: Command Rate Support */
	u8 en_cmd_rate;
	/* 0x08ad: REFRESH_2X_MODE */
	u8 refresh2_x;
	/* 0x08ae: Energy Performance Gain */
	u8 epg_enable;
	/* 0x08af: RH pTRR LFSR0 Mask */
	u8 lfsr0_mask;
	/* 0x08b0: User Manual Threshold */
	u8 user_threshold_enable;
	/* 0x08b1: User Manual Budget */
	u8 user_budget_enable;
	/* 0x08b2: Power Down Mode */
	u8 power_down_mode;
	/* 0x08b3: Pwr Down Idle Timer */
	u8 pwdwn_idle_counter;
	/* 0x08b4: Page Close Idle Timeout */
	u8 dis_pg_close_idle_timeout;
	/* 0x08b5: Bitmask of ranks that have CA bus terminated */
	u8 cmd_ranks_terminated;
	/* 0x08b6: PcdSerialDebugLevel */
	u8 pcd_serial_debug_level;
	/* 0x08b7: Safe Mode Support */
	u8 safe_mode;
	/* 0x08b8: Ask MRC to clear memory content */
	u8 clean_memory;
	/* 0x08b9: LpDdrDqDqsReTraining */
	u8 lp_ddr_dq_dqs_re_training;
	/* 0x08ba: TCSS USB Port Enable */
	u8 usb_tc_port_en_pre_mem;
	u8 rsvd21;
	/* 0x08bc: Post Code Output Port */
	u16 post_code_output_port;
	/* 0x08be: RMTLoopCount */
	u8 rmt_loop_count;
	/* 0x08bf: Enable/Disable SA CRID */
	u8 crid_enable;
	/* 0x08c0: WRC Feature */
	u8 wrc_feature_enable;
	u8 rsvd22[3];
	/* 0x08c4: BCLK RFI Frequency */
	u32 bclk_rfi_freq[4];
	/* 0x08d4: Size of PCIe IMR. */
	u16 pcie_imr_size;
	/* 0x08d6: Enable PCIe IMR */
	u8 pcie_imr_enabled;
	/* 0x08d7: Enable PCIe IMR */
	u8 pcie_imr_rp_location;
	/* 0x08d8: Root port number for IMR. */
	u8 pcie_imr_rp_selection;
	/* 0x08d9: SerialDebugMrcLevel */
	u8 serial_debug_mrc_level;
	/* 0x08da: Ddr4OneDpc */
	u8 ddr4_one_dpc;
	/* 0x08db: RH pTRR LFSR1 Mask */
	u8 lfsr1_mask;
	/* 0x08dc: LPDDR ODT RttWr */
	u8 lpddr_rtt_wr;
	/* 0x08dd: LPDDR ODT RttCa */
	u8 lpddr_rtt_ca;
	/* 0x08de: REFRESH_PANIC_WM */
	u8 refresh_panic_wm;
	/* 0x08df: REFRESH_HP_WM */
	u8 refresh_hp_wm;
	/* 0x08e0: Command Pins Mapping */
	u8 lp5_ccc_config;
	/* 0x08e1: Command Pins Mirrored */
	u8 cmd_mirror;
	/* 0x08e2: DIMM DFE Training */
	u8 dimmdfe;
	/* 0x08e3: Extended Bank Hashing */
	u8 extended_bank_hashing;
	/* 0x08e4: Refresh Watermarks */
	u8 refresh_wm;
	/* 0x08e5: MC_REFRESH_RATE */
	u8 mc_refresh_rate;
	/* 0x08e6: Periodic DCC */
	u8 periodic_dcc;
	/* 0x08e7: LpMode */
	u8 lp_mode;
	/* 0x08e8: TX DQS DCC Training */
	u8 txdqsdcc;
	/* 0x08e9: DRAM DCA Training */
	u8 dramdca;
	/* 0x08ea: EARLY DIMM DFE Training */
	u8 earlydimmdfe;
	/* 0x08eb: Skip external display device scanning */
	u8 skip_ext_gfx_scan;
	/* 0x08ec: Generate BIOS Data ACPI Table */
	u8 bdat_enable;
	/* 0x08ed: Lock PCU Thermal Management registers */
	u8 lock_pt_mregs;
	/* 0x08ee: Rsvd */
	u8 peg_gen3_rsvd;
	/* 0x08ef: Panel Power Enable */
	u8 panel_power_enable;
	/* 0x08f0: BdatTestType */
	u8 bdat_test_type;
	/* 0x08f1: DRAMEMPHASIS Training */
	u8 dramemphasis;
	u8 rsvd23[2];
	/* 0x08f4: PMR Size */
	u32 dma_buffer_size;
	/* 0x08f8: VT-d/IOMMU Boot Policy */
	u8 pre_boot_dma_mask;
	u8 rsvd24;
	/* 0x08fa: Delta T12 Power Cycle Delay required in ms */
	u16 delta_t12_power_cycle_delay;
	/* 0x08fc: Reuse Adl DDR5 Board or not */
	u8 reuse_adl_s_ddr5_board;
	/* 0x08fd: Oem T12 Delay Override */
	u8 oem_t12_delay_override;
	/* 0x08fe: DQS Offset Adjust Training */
	u8 dqsoffsetadjust;
	/* 0x08ff: SaPreMemTestRsvd */
	u8 sa_pre_mem_test_rsvd[88];
	u8 rsvd25;
	/* 0x0958: TotalFlashSize */
	u16 total_flash_size;
	/* 0x095a: BiosSize */
	u16 bios_size;
	/* 0x095c: SecurityTestRsvd */
	u8 security_test_rsvd[12];
	/* 0x0968: Smbus dynamic power gating */
	u8 smbus_dynamic_power_gating;
	/* 0x0969: Disable and Lock Watch Dog Register */
	u8 wdt_disable_and_lock;
	/* 0x096a: SMBUS SPD Write Disable */
	u8 smbus_spd_write_disable;
	/* 0x096b: Force ME DID Init Status */
	u8 did_init_stat;
	/* 0x096c: CPU Replaced Polling Disable */
	u8 disable_cpu_replaced_polling;
	/* 0x096d: Check HECI message before send */
	u8 disable_message_check;
	/* 0x096e: Skip MBP HOB */
	u8 skip_mbp_hob;
	/* 0x096f: HECI2 Interface Communication */
	u8 heci_communication2;
	/* 0x0970: Enable KT device */
	u8 kt_device_enable;
	/* 0x0971: Skip CPU replacement check */
	u8 skip_cpu_replacement_check;
	u8 rsvd26[2];
	/* 0x0974: Hybrid Graphics GPIO information for PEG 1 */
	u32 cpu_pcie1_rtd3_gpio[24];
	/* 0x09d4: Hybrid Graphics GPIO information for PEG 2 */
	u32 cpu_pcie2_rtd3_gpio[24];
	/* 0x0a34: Hybrid Graphics GPIO information for PEG 3 */
	u32 cpu_pcie3_rtd3_gpio[24];
	/* 0x0a94: Avx2 Voltage Guardband Scaling Factor */
	u8 avx2_voltage_scale_factor;
	/* 0x0a95: Avx512 Voltage Guardband Scaling Factor */
	u8 avx512_voltage_scale_factor;
	/* 0x0a96: Serial Io Uart Debug Mode */
	u8 serial_io_uart_debug_mode;
	u8 rsvd27;
	/* 0x0a98: SerialIoUartDebugRxPinMux - FSPT */
	u32 serial_io_uart_debug_rx_pin_mux;
	/* 0x0a9c: SerialIoUartDebugTxPinMux - FSPM */
	u32 serial_io_uart_debug_tx_pin_mux;
	/* 0x0aa0: SerialIoUartDebugRtsPinMux - FSPM */
	u32 serial_io_uart_debug_rts_pin_mux;
	/* 0x0aa4: SerialIoUartDebugCtsPinMux - FSPM */
	u32 serial_io_uart_debug_cts_pin_mux;
	/* 0x0aa8: Ppr Enable Type */
	u8 ppr_enable;
	/* 0x0aa9: Margin Limit Check */
	u8 margin_limit_check;
	/* 0x0aaa: Margin Limit L2 */
	u16 margin_limit_l2;
	/* 0x0aac: DEKEL CDR Relock */
	u8 cpu_pcie_rp_cdr_relock[4];
	/* 0x0ab0: DMI DEKEL CDR Relock */
	u8 dmi_cdr_relock;
	/* 0x0ab1: IbeccErrInjControl */
	u8 ibecc_err_inj_control;
	/* 0x0ab2: CPU PCIe root port connection type */
	u8 cpu_pcie_rp_slot_implemented[4];
	/* 0x0ab6: Ppr Run Once */
	u8 ppr_run_once;
	/* 0x0ab7: Post Package Repair */
	u8 ppr;
	/* 0x0ab8: IbeccErrInjAddress */
	u64 ibecc_err_inj_address;
	/* 0x0ac0: IbeccErrInjMask */
	u64 ibecc_err_inj_mask;
	/* 0x0ac8: IbeccErrInjCount */
	u32 ibecc_err_inj_count;
	/* 0x0acc: Pointer EnableDmaBuffer */
	u8 enable_dma_buffer[8];
	/* 0x0ad4: PLL Max Banding Ratio */
	u8 pll_max_banding_ratio;
	u8 rsvd29[3];
	/* 0x0ad8: Debug Value */
	u32 debug_value;
	/* 0x0adc: Pre-Mem GPIO table address */
	u32 board_gpio_table_pre_mem_address;
	/* 0x0ae0: tRFCpb */
	u16 t_rf_cpb;
	/* 0x0ae2: tRFC2 */
	u16 t_rfc2;
	/* 0x0ae4: tRFC4 */
	u16 t_rfc4;
	/* 0x0ae6: tRRD_L */
	u8 t_rrd_l;
	/* 0x0ae7: tRRD_S */
	u8 t_rrd_s;
	/* 0x0ae8: tWTR_L */
	u8 t_wtr_l;
	/* 0x0ae9: tCCD_L */
	u8 t_ccd_l;
	/* 0x0aea: tWTR_S */
	u8 t_wtr_s;
	u8 rsvd30[5];
	/* 0x0af0: EccErrInjAddress */
	u64 ecc_err_inj_address;
	/* 0x0af8: EccErrInjMask */
	u64 ecc_err_inj_mask;
	/* 0x0b00: EccErrInjCount */
	u32 ecc_err_inj_count;
	/* 0x0b04: Frequency Limit for 2DPC Mixed or non-POR Config */
	u16 freq_limit_mixed_config;
	/* 0x0b06: First Dimm BitMask */
	u8 first_dimm_bit_mask;
	/* 0x0b07: SAGV Switch Factor IA DDR BW */
	u8 sagv_switch_factor_ia;
	/* 0x0b08: SAGV Switch Factor GT DDR BW */
	u8 sagv_switch_factor_gt;
	/* 0x0b09: SAGV Switch Factor IO DDR BW */
	u8 sagv_switch_factor_io;
	/* 0x0b0a: SAGV Switch Factor IA and GT Stall */
	u8 sagv_switch_factor_stall;
	/* 0x0b0b: Threshold For Switch Down */
	u8 sagv_heuristics_down_control;
	/* 0x0b0c: Threshold For Switch Up */
	u8 sagv_heuristics_up_control;
	u8 rsvd31;
	/* 0x0b0e: Frequency Limit for Mixed 2DPC DDR5 1 Rank 8GB and 8GB */
	u16 freq_limit_mixed_config_1_r1_r_8_gb;
	/* 0x0b10: Frequency Limit for Mixed 2DPC DDR5 1 Rank 16GB and 16GB */
	u16 freq_limit_mixed_config_1_r1_r_16_gb;
	/* 0x0b12: Frequency Limit for Mixed 2DPC DDR5 1 Rank 8GB and 16GB */
	u16 freq_limit_mixed_config_1_r1_r_8_gb_16_gb;
	/* 0x0b14: Frequency Limit for Mixed 2DPC DDR5 2 Rank */
	u16 freq_limit_mixed_config_2_r2_r;
	/* 0x0b16: DMI Hw Eq Gen3 CoeffList Cm */
	u8 pch_dmi_hw_eq_gen3_coeff_list_cm[8];
	/* 0x0b1e: DMI Hw Eq Gen3 CoeffList Cp */
	u8 pch_dmi_hw_eq_gen3_coeff_list_cp[8];
	/* 0x0b26: LCT Command eyewidth */
	u16 lct_cmd_eye_width;
	/* 0x0b28: For LPDDR Only: Throttler CKEMin Timer */
	u8 thrt_cke_min_tmr_lpddr;
	/* 0x0b29: First ECC Dimm BitMask */
	u8 first_dimm_bit_mask_ecc;
	/* 0x0b2a: LP5 Bank Mode */
	u8 lp5_bank_mode;
	/* 0x0b2b: Write DS Training */
	u8 wrds;
	/* 0x0b2c: SAM Overlaoding */
	u8 overload_sam;
	/* 0x0b2d: Time Measure */
	u8 mrc_time_measure;
	/* 0x0b2e: Dfe Gain */
	u8 dfe_gain;
	/* 0x0b2f: CsPiStartHighinEct */
	u8 cs_pi_start_highin_ect;
	/*
	 * 0x0b30: Use user provided power weights, and channel power floor
	 * values
	 */
	u8 user_power_weights_en;
	/* 0x0b31: DisableFGRAndPBRWA */
	u8 disable_fgr_and_pbrwa;
	/* 0x0b32: LowerBasicMemTestSize */
	u8 lower_basic_mem_test_size;
	/* 0x0b33: DisableSagvReorder */
	u8 disable_sagv_reorder;
	u8 reserved_fspm_upd2[4];
};

/**
 * struct fspm_upd - complete FSP-M UPD region, passed to FspMemoryInit()
 *
 * @terminator: Must be FSPT_UPD_TERMINATOR
 */
struct __packed fspm_upd {
	struct fsp_upd_header header;
	struct fspm_arch_upd arch;
	struct fsp_m_config config;
	u8 reserved[6];
	u16 terminator;
};

#endif
