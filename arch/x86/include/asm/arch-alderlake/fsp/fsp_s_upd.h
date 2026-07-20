/* SPDX-License-Identifier: Intel */
/*
 * Copyright (c) 2022, Intel Corporation. All rights reserved.
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * FSP-S Updateable Product Data (UPD) for Alder Lake, converted from the
 * FspsUpd.h shipped with the FSP binary, whose fields carry their offsets
 */

#ifndef _ASM_ARCH_FSP_S_UPD_H
#define _ASM_ARCH_FSP_S_UPD_H

#include <linux/build_bug.h>
#include <linux/types.h>
#include <asm/fsp2/fsp_api.h>
#include <asm/arch/fsp/fsp_configs.h>

/**
 * struct fsps_arch_upd - architectural settings for FSP-S
 *
 * @revision: Revision of this structure
 * @length: Size of this structure in bytes
 * @fsp_event_handler: Optional handler which the FSP calls to report
 *	events as it runs (0 for none). This is a 32-bit pointer on the
 *	wire, since the FSP binary is 32-bit, so it must not be void *
 */
struct __packed fsps_arch_upd {
	u8 revision;
	u8 reserved[3];
	u32 length;
	u32 fsp_event_handler;
	u8 reserved1[20];
};

/**
 * struct fsp_s_config - silicon-init settings
 */
struct __packed fsp_s_config {
	/* 0x0040: Logo Pointer */
	u32 logo_ptr;
	/* 0x0044: Logo Size */
	u32 logo_size;
	/* 0x0048: Blt Buffer Address */
	u32 blt_buffer_address;
	/* 0x004c: Blt Buffer Size */
	u32 blt_buffer_size;
	/* 0x0050: Graphics Configuration Ptr */
	u32 graphics_config_ptr;
	/* 0x0054: Enable Device 4 */
	u8 device4_enable;
	/* 0x0055: Show SPI controller */
	u8 show_spi_controller;
	u8 rsvd00[2];
	/* 0x0058: MicrocodeRegionBase */
	u32 microcode_region_base;
	/* 0x005c: MicrocodeRegionSize */
	u32 microcode_region_size;
	/* 0x0060: Turbo Mode */
	u8 turbo_mode;
	/* 0x0061: Enable SATA SALP Support */
	u8 sata_salp_support;
	/* 0x0062: Enable SATA ports */
	u8 sata_ports_enable[8];
	/* 0x006a: Enable SATA DEVSLP Feature */
	u8 sata_ports_dev_slp[8];
	u8 rsvd01[2];
	/* 0x0074: SATA DEVSLP GPIO Pin */
	u32 sata_port_dev_slp_pin_mux[8];
	/* 0x0094: Enable USB2 ports */
	u8 port_usb20_enable[16];
	/* 0x00a4: Enable USB3 ports */
	u8 port_usb30_enable[10];
	/* 0x00ae: Enable xDCI controller */
	u8 xdci_enable;
	u8 rsvd02;
	/* 0x00b0: Address of PCH_DEVICE_INTERRUPT_CONFIG table. */
	u32 dev_int_config_ptr;
	/* 0x00b4: Number of DevIntConfig Entry */
	u8 num_of_dev_int_config;
	/* 0x00b5: PIRQx to IRQx Map Config */
	u8 px_rc_config[8];
	/* 0x00bd: Select GPIO IRQ Route */
	u8 gpio_irq_route;
	/* 0x00be: Select SciIrqSelect */
	u8 sci_irq_select;
	/* 0x00bf: Select TcoIrqSelect */
	u8 tco_irq_select;
	/* 0x00c0: Enable/Disable Tco IRQ */
	u8 tco_irq_enable;
	/* 0x00c1: PCH HDA Verb Table Entry Number */
	u8 pch_hda_verb_table_entry_num;
	u8 rsvd03[2];
	/* 0x00c4: PCH HDA Verb Table Pointer */
	u32 pch_hda_verb_table_ptr;
	/* 0x00c8: PCH HDA Codec Sx Wake Capability */
	u8 pch_hda_codec_sx_wake_capability;
	/* 0x00c9: Enable SATA */
	u8 sata_enable;
	/* 0x00ca: SATA Mode */
	u8 sata_mode;
	/* 0x00cb: SPIn Device Mode */
	u8 serial_io_spi_mode[7];
	/* 0x00d2: SPI<N> Chip Select Polarity */
	u8 serial_io_spi_cs_polarity[14];
	/* 0x00e0: SPI<N> Chip Select Enable */
	u8 serial_io_spi_cs_enable[14];
	/* 0x00ee: SPIn Default Chip Select Output */
	u8 serial_io_spi_default_cs_output[7];
	/* 0x00f5: SPIn Default Chip Select Mode HW/SW */
	u8 serial_io_spi_cs_mode[7];
	/* 0x00fc: SPIn Default Chip Select State Low/High */
	u8 serial_io_spi_cs_state[7];
	/* 0x0103: UARTn Device Mode */
	u8 serial_io_uart_mode[7];
	u8 rsvd04[2];
	/* 0x010c: Default BaudRate for each Serial IO UART */
	u32 serial_io_uart_baud_rate[7];
	/* 0x0128: Default ParityType for each Serial IO UART */
	u8 serial_io_uart_parity[7];
	/* 0x012f: Default DataBits for each Serial IO UART */
	u8 serial_io_uart_data_bits[7];
	/* 0x0136: Default StopBits for each Serial IO UART */
	u8 serial_io_uart_stop_bits[7];
	/*
	 * 0x013d: Power Gating mode for each Serial IO UART that works in COM
	 * mode
	 */
	u8 serial_io_uart_power_gating[7];
	/* 0x0144: Enable Dma for each Serial IO UART that supports it */
	u8 serial_io_uart_dma_enable[7];
	/* 0x014b: Enables UART hardware flow control, CTS and RTS lines */
	u8 serial_io_uart_auto_flow[7];
	u8 rsvd05[2];
	/* 0x0154: SerialIoUartRtsPinMuxPolicy */
	u32 serial_io_uart_rts_pin_mux_policy[7];
	/* 0x0170: SerialIoUartCtsPinMuxPolicy */
	u32 serial_io_uart_cts_pin_mux_policy[7];
	/* 0x018c: SerialIoUartRxPinMuxPolicy */
	u32 serial_io_uart_rx_pin_mux_policy[7];
	/* 0x01a8: SerialIoUartTxPinMuxPolicy */
	u32 serial_io_uart_tx_pin_mux_policy[7];
	/* 0x01c4: UART Number For Debug Purpose */
	u8 serial_io_debug_uart_number;
	/* 0x01c5: Serial IO UART DBG2 table */
	u8 serial_io_uart_dbg2[7];
	/* 0x01cc: I2Cn Device Mode */
	u8 serial_io_i2c_mode[8];
	/* 0x01d4: Serial IO I2C SDA Pin Muxing */
	u32 pch_serial_io_i2c_sda_pin_mux[8];
	/* 0x01f4: Serial IO I2C SCL Pin Muxing */
	u32 pch_serial_io_i2c_scl_pin_mux[8];
	/* 0x0214: PCH SerialIo I2C Pads Termination */
	u8 pch_serial_io_i2c_pads_termination[8];
	/* 0x021c: ISH GP GPIO Pin Muxing */
	u32 ish_gp_gpio_pin_muxing[8];
	/* 0x023c: ISH UART Rx Pin Muxing */
	u32 ish_uart_rx_pin_muxing[3];
	/* 0x0248: ISH UART Tx Pin Muxing */
	u32 ish_uart_tx_pin_muxing[3];
	/* 0x0254: ISH UART Rts Pin Muxing */
	u32 ish_uart_rts_pin_muxing[3];
	/* 0x0260: ISH UART Rts Pin Muxing */
	u32 ish_uart_cts_pin_muxing[3];
	/* 0x026c: ISH I2C SDA Pin Muxing */
	u32 ish_i2c_sda_pin_muxing[3];
	/* 0x0278: ISH I2C SCL Pin Muxing */
	u32 ish_i2c_scl_pin_muxing[3];
	/* 0x0284: ISH SPI MOSI Pin Muxing */
	u32 ish_spi_mosi_pin_muxing[2];
	/* 0x028c: ISH SPI MISO Pin Muxing */
	u32 ish_spi_miso_pin_muxing[2];
	/* 0x0294: ISH SPI CLK Pin Muxing */
	u32 ish_spi_clk_pin_muxing[2];
	/* 0x029c: ISH SPI CS#N Pin Muxing */
	u32 ish_spi_cs_pin_muxing[4];
	/* 0x02ac: ISH GP GPIO Pad termination */
	u8 ish_gp_gpio_pad_termination[8];
	/* 0x02b4: ISH UART Rx Pad termination */
	u8 ish_uart_rx_pad_termination[3];
	/* 0x02b7: ISH UART Tx Pad termination */
	u8 ish_uart_tx_pad_termination[3];
	/* 0x02ba: ISH UART Rts Pad termination */
	u8 ish_uart_rts_pad_termination[3];
	/* 0x02bd: ISH UART Rts Pad termination */
	u8 ish_uart_cts_pad_termination[3];
	/* 0x02c0: ISH I2C SDA Pad termination */
	u8 ish_i2c_sda_pad_termination[3];
	/* 0x02c3: ISH I2C SCL Pad termination */
	u8 ish_i2c_scl_pad_termination[3];
	/* 0x02c6: ISH SPI MOSI Pad termination */
	u8 ish_spi_mosi_pad_termination[2];
	/* 0x02c8: ISH SPI MISO Pad termination */
	u8 ish_spi_miso_pad_termination[2];
	/* 0x02ca: ISH SPI CLK Pad termination */
	u8 ish_spi_clk_pad_termination[2];
	/* 0x02cc: ISH SPI CS#N Pad termination */
	u8 ish_spi_cs_pad_termination[4];
	/* 0x02d0: Enable PCH ISH SPI Cs#N pins assigned */
	u8 pch_ish_spi_cs_enable[4];
	/* 0x02d4: USB Per Port HS Preemphasis Bias */
	u8 usb2_phy_petxiset[16];
	/* 0x02e4: USB Per Port HS Transmitter Bias */
	u8 usb2_phy_txiset[16];
	/* 0x02f4: USB Per Port HS Transmitter Emphasis */
	u8 usb2_phy_predeemp[16];
	/* 0x0304: USB Per Port Half Bit Pre-emphasis */
	u8 usb2_phy_pehalfbit[16];
	/*
	 * 0x0314: Enable the write to USB 3.0 TX Output -3.5dB De-Emphasis
	 * Adjustment
	 */
	u8 usb3_hsio_tx_de_emph_enable[10];
	/* 0x031e: USB 3.0 TX Output -3.5dB De-Emphasis Adjustment Setting */
	u8 usb3_hsio_tx_de_emph[10];
	/*
	 * 0x0328: Enable the write to USB 3.0 TX Output Downscale Amplitude
	 * Adjustment
	 */
	u8 usb3_hsio_tx_downscale_amp_enable[10];
	/* 0x0332: USB 3.0 TX Output Downscale Amplitude Adjustment */
	u8 usb3_hsio_tx_downscale_amp[10];
	u8 pch_usb3_hsio_ctrl_adapt_offset_cfg_enable[10];
	u8 pch_usb3_hsio_filter_sel_n_enable[10];
	u8 pch_usb3_hsio_filter_sel_p_enable[10];
	u8 pch_usb3_hsio_olfps_cfg_pull_up_dwn_res_enable[10];
	u8 pch_usb3_hsio_ctrl_adapt_offset_cfg[10];
	u8 pch_usb3_hsio_olfps_cfg_pull_up_dwn_res[10];
	u8 pch_usb3_hsio_filter_sel_n[10];
	u8 pch_usb3_hsio_filter_sel_p[10];
	/* 0x038c: Enable LAN */
	u8 pch_lan_enable;
	/* 0x038d: Enable PCH TSN */
	u8 pch_tsn_enable;
	/* 0x038e: TSN Link Speed */
	u8 pch_tsn_link_speed;
	u8 rsvd06;
	/* 0x0390: PCH TSN MAC Address High Bits */
	u32 pch_tsn_mac_address_high;
	/* 0x0394: PCH TSN MAC Address Low Bits */
	u32 pch_tsn_mac_address_low;
	/* 0x0398: PCIe PTM enable/disable */
	u8 pcie_ptm[28];
	/* 0x03b4: PCIe DPC enable/disable */
	u8 pcie_dpc[28];
	/* 0x03d0: PCIe DPC extensions enable/disable */
	u8 pcie_edpc[28];
	/* 0x03ec: USB PDO Programming */
	u8 usb_pdo_programming;
	u8 rsvd07[3];
	/* 0x03f0: Power button debounce configuration */
	u32 pmc_power_button_debounce;
	/* 0x03f4: PCH eSPI Host and Device BME enabled */
	u8 pch_espi_bme_master_slave_enabled;
	/* 0x03f5: PCH eSPI Link Configuration Lock (SBLCL) */
	u8 pch_espi_lock_link_configuration;
	/*
	 * 0x03f6: Mask to enable the usage of external V1p05 VR rail in
	 * specific S0ix or Sx states
	 */
	u8 pch_fivr_ext_v1p05_rail_enabled_states;
	/*
	 * 0x03f7: Mask to enable the platform configuration of external V1p05
	 * VR rail
	 */
	u8 pch_fivr_ext_v1p05_rail_supported_voltage_states;
	/*
	 * 0x03f8: External V1P05 Voltage Value that will be used in S0i2/S0i3
	 * states
	 */
	u16 pch_fivr_ext_v1p05_rail_voltage;
	/* 0x03fa: External V1P05 Icc Max Value */
	u8 pch_fivr_ext_v1p05_rail_icc_max;
	/*
	 * 0x03fb: Mask to enable the usage of external Vnn VR rail in specific
	 * S0ix or Sx states
	 */
	u8 pch_fivr_ext_vnn_rail_enabled_states;
	/*
	 * 0x03fc: Mask to enable the platform configuration of external Vnn VR
	 * rail
	 */
	u8 pch_fivr_ext_vnn_rail_supported_voltage_states;
	u8 rsvd08;
	/*
	 * 0x03fe: External Vnn Voltage Value that will be used in S0ix/Sx
	 * states
	 */
	u16 pch_fivr_ext_vnn_rail_voltage;
	/*
	 * 0x0400: External Vnn Icc Max Value that will be used in S0ix/Sx
	 * states
	 */
	u8 pch_fivr_ext_vnn_rail_icc_max;
	/*
	 * 0x0401: Mask to enable the usage of external Vnn VR rail in Sx states
	 */
	u8 pch_fivr_ext_vnn_rail_sx_enabled_states;
	/* 0x0402: External Vnn Voltage Value that will be used in Sx states */
	u16 pch_fivr_ext_vnn_rail_sx_voltage;
	/* 0x0404: External Vnn Icc Max Value that will be used in Sx states */
	u8 pch_fivr_ext_vnn_rail_sx_icc_max;
	/*
	 * 0x0405: Transition time in microseconds from Low Current Mode Voltage
	 * to High Current Mode Voltage
	 */
	u8 pch_fivr_vccin_aux_low_to_high_cur_mode_vol_tran_time;
	/*
	 * 0x0406: Transition time in microseconds from Retention Mode Voltage
	 * to High Current Mode Voltage
	 */
	u8 pch_fivr_vccin_aux_ret_to_high_cur_mode_vol_tran_time;
	/*
	 * 0x0407: Transition time in microseconds from Retention Mode Voltage
	 * to Low Current Mode Voltage
	 */
	u8 pch_fivr_vccin_aux_ret_to_low_cur_mode_vol_tran_time;
	/*
	 * 0x0408: Transition time in microseconds from Off (0V) to High Current
	 * Mode Voltage
	 */
	u16 pch_fivr_vccin_aux_off_to_high_cur_mode_vol_tran_time;
	/* 0x040a: PMC Debug Message Enable */
	u8 pmc_dbg_msg_en;
	u8 rsvd09[1];
	/* 0x040c: Pointer of ChipsetInit Binary */
	u32 chipset_init_bin_ptr;
	/* 0x0410: Length of ChipsetInit Binary */
	u32 chipset_init_bin_len;
	/* 0x0414: FIVR Dynamic Power Management */
	u8 pch_fivr_dyn_pm;
	/* 0x0415: FIVR VCCST ICCMax Control */
	u8 pch_fivr_vccst_icc_max_control;
	/* 0x0416: External V1P05 Icc Max Value */
	u16 pch_fivr_ext_v1p05_rail_icc_maximum;
	/*
	 * 0x0418: External Vnn Icc Max Value that will be used in S0ix/Sx
	 * states
	 */
	u16 pch_fivr_ext_vnn_rail_icc_maximum;
	/* 0x041a: External Vnn Icc Max Value that will be used in Sx states */
	u16 pch_fivr_ext_vnn_rail_sx_icc_maximum;
	/* 0x041c: Extented BIOS Direct Read Decode enable */
	u8 pch_spi_extended_bios_decode_range_enable;
	u8 rsvd10[3];
	/* 0x0420: Extended BIOS Direct Read Decode Range base */
	u32 pch_spi_extended_bios_decode_range_base;
	/* 0x0424: Extended BIOS Direct Read Decode Range limit */
	u32 pch_spi_extended_bios_decode_range_limit;
	/* 0x0428: USB Audio Offload enable */
	u8 pch_xhci_uaol_enable;
	u8 rsvd11[3];
	/* 0x042c: Pointer of SYNPS PHY Binary */
	u32 synps_phy_bin_ptr;
	/* 0x0430: Length of SYNPS PHY Binary */
	u32 synps_phy_bin_len;
	/* 0x0434: CNVi Configuration */
	u8 cnvi_mode;
	/* 0x0435: CNVi Wi-Fi Core */
	u8 cnvi_wifi_core;
	/* 0x0436: CNVi BT Core */
	u8 cnvi_bt_core;
	/* 0x0437: CNVi BT Audio Offload */
	u8 cnvi_bt_audio_offload;
	/* 0x0438: CNVi RF_RESET pin muxing */
	u32 cnvi_rf_reset_pin_mux;
	/* 0x043c: CNVi CLKREQ pin muxing */
	u32 cnvi_clkreq_pin_mux;
	/* 0x0440: Enable Host C10 reporting through eSPI */
	u8 pch_espi_host_c10_report_enable;
	/* 0x0441: PCH USB2 PHY Power Gating enable */
	u8 pmc_usb2_phy_sus_pg_enable;
	/* 0x0442: PCH USB OverCurrent mapping enable */
	u8 pch_usb_over_current_enable;
	/* 0x0443: Espi Lgmr Memory Range decode */
	u8 pch_espi_lgmr_enable;
	/* 0x0444: External V1P05 Control Ramp Timer value */
	u8 pch_fivr_ext_v1p05_rail_ctrl_ramp_tmr;
	/* 0x0445: External VNN Control Ramp Timer value */
	u8 pch_fivr_ext_vnn_rail_ctrl_ramp_tmr;
	/* 0x0446: Set SATA DEVSLP GPIO Reset Config */
	u8 sata_ports_dev_slp_reset_config[8];
	/* 0x044e: PCHHOT# pin */
	u8 pch_hot_enable;
	/* 0x044f: SATA LED */
	u8 sata_led_enable;
	/* 0x0450: VRAlert# Pin */
	u8 pch_pm_vr_alert;
	/* 0x0451: AMT Switch */
	u8 amt_enabled;
	/* 0x0452: WatchDog Timer Switch */
	u8 watch_dog_enabled;
	/* 0x0453: PET Progress */
	u8 fw_progress;
	/* 0x0454: SOL Switch */
	u8 amt_sol_enabled;
	u8 rsvd12;
	/* 0x0456: OS Timer */
	u16 watch_dog_timer_os;
	/* 0x0458: BIOS Timer */
	u16 watch_dog_timer_bios;
	/* 0x045a: Force MEBX execution */
	u8 forc_mebx_sync_up;
	/* 0x045b: PCH PCIe root port connection type */
	u8 pcie_rp_slot_implemented[28];
	/* 0x0477: PCIE RP Access Control Services Extended Capability */
	u8 pcie_rp_acs_enabled[28];
	/* 0x0493: PCIE RP Clock Power Management */
	u8 pcie_rp_enable_cpm[28];
	u8 rsvd13[1];
	/* 0x04b0: PCIE RP Detect Timeout Ms */
	u16 pcie_rp_detect_timeout_ms[28];
	/* 0x04e8: ModPHY SUS Power Domain Dynamic Gating */
	u8 pmc_mod_phy_sus_pg_enable;
	/* 0x04e9: V1p05-PHY supply external FET control */
	u8 pmc_v1p05_phy_ext_fet_control_en;
	/* 0x04ea: V1p05-IS supply external FET control */
	u8 pmc_v1p05_is_ext_fet_control_en;
	/* 0x04eb: Enable/Disable PavpEnable */
	u8 pavp_enable;
	/* 0x04ec: CdClock Frequency selection */
	u8 cd_clock;
	/* 0x04ed: Enable/Disable PeiGraphicsPeimInit */
	u8 pei_graphics_peim_init;
	/* 0x04ee: Enable D3 Hot in TCSS */
	u8 d3_hot_enable;
	/* 0x04ef: Enable or disable GNA device */
	u8 gna_enable;
	/* 0x04f0: TypeC port GPIO setting */
	u32 iom_type_c_port_pad_cfg[8];
	/* 0x0510: CPU USB3 Port Over Current Pin */
	u8 cpu_usb3_over_current_pin[8];
	/* 0x0518: Enable D3 Cold in TCSS */
	u8 d3_cold_enable;
	/* 0x0519: Enable/Disable PCIe tunneling for USB4 */
	u8 i_tbt_pcie_tunneling_for_usb4;
	/* 0x051a: Enable/Disable SkipFspGop */
	u8 skip_fsp_gop;
	/* 0x051b: TC State in TCSS */
	u8 tc_cstate_limit;
	/* 0x051c: Intel Graphics VBT (Video BIOS Table) Size */
	u32 vbt_size;
	/* 0x0520: Platform LID Status for LFP Displays. */
	u8 lid_status;
	/* 0x0521: Set Iom stay in TC cold seconds in TCSS */
	u8 iom_stay_in_tc_cold_seconds;
	/* 0x0522: Set Iom before entering TC cold seconds in TCSS */
	u8 iom_before_entering_tc_cold_seconds;
	/* 0x0523: SaPostMemRsvd */
	u8 sa_post_mem_rsvd[5];
	/* 0x0528: PCH xHCI enable HS Interrupt IN Alarm */
	u8 pch_xhci_hsii_enable;
	/* 0x0529: Enable VMD controller */
	u8 vmd_enable;
	/* 0x052a: Map port under VMD */
	u8 vmd_port[31];
	/* 0x0549: VMD Port Device */
	u8 vmd_port_dev[31];
	/* 0x0568: VMD Port Func */
	u8 vmd_port_func[31];
	/* 0x0587: VMD Config Bar size */
	u8 vmd_cfg_bar_size;
	/* 0x0588: VMD Config Bar Attributes */
	u8 vmd_cfg_bar_attr;
	/* 0x0589: VMD Mem Bar1 size */
	u8 vmd_mem_bar_size1;
	/* 0x058a: VMD Mem Bar1 Attributes */
	u8 vmd_mem_bar1_attr;
	/* 0x058b: VMD Mem Bar2 size */
	u8 vmd_mem_bar_size2;
	/* 0x058c: VMD Mem Bar2 Attributes */
	u8 vmd_mem_bar2_attr;
	u8 rsvd14[3];
	/* 0x0590: VMD Variable */
	u32 vmd_variable_ptr;
	/* 0x0594: Temporary CfgBar address for VMD */
	u32 vmd_cfg_bar_base;
	/* 0x0598: Temporary MemBar1 address for VMD */
	u32 vmd_mem_bar1_base;
	/* 0x059c: Temporary MemBar2 address for VMD */
	u32 vmd_mem_bar2_base;
	/* 0x05a0: TCSS CPU USB PDO Programming */
	u8 tcss_cpu_usb_pdo_programming;
	/* 0x05a1: Enable/Disable PMC-PD Solution */
	u8 pmc_pd_enable;
	/* 0x05a2: TCSS Aux Orientation Override Enable */
	u16 tcss_aux_ori;
	/* 0x05a4: TCSS HSL Orientation Override Enable */
	u16 tcss_hsl_ori;
	/* 0x05a6: USB override in IOM */
	u8 usb_override;
	/* 0x05a7: ITBT Root Port Enable */
	u8 i_tbt_pcie_root_port_en[4];
	/* 0x05ab: TCSS USB Port Enable */
	u8 usb_tc_port_en;
	/* 0x05ac: ITBTForcePowerOn Timeout value */
	u16 i_tbt_force_power_on_timeout_in_ms;
	/* 0x05ae: ITbtConnectTopology Timeout value */
	u16 i_tbt_connect_topology_timeout_in_ms;
	/* 0x05b0: VCCST request for IOM */
	u8 vcc_st;
	u8 rsvd15[1];
	/* 0x05b2: ITBT DMA LTR */
	u16 i_tbt_dma_ltr[2];
	/* 0x05b6: Enable/Disable CrashLog */
	u8 cpu_crash_log_enable;
	/* 0x05b7: Enable/Disable PTM */
	u8 ptm_enabled[4];
	/* 0x05bb: PCIE RP Ltr Enable */
	u8 sa_pcie_itbt_rp_ltr_enable[4];
	/* 0x05bf: PCIE RP Snoop Latency Override Mode */
	u8 sa_pcie_itbt_rp_snoop_latency_override_mode[4];
	/* 0x05c3: PCIE RP Snoop Latency Override Multiplier */
	u8 sa_pcie_itbt_rp_snoop_latency_override_multiplier[4];
	u8 rsvd16[1];
	/* 0x05c8: PCIE RP Snoop Latency Override Value */
	u16 sa_pcie_itbt_rp_snoop_latency_override_value[4];
	/* 0x05d0: PCIE RP Non Snoop Latency Override Mode */
	u8 sa_pcie_itbt_rp_non_snoop_latency_override_mode[4];
	/* 0x05d4: PCIE RP Non Snoop Latency Override Multiplier */
	u8 sa_pcie_itbt_rp_non_snoop_latency_override_multiplier[4];
	/* 0x05d8: PCIE RP Non Snoop Latency Override Value */
	u16 sa_pcie_itbt_rp_non_snoop_latency_override_value[4];
	/* 0x05e0: Force LTR Override */
	u8 sa_pcie_itbt_rp_force_ltr_override[4];
	/* 0x05e4: PCIE RP Ltr Config Lock */
	u8 sa_pcie_itbt_rp_ltr_config_lock[4];
	/* 0x05e8: Advanced Encryption Standard (AES) feature */
	u8 aes_enable;
	/* 0x05e9: Power State 3 enable/disable */
	u8 psi3_enable[5];
	/* 0x05ee: Power State 4 enable/disable */
	u8 psi4_enable[5];
	u8 rsvd17[1];
	/* 0x05f4: Imon slope correction */
	u16 imon_slope[5];
	/* 0x05fe: Imon offset correction */
	u16 imon_offset[5];
	/* 0x0608: Enable/Disable BIOS configuration of VR */
	u8 vr_config_enable[5];
	/* 0x060d: Thermal Design Current enable/disable */
	u8 tdc_enable[5];
	u8 rsvd18[2];
	/* 0x0614: Thermal Design Current time window */
	u32 tdc_time_window[5];
	/* 0x0628: Thermal Design Current Lock */
	u8 tdc_lock[5];
	/* 0x062d: Platform Psys slope correction */
	u8 psys_slope;
	/* 0x062e: Platform Psys offset correction */
	u16 psys_offset;
	/* 0x0630: Acoustic Noise Mitigation feature */
	u8 acoustic_noise_mitigation;
	/*
	 * 0x0631: Disable Fast Slew Rate for Deep Package C States for VR
	 * domains
	 */
	u8 fast_pkg_c_ramp_disable[5];
	/*
	 * 0x0636: Slew Rate configuration for Deep Package C States for VR
	 * domains
	 */
	u8 slow_slew_rate[5];
	u8 rsvd19[1];
	/* 0x063c: Thermal Design Current limit */
	u16 tdc_current_limit[5];
	/* 0x0646: AcLoadline */
	u16 ac_loadline[5];
	/* 0x0650: DcLoadline */
	u16 dc_loadline[5];
	/* 0x065a: Power State 1 Threshold current */
	u16 psi1_threshold[5];
	/* 0x0664: Power State 2 Threshold current */
	u16 psi2_threshold[5];
	/* 0x066e: Power State 3 Threshold current */
	u16 psi3_threshold[5];
	/* 0x0678: Icc Max limit */
	u16 icc_max[5];
	/* 0x0682: Enable or Disable TXT */
	u8 txt_enable;
	/* 0x0683: Skip Multi-Processor Initialization */
	u8 skip_mp_init;
	/* 0x0684: FIVR RFI Frequency */
	u16 fivr_rfi_frequency;
	/* 0x0686: FIVR RFI Spread Spectrum */
	u8 fivr_spread_spectrum;
	u8 rsvd20;
	/* 0x0688: CpuBistData */
	u32 cpu_bist_data;
	/* 0x068c: CpuMpPpi */
	u32 cpu_mp_ppi;
	/* 0x0690: Pre Wake Randomization time */
	u8 pre_wake;
	/* 0x0691: Ramp Up Randomization time */
	u8 ramp_up;
	/* 0x0692: Ramp Down Randomization time */
	u8 ramp_down;
	u8 rsvd21[1];
	/* 0x0694: VR Voltage Limit */
	u16 vr_voltage_limit[5];
	/* 0x069e: VccIn Aux Imon IccMax */
	u16 vcc_in_aux_imon_icc_imax;
	/* 0x06a0: Vsys Critical */
	u8 enable_vsys_critical;
	/* 0x06a1: Vsys Full Scale */
	u8 vsys_full_scale;
	/* 0x06a2: Vsys Critical Threshold */
	u8 vsys_critical_threshold;
	/* 0x06a3: Assertion Deglitch Mantissa */
	u8 vsys_assertion_deglitch_mantissa;
	/* 0x06a4: Assertion Deglitch Exponent */
	u8 vsys_assertion_deglitch_exponent;
	/* 0x06a5: De assertion Deglitch Mantissa */
	u8 vsys_deassertion_deglitch_mantissa;
	/* 0x06a6: De assertion Deglitch Exponent */
	u8 vsys_deassertion_deglitch_exponent;
	/* 0x06a7: VccIn Aux Imon slope correction */
	u8 vcc_in_aux_imon_slope;
	/* 0x06a8: VccIn Aux Imon offset correction */
	u16 vcc_in_aux_imon_offset;
	/* 0x06aa: FIVR RFI Spread Spectrum Enable or disable */
	u8 fivr_spectrum_enable;
	u8 rsvd22[1];
	/* 0x06ac: VR Fast Vmode ICC Limit support */
	u16 icc_limit[5];
	u8 cpu_post_mem_rsvd[2];
	/* 0x06b8: PpinSupport to view Protected Processor Inventory Number */
	u8 ppin_support;
	/* 0x06b9: Enable or Disable Minimum Voltage Override */
	u8 enable_min_voltage_override;
	/* 0x06ba: Min Voltage for Runtime */
	u16 min_voltage_runtime;
	/* 0x06bc: Memory size per thread allocated for Processor Trace */
	u8 processor_trace_mem_size;
	u8 rsvd23;
	/* 0x06be: Min Voltage for C8 */
	u16 min_voltage_c8;
	/* 0x06c0: Smbios Type4 Max Speed Override */
	u16 smbios_type4_max_speed_override;
	/* 0x06c2: Current root mean square */
	u8 irms[5];
	/* 0x06c7: AvxDisable */
	u8 avx_disable;
	/* 0x06c8: Avx3Disable */
	u8 avx3_disable;
	/* 0x06c9: X2ApicSupport */
	u8 x2_apic_support;
	/* 0x06ca: CPU VR Power Delivery Design */
	u8 vr_power_delivery_design;
	/*
	 * 0x06cb: Enable/Disable VR FastVmode. The VR will initiate reactive
	 * protection if Fast Vmode is enabled.
	 */
	u8 enable_fast_vmode[5];
	/* 0x06d0: Vsys Full Scale */
	u32 vsys_full_scale1;
	/* 0x06d4: Vsys Critical Threshold */
	u32 vsys_critical_threshold1;
	/* 0x06d8: Psys Full Scale */
	u32 psys_full_scale;
	/* 0x06dc: Psys Critical Threshold */
	u32 psys_critical_threshold;
	/* 0x06e0: ReservedCpuPostMemProduction */
	u8 reserved_cpu_post_mem_production[11];
	/* 0x06eb: Enable Power Optimizer */
	u8 pch_pwr_opt_enable;
	/* 0x06ec: PCH Flash Protection Ranges Write Enable */
	u8 pch_write_protection_enable[5];
	/* 0x06f1: PCH Flash Protection Ranges Read Enable */
	u8 pch_read_protection_enable[5];
	/* 0x06f6: PCH Protect Range Limit */
	u16 pch_protected_range_limit[5];
	/* 0x0700: PCH Protect Range Base */
	u16 pch_protected_range_base[5];
	/* 0x070a: Enable Pme */
	u8 pch_hda_pme;
	/* 0x070b: HD Audio Link Frequency */
	u8 pch_hda_link_frequency;
	/* 0x070c: Enable PCH ISH SPI Cs0 pins assigned */
	u8 pch_ish_spi_cs0_enable[1];
	/* 0x070d: Enable PCH Io Apic Entry 24-119 */
	u8 pch_io_apic_entry24_119;
	/* 0x070e: PCH Io Apic ID */
	u8 pch_io_apic_id;
	/* 0x070f: Enable PCH ISH SPI pins assigned */
	u8 pch_ish_spi_enable[1];
	/* 0x0710: Enable PCH ISH UART pins assigned */
	u8 pch_ish_uart_enable[2];
	/* 0x0712: Enable PCH ISH I2C pins assigned */
	u8 pch_ish_i2c_enable[3];
	/* 0x0715: Enable PCH ISH GP pins assigned */
	u8 pch_ish_gp_enable[8];
	/* 0x071d: PCH ISH PDT Unlock Msg */
	u8 pch_ish_pdt_unlock;
	/* 0x071e: Enable PCH Lan LTR capability of PCH internal LAN */
	u8 pch_lan_ltr_enable;
	/* 0x071f: Enable LOCKDOWN BIOS LOCK */
	u8 pch_lock_down_bios_lock;
	/* 0x0720: PCH Compatibility Revision ID */
	u8 pch_crid;
	/* 0x0721: RTC BIOS Interface Lock */
	u8 rtc_bios_interface_lock;
	/* 0x0722: RTC Cmos Memory Lock */
	u8 rtc_memory_lock;
	/* 0x0723: Enable PCIE RP HotPlug */
	u8 pcie_rp_hot_plug[28];
	/* 0x073f: Enable PCIE RP Pm Sci */
	u8 pcie_rp_pm_sci[28];
	/* 0x075b: Enable PCIE RP Transmitter Half Swing */
	u8 pcie_rp_transmitter_half_swing[28];
	/* 0x0777: Enable PCIE RP Clk Req Detect */
	u8 pcie_rp_clk_req_detect[28];
	/* 0x0793: PCIE RP Advanced Error Report */
	u8 pcie_rp_advanced_error_reporting[28];
	/* 0x07af: PCIE RP Unsupported Request Report */
	u8 pcie_rp_unsupported_request_report[28];
	/* 0x07cb: PCIE RP Fatal Error Report */
	u8 pcie_rp_fatal_error_report[28];
	/* 0x07e7: PCIE RP No Fatal Error Report */
	u8 pcie_rp_no_fatal_error_report[28];
	/* 0x0803: PCIE RP Correctable Error Report */
	u8 pcie_rp_correctable_error_report[28];
	/* 0x081f: PCIE RP System Error On Fatal Error */
	u8 pcie_rp_system_error_on_fatal_error[28];
	/* 0x083b: PCIE RP System Error On Non Fatal Error */
	u8 pcie_rp_system_error_on_non_fatal_error[28];
	/* 0x0857: PCIE RP System Error On Correctable Error */
	u8 pcie_rp_system_error_on_correctable_error[28];
	/* 0x0873: PCIE RP Max Payload */
	u8 pcie_rp_max_payload[28];
	/* 0x088f: Touch Host Controller Port 0 Assignment */
	u8 thc_port0_assignment;
	/* 0x0890: Touch Host Controller Port 0 Interrupt Pin Mux */
	u32 thc_port0_interrupt_pin_muxing;
	/* 0x0894: Touch Host Controller Port 0 Wake On Touch */
	u8 thc_port0_wake_on_touch;
	/* 0x0895: Touch Host Controller Port 1 Assignment */
	u8 thc_port1_assignment;
	/*
	 * 0x0896: Touch Host Controller Port 1 Hid Over Spi Reset Sequencing
	 * Delay [ms]
	 */
	u16 thc_port1_hid_reset_sequencing_delay;
	/* 0x0898: Touch Host Controller Port 1 Interrupt Pin Mux */
	u32 thc_port1_interrupt_pin_muxing;
	/* 0x089c: Touch Host Controller Port 1 Wake On Touch */
	u8 thc_port1_wake_on_touch;
	/* 0x089d: PCIE RP Pcie Speed */
	u8 pcie_rp_pcie_speed[28];
	/* 0x08b9: PCIE RP Physical Slot Number */
	u8 pcie_rp_physical_slot_number[28];
	/* 0x08d5: PCIE RP Completion Timeout */
	u8 pcie_rp_completion_timeout[28];
	/* 0x08f1: PCIE RP Aspm */
	u8 pcie_rp_aspm[28];
	/* 0x090d: PCIE RP L1 Substates */
	u8 pcie_rp_l1_substates[28];
	/* 0x0929: PCIE RP L1 Low Substate */
	u8 pcie_rp_l1_low[28];
	/* 0x0945: PCIE RP Ltr Enable */
	u8 pcie_rp_ltr_enable[28];
	/* 0x0961: PCIE RP Ltr Config Lock */
	u8 pcie_rp_ltr_config_lock[28];
	/* 0x097d: PCIe override default settings for EQ */
	u8 pcie_eq_override_default;
	/* 0x097e: PCIe choose EQ method */
	u8 pcie_eq_method;
	/* 0x097f: PCIe choose EQ mode */
	u8 pcie_eq_mode;
	/* 0x0980: PCIe EQ local transmitter override */
	u8 pcie_eq_local_transmitter_override_enable;
	/* 0x0981: PCIe number of valid list entries */
	u8 pcie_eq_ph3_number_of_presets_or_coefficients;
	/* 0x0982: PCIe pre-cursor coefficient list */
	u8 pcie_eq_ph3_pre_cursor_list[10];
	/* 0x098c: PCIe post-cursor coefficient list */
	u8 pcie_eq_ph3_post_cursor_list[10];
	/* 0x0996: PCIe preset list */
	u8 pcie_eq_ph3_preset_list[11];
	u8 rsvd24;
	/*
	 * 0x09a2: Touch Host Controller Port 0 Hid Over Spi Reset Sequencing
	 * Delay [ms]
	 */
	u16 thc_port0_hid_reset_sequencing_delay;
	/* 0x09a4: PCIe EQ phase 1 downstream transmitter port preset */
	u32 pcie_eq_ph1_downstream_port_transmitter_preset;
	/* 0x09a8: PCIe EQ phase 1 upstream tranmitter port preset */
	u32 pcie_eq_ph1_upstream_port_transmitter_preset;
	/* 0x09ac: PCIe EQ phase 2 local transmitter override preset */
	u8 pcie_eq_ph2_local_transmitter_override_preset;
	/* 0x09ad: PCIE Enable Peer Memory Write */
	u8 pcie_enable_peer_memory_write[28];
	/* 0x09c9: PCIE Compliance Test Mode */
	u8 pcie_compliance_test_mode;
	/* 0x09ca: PCIE Rp Function Swap */
	u8 pcie_rp_function_swap;
	/* 0x09cb: Enable/Disable PEG GEN3 Static EQ Phase1 programming */
	u8 cpu_pcie_gen3_program_static_eq;
	/* 0x09cc: Enable/Disable GEN4 Static EQ Phase1 programming */
	u8 cpu_pcie_gen4_program_static_eq;
	/* 0x09cd: PCH Pm PME_B0_S5_DIS */
	u8 pch_pm_pme_b0_s5_dis;
	/* 0x09ce: PCIE IMR */
	u8 pcie_rp_imr_enabled;
	/* 0x09cf: PCIE IMR port number */
	u8 pcie_rp_imr_selection;
	/* 0x09d0: PCH Pm Wol Enable Override */
	u8 pch_pm_wol_enable_override;
	/* 0x09d1: PCH Pm Pcie Wake From DeepSx */
	u8 pch_pm_pcie_wake_from_deep_sx;
	/* 0x09d2: PCH Pm WoW lan Enable */
	u8 pch_pm_wo_wlan_enable;
	/* 0x09d3: PCH Pm WoW lan DeepSx Enable */
	u8 pch_pm_wo_wlan_deep_sx_enable;
	/* 0x09d4: PCH Pm Lan Wake From DeepSx */
	u8 pch_pm_lan_wake_from_deep_sx;
	/* 0x09d5: PCH Pm Deep Sx Pol */
	u8 pch_pm_deep_sx_pol;
	/* 0x09d6: PCH Pm Slp S3 Min Assert */
	u8 pch_pm_slp_s3_min_assert;
	/* 0x09d7: PCH Pm Slp S4 Min Assert */
	u8 pch_pm_slp_s4_min_assert;
	/* 0x09d8: PCH Pm Slp Sus Min Assert */
	u8 pch_pm_slp_sus_min_assert;
	/* 0x09d9: PCH Pm Slp A Min Assert */
	u8 pch_pm_slp_a_min_assert;
	/* 0x09da: USB Overcurrent Override for VISA */
	u8 pch_enable_dbc_obs;
	/* 0x09db: PCH Pm Slp Strch Sus Up */
	u8 pch_pm_slp_strch_sus_up;
	/* 0x09dc: PCH Pm Slp Lan Low Dc */
	u8 pch_pm_slp_lan_low_dc;
	/* 0x09dd: PCH Pm Pwr Btn Override Period */
	u8 pch_pm_pwr_btn_override_period;
	/* 0x09de: PCH Pm Disable Dsx Ac Present Pulldown */
	u8 pch_pm_disable_dsx_ac_present_pulldown;
	/* 0x09df: PCH Pm Disable Native Power Button */
	u8 pch_pm_disable_native_power_button;
	/* 0x09e0: PCH Pm ME_WAKE_STS */
	u8 pch_pm_me_wake_sts;
	/* 0x09e1: PCH Pm WOL_OVR_WK_STS */
	u8 pch_pm_wol_ovr_wk_sts;
	/* 0x09e2: PCH Pm Reset Power Cycle Duration */
	u8 pch_pm_pwr_cyc_dur;
	/* 0x09e3: PCH Pm Pcie Pll Ssc */
	u8 pch_pm_pcie_pll_ssc;
	/* 0x09e4: PCH Legacy IO Low Latency Enable */
	u8 pch_legacy_io_low_latency;
	/* 0x09e5: PCH Sata Pwr Opt Enable */
	u8 sata_pwr_opt_enable;
	/* 0x09e6: PCH Sata eSATA Speed Limit */
	u8 esata_speed_limit;
	/* 0x09e7: PCH Sata Speed Limit */
	u8 sata_speed_limit;
	/* 0x09e8: Enable SATA Port HotPlug */
	u8 sata_ports_hot_plug[8];
	/* 0x09f0: Enable SATA Port Interlock Sw */
	u8 sata_ports_interlock_sw[8];
	/* 0x09f8: Enable SATA Port External */
	u8 sata_ports_external[8];
	/* 0x0a00: Enable SATA Port SpinUp */
	u8 sata_ports_spin_up[8];
	/* 0x0a08: Enable SATA Port Solid State Drive */
	u8 sata_ports_solid_state_drive[8];
	/* 0x0a10: Enable SATA Port Enable Dito Config */
	u8 sata_ports_enable_dito_config[8];
	/* 0x0a18: Enable SATA Port DmVal */
	u8 sata_ports_dm_val[8];
	/* 0x0a20: Enable SATA Port DmVal */
	u16 sata_ports_dito_val[8];
	/* 0x0a30: Enable SATA Port ZpOdd */
	u8 sata_ports_zp_odd[8];
	/* 0x0a38: PCH Sata Rst Raid Alternate Id */
	u8 sata_rst_raid_device_id;
	/* 0x0a39: PCH Sata Rst Pcie Storage Remap enable */
	u8 sata_rst_pcie_enable[3];
	/* 0x0a3c: PCH Sata Rst Pcie Storage Port */
	u8 sata_rst_pcie_storage_port[3];
	/* 0x0a3f: PCH Sata Rst Pcie Device Reset Delay */
	u8 sata_rst_pcie_device_reset_delay[3];
	/* 0x0a42: UFS enable/disable */
	u8 ufs_enable[2];
	/* 0x0a44: IEH Mode */
	u8 ieh_mode;
	u8 rsvd25;
	/* 0x0a46: Thermal Throttling Custimized T0Level Value */
	u16 pch_t0_level;
	/* 0x0a48: Thermal Throttling Custimized T1Level Value */
	u16 pch_t1_level;
	/* 0x0a4a: Thermal Throttling Custimized T2Level Value */
	u16 pch_t2_level;
	/* 0x0a4c: Enable The Thermal Throttle */
	u8 pch_tt_enable;
	/* 0x0a4d: PMSync State 13 */
	u8 pch_tt_state13_enable;
	/* 0x0a4e: Thermal Throttle Lock */
	u8 pch_tt_lock;
	/* 0x0a4f: Thermal Throttling Suggested Setting */
	u8 tt_suggested_setting;
	/* 0x0a50: Enable PCH Cross Throttling */
	u8 tt_cross_throttling;
	/* 0x0a51: DMI Thermal Sensor Autonomous Width Enable */
	u8 pch_dmi_tsaw_en;
	/* 0x0a52: DMI Thermal Sensor Suggested Setting */
	u8 dmi_suggested_setting;
	/* 0x0a53: Thermal Sensor 0 Target Width */
	u8 dmi_ts0_tw;
	/* 0x0a54: Thermal Sensor 1 Target Width */
	u8 dmi_ts1_tw;
	/* 0x0a55: Thermal Sensor 2 Target Width */
	u8 dmi_ts2_tw;
	/* 0x0a56: Thermal Sensor 3 Target Width */
	u8 dmi_ts3_tw;
	/* 0x0a57: Port 0 T1 Multiplier */
	u8 sata_p0_t1_m;
	/* 0x0a58: Port 0 T2 Multiplier */
	u8 sata_p0_t2_m;
	/* 0x0a59: Port 0 T3 Multiplier */
	u8 sata_p0_t3_m;
	/* 0x0a5a: Port 0 Tdispatch */
	u8 sata_p0_t_disp;
	/* 0x0a5b: Port 1 T1 Multiplier */
	u8 sata_p1_t1_m;
	/* 0x0a5c: Port 1 T2 Multiplier */
	u8 sata_p1_t2_m;
	/* 0x0a5d: Port 1 T3 Multiplier */
	u8 sata_p1_t3_m;
	/* 0x0a5e: Port 1 Tdispatch */
	u8 sata_p1_t_disp;
	/* 0x0a5f: Port 0 Tinactive */
	u8 sata_p0_tinact;
	/* 0x0a60: Port 0 Alternate Fast Init Tdispatch */
	u8 sata_p0_t_disp_finit;
	/* 0x0a61: Port 1 Tinactive */
	u8 sata_p1_tinact;
	/* 0x0a62: Port 1 Alternate Fast Init Tdispatch */
	u8 sata_p1_t_disp_finit;
	/* 0x0a63: Sata Thermal Throttling Suggested Setting */
	u8 sata_thermal_suggested_setting;
	/* 0x0a64: Enable Memory Thermal Throttling */
	u8 pch_memory_throttling_enable;
	/* 0x0a65: Memory Thermal Throttling */
	u8 pch_memory_pmsync_enable[2];
	/* 0x0a67: Enable Memory Thermal Throttling */
	u8 pch_memory_c0_transmit_enable[2];
	/* 0x0a69: Enable Memory Thermal Throttling */
	u8 pch_memory_pin_selection[2];
	u8 rsvd26;
	/* 0x0a6c: Thermal Device Temperature */
	u16 pch_temperature_hot_level;
	/* 0x0a6e: USB2 Port Over Current Pin */
	u8 usb2_over_current_pin[16];
	/* 0x0a7e: USB3 Port Over Current Pin */
	u8 usb3_over_current_pin[10];
	/* 0x0a88: Enable xHCI LTR override */
	u8 pch_usb_ltr_override_enable;
	/* 0x0a89: Touch Host Controller Mode */
	u8 thc_mode[2];
	u8 rsvd27;
	/* 0x0a8c: xHCI High Idle Time LTR override */
	u32 pch_usb_ltr_high_idle_time_override;
	/* 0x0a90: xHCI Medium Idle Time LTR override */
	u32 pch_usb_ltr_medium_idle_time_override;
	/* 0x0a94: xHCI Low Idle Time LTR override */
	u32 pch_usb_ltr_low_idle_time_override;
	/* 0x0a98: Enable 8254 Static Clock Gating */
	u8 enable8254_clock_gating;
	/* 0x0a99: Enable 8254 Static Clock Gating On S3 */
	u8 enable8254_clock_gating_on_s3;
	/* 0x0a9a: Enable TCO timer. */
	u8 enable_tco_timer;
	/* 0x0a9b: Hybrid Storage Detection and Configuration Mode */
	u8 hybrid_storage_mode;
	/* 0x0a9c: CPU Root Port used for Hybrid Storage */
	u8 cpu_rootport_used_for_hybrid_storage;
	/*
	 * 0x0a9d: PCH Root Port used for Hybrid Storage when two lanes are
	 * connected to CPU
	 */
	u8 pch_rootport_used_for_cpu_attach;
	/* 0x0a9e: PCH GPE event handler */
	u8 pch_acpi_l6d_pme_handling;
	u8 rsvd28[1];
	/* 0x0aa0: BgpdtHash[4] */
	u64 bgpdt_hash[4];
	/* 0x0ac0: BiosGuardAttr */
	u32 bios_guard_attr;
	u8 rsvd29[4];
	/* 0x0ac8: BiosGuardModulePtr */
	u64 bios_guard_module_ptr;
	/* 0x0ad0: SendEcCmd */
	u64 send_ec_cmd;
	/* 0x0ad8: EcCmdProvisionEav */
	u8 ec_cmd_provision_eav;
	/* 0x0ad9: EcCmdLock */
	u8 ec_cmd_lock;
	/* 0x0ada: Skip Ssid Programming. */
	u8 si_skip_ssid_programming;
	u8 rsvd30;
	/* 0x0adc: Change Default SVID */
	u16 si_customized_svid;
	/* 0x0ade: Change Default SSID */
	u16 si_customized_ssid;
	/* 0x0ae0: SVID SDID table Poniter. */
	u32 si_ssid_table_ptr;
	/* 0x0ae4: Number of ssid table. */
	u16 si_number_of_ssid_table_entry;
	/* 0x0ae6: USB2 Port Reset Message Enable */
	u8 port_reset_message_enable[16];
	/* 0x0af6: SATA RST Interrupt Mode */
	u8 sata_rst_interrupt;
	/* 0x0af7: ME Unconfig on RTC clear */
	u8 me_unconfig_on_rtc_clear;
	/* 0x0af8: Enforce Enhanced Debug Mode */
	u8 enforce_e_debug_mode;
	/* 0x0af9: Enable PS_ON. */
	u8 ps_on_enable;
	/* 0x0afa: Pmc Cpu C10 Gate Pin Enable */
	u8 pmc_cpu_c10_gate_pin_enable;
	/* 0x0afb: Pch Dmi Aspm Ctrl */
	u8 pch_dmi_aspm_ctrl;
	/* 0x0afc: PchDmiCwbEnable */
	u8 pch_dmi_cwb_enable;
	/* 0x0afd: OS IDLE Mode Enable */
	u8 pmc_os_idle_enable;
	/* 0x0afe: S0ix Auto-Demotion */
	u8 pch_s0ix_auto_demotion;
	/* 0x0aff: Latch Events C10 Exit */
	u8 pch_pm_latch_events_c10_exit;
	/* 0x0b00: PMC ADR enable */
	u8 pmc_adr_en;
	/* 0x0b01: PMC ADR timer configuration enable */
	u8 pmc_adr_timer_en;
	/* 0x0b02: PMC ADR phase 1 timer value */
	u8 pmc_adr_timer1_val;
	/* 0x0b03: PMC ADR phase 1 timer multiplier value */
	u8 pmc_adr_multiplier1_val;
	/* 0x0b04: PMC ADR host reset partition enable */
	u8 pmc_adr_host_partition_reset;
	/* 0x0b05: PMC ADR source select override enable */
	u8 pmc_adr_src_override;
	u8 rsvd31[2];
	/* 0x0b08: PMC ADR source selection */
	u32 pmc_adr_src_sel;
	/* 0x0b0c: PCIE Eq Ph3 Lane Param Cm */
	u8 cpu_pcie_eq_ph3_lane_param_cm[32];
	/* 0x0b2c: PCIE Eq Ph3 Lane Param Cp */
	u8 cpu_pcie_eq_ph3_lane_param_cp[32];
	/* 0x0b4c: Gen3 Root port preset values per lane */
	u8 cpu_pcie_gen3_root_port_preset[20];
	/* 0x0b60: Pcie Gen4 Root port preset values per lane */
	u8 cpu_pcie_gen4_root_port_preset[20];
	/* 0x0b74: Pcie Gen3 End port preset values per lane */
	u8 cpu_pcie_gen3_end_point_preset[20];
	/* 0x0b88: Pcie Gen4 End port preset values per lane */
	u8 cpu_pcie_gen4_end_point_preset[20];
	/* 0x0b9c: Pcie Gen3 End port Hint values per lane */
	u8 cpu_pcie_gen3_end_point_hint[20];
	/* 0x0bb0: Pcie Gen4 End port Hint values per lane */
	u8 cpu_pcie_gen4_end_point_hint[20];
	/* 0x0bc4: CPU PCIe Fia Programming */
	u8 cpu_pcie_fia_programming;
	/* 0x0bc5: CPU PCIe RootPort Clock Gating */
	u8 cpu_pcie_clock_gating[4];
	/* 0x0bc9: CPU PCIe RootPort Power Gating */
	u8 cpu_pcie_power_gating[4];
	/* 0x0bcd: PCIE Compliance Test Mode */
	u8 cpu_pcie_compliance_test_mode;
	/* 0x0bce: PCIE Enable Peer Memory Write */
	u8 cpu_pcie_enable_peer_memory_write;
	/* 0x0bcf: PCIE Rp Function Swap */
	u8 cpu_pcie_rp_function_swap;
	/* 0x0bd0: PCI Express Slot Selection */
	u8 cpu_pcie_slot_selection;
	u8 rsvd32[3];
	/* 0x0bd4: CPU PCIE device override table pointer */
	u32 cpu_pcie_device_override_table_ptr;
	/* 0x0bd8: Enable PCIE RP HotPlug */
	u8 cpu_pcie_rp_hot_plug[4];
	/* 0x0bdc: Enable PCIE RP Pm Sci */
	u8 cpu_pcie_rp_pm_sci[4];
	/* 0x0be0: Enable PCIE RP Transmitter Half Swing */
	u8 cpu_pcie_rp_transmitter_half_swing[4];
	/* 0x0be4: PCIE RP Access Control Services Extended Capability */
	u8 cpu_pcie_rp_acs_enabled[4];
	/* 0x0be8: PCIE RP Clock Power Management */
	u8 cpu_pcie_rp_enable_cpm[4];
	/* 0x0bec: PCIE RP Advanced Error Report */
	u8 cpu_pcie_rp_advanced_error_reporting[4];
	/* 0x0bf0: PCIE RP Unsupported Request Report */
	u8 cpu_pcie_rp_unsupported_request_report[4];
	/* 0x0bf4: PCIE RP Fatal Error Report */
	u8 cpu_pcie_rp_fatal_error_report[4];
	/* 0x0bf8: PCIE RP No Fatal Error Report */
	u8 cpu_pcie_rp_no_fatal_error_report[4];
	/* 0x0bfc: PCIE RP Correctable Error Report */
	u8 cpu_pcie_rp_correctable_error_report[4];
	/* 0x0c00: PCIE RP System Error On Fatal Error */
	u8 cpu_pcie_rp_system_error_on_fatal_error[4];
	/* 0x0c04: PCIE RP System Error On Non Fatal Error */
	u8 cpu_pcie_rp_system_error_on_non_fatal_error[4];
	/* 0x0c08: PCIE RP System Error On Correctable Error */
	u8 cpu_pcie_rp_system_error_on_correctable_error[4];
	/* 0x0c0c: PCIE RP Max Payload */
	u8 cpu_pcie_rp_max_payload[4];
	/* 0x0c10: DPC for PCIE RP Mask */
	u8 cpu_pcie_rp_dpc_enabled[4];
	/* 0x0c14: DPC Extensions PCIE RP Mask */
	u8 cpu_pcie_rp_dpc_extensions_enabled[4];
	/* 0x0c18: CPU PCIe root port connection type */
	u8 cpu_pcie_rp_slot_implemented[4];
	/* 0x0c1c: PCIE RP Gen3 Equalization Phase Method */
	u8 cpu_pcie_rp_gen3_eq_ph3_method[4];
	/* 0x0c20: PCIE RP Gen4 Equalization Phase Method */
	u8 cpu_pcie_rp_gen4_eq_ph3_method[4];
	/* 0x0c24: PCIE RP Physical Slot Number */
	u8 cpu_pcie_rp_physical_slot_number[4];
	/* 0x0c28: PCIE RP Aspm */
	u8 cpu_pcie_rp_aspm[4];
	/* 0x0c2c: PCIE RP L1 Substates */
	u8 cpu_pcie_rp_l1_substates[4];
	/* 0x0c30: PCIE RP Ltr Enable */
	u8 cpu_pcie_rp_ltr_enable[4];
	/* 0x0c34: PCIE RP Ltr Config Lock */
	u8 cpu_pcie_rp_ltr_config_lock[4];
	/* 0x0c38: PTM for PCIE RP Mask */
	u8 cpu_pcie_rp_ptm_enabled[4];
	/* 0x0c3c: PCIE RP Detect Timeout Ms */
	u16 cpu_pcie_rp_detect_timeout_ms[4];
	/* 0x0c44: Multi-VC for PCIE RP Mask */
	u8 cpu_pcie_rp_multi_vc_enabled[4];
	/*
	 * 0x0c48: Enable the write to USB 3.0 TX Output Unique Transition Bit
	 * Mode for rate 3
	 */
	u8 usb3_hsio_tx_rate3_uniq_tran_enable[10];
	/* 0x0c52: USB 3.0 TX Output Unique Transition Bit Scale for rate 3 */
	u8 usb3_hsio_tx_rate3_uniq_tran[10];
	/*
	 * 0x0c5c: Enable the write to USB 3.0 TX Output Unique Transition Bit
	 * Mode for rate 2
	 */
	u8 usb3_hsio_tx_rate2_uniq_tran_enable[10];
	/* 0x0c66: USB 3.0 TX Output Unique Transition Bit Scale for rate 2 */
	u8 usb3_hsio_tx_rate2_uniq_tran[10];
	/*
	 * 0x0c70: Enable the write to USB 3.0 TX Output Unique Transition Bit
	 * Mode for rate 1
	 */
	u8 usb3_hsio_tx_rate1_uniq_tran_enable[10];
	/* 0x0c7a: USB 3.0 TX Output Unique Transition Bit Scale for rate 1 */
	u8 usb3_hsio_tx_rate1_uniq_tran[10];
	/*
	 * 0x0c84: Enable the write to USB 3.0 TX Output Unique Transition Bit
	 * Mode for rate 0
	 */
	u8 usb3_hsio_tx_rate0_uniq_tran_enable[10];
	/* 0x0c8e: USB 3.0 TX Output Unique Transition Bit Scale for rate 0 */
	u8 usb3_hsio_tx_rate0_uniq_tran[10];
	/* 0x0c98: Skip PAM register lock */
	u8 skip_pam_lock;
	/* 0x0c99: EDRAM Test Mode */
	u8 edram_test_mode;
	/* 0x0c9a: Enable/Disable IGFX RenderStandby */
	u8 render_standby;
	/* 0x0c9b: Enable/Disable IGFX PmSupport */
	u8 pm_support;
	/* 0x0c9c: Enable/Disable CdynmaxClamp */
	u8 cdynmax_clamp_enable;
	/* 0x0c9d: GT Frequency Limit */
	u8 gt_freq_max;
	/* 0x0c9e: Disable Turbo GT */
	u8 disable_turbo_gt;
	/* 0x0c9f: Enable/Disable CdClock Init */
	u8 skip_cd_clock_init;
	/*
	 * 0x0ca0: Enable RC1p frequency request to PMA (provided all other
	 * conditions are met)
	 */
	u8 rc1p_freq_enable;
	/* 0x0ca1: Enable TSN Multi-VC */
	u8 pch_tsn_multi_vc_enable;
	u8 rsvd33[2];
	/* 0x0ca4: LogoPixelHeight Address */
	u32 logo_pixel_height;
	/* 0x0ca8: LogoPixelWidth Address */
	u32 logo_pixel_width;
	/* 0x0cac: ITbt Usb4CmMode value */
	u8 usb4_cm_mode;
	/* 0x0cad: PCIE Resizable BAR Support */
	u8 cpu_pcie_resizable_bar_support;
	/* 0x0cae: SaPostMemTestRsvd */
	u8 sa_post_mem_test_rsvd[3];
	/* 0x0cb1: RSR feature */
	u8 enable_rsr;
	/* 0x0cb2: ReservedCpuPostMem1 */
	u8 reserved_cpu_post_mem1[4];
	/* 0x0cb6: Enable or Disable HWP */
	u8 hwp;
	/* 0x0cb7: Hardware Duty Cycle Control */
	u8 hdc_control;
	/* 0x0cb8: Package Long duration turbo mode time */
	u8 power_limit1_time;
	/* 0x0cb9: Short Duration Turbo Mode */
	u8 power_limit2;
	/* 0x0cba: Turbo settings Lock */
	u8 turbo_power_limit_lock;
	/* 0x0cbb: Package PL3 time window */
	u8 power_limit3_time;
	/* 0x0cbc: Package PL3 Duty Cycle */
	u8 power_limit3_duty_cycle;
	/* 0x0cbd: Package PL3 Lock */
	u8 power_limit3_lock;
	/* 0x0cbe: Package PL4 Lock */
	u8 power_limit4_lock;
	/* 0x0cbf: TCC Activation Offset */
	u8 tcc_activation_offset;
	/* 0x0cc0: Tcc Offset Clamp Enable/Disable */
	u8 tcc_offset_clamp;
	/* 0x0cc1: Tcc Offset Lock */
	u8 tcc_offset_lock;
	/* 0x0cc2: Custom Ratio State Entries */
	u8 number_of_entries;
	/* 0x0cc3: Custom Short term Power Limit time window */
	u8 custom1_power_limit1_time;
	/* 0x0cc4: Custom Turbo Activation Ratio */
	u8 custom1_turbo_activation_ratio;
	/* 0x0cc5: Custom Config Tdp Control */
	u8 custom1_config_tdp_control;
	/* 0x0cc6: Custom Short term Power Limit time window */
	u8 custom2_power_limit1_time;
	/* 0x0cc7: Custom Turbo Activation Ratio */
	u8 custom2_turbo_activation_ratio;
	/* 0x0cc8: Custom Config Tdp Control */
	u8 custom2_config_tdp_control;
	/* 0x0cc9: Custom Short term Power Limit time window */
	u8 custom3_power_limit1_time;
	/* 0x0cca: Custom Turbo Activation Ratio */
	u8 custom3_turbo_activation_ratio;
	/* 0x0ccb: Custom Config Tdp Control */
	u8 custom3_config_tdp_control;
	/* 0x0ccc: ConfigTdp mode settings Lock */
	u8 config_tdp_lock;
	/* 0x0ccd: Load Configurable TDP SSDT */
	u8 config_tdp_bios;
	/* 0x0cce: PL1 Enable value */
	u8 psys_power_limit1;
	/* 0x0ccf: PL1 timewindow */
	u8 psys_power_limit1_time;
	/* 0x0cd0: PL2 Enable Value */
	u8 psys_power_limit2;
	/* 0x0cd1: Enable or Disable MLC Streamer Prefetcher */
	u8 mlc_streamer_prefetcher;
	/* 0x0cd2: Enable or Disable MLC Spatial Prefetcher */
	u8 mlc_spatial_prefetcher;
	/* 0x0cd3: Enable or Disable Monitor /MWAIT instructions */
	u8 monitor_mwait_enable;
	/*
	 * 0x0cd4: Enable or Disable initialization of machine check registers
	 */
	u8 machine_check_enable;
	/* 0x0cd5: AP Idle Manner of waiting for SIPI */
	u8 ap_idle_manner;
	/* 0x0cd6: Control on Processor Trace output scheme */
	u8 processor_trace_output_scheme;
	/* 0x0cd7: Enable or Disable Processor Trace feature */
	u8 processor_trace_enable;
	/* 0x0cd8: Enable or Disable Intel SpeedStep Technology */
	u8 eist;
	/* 0x0cd9: Enable or Disable Energy Efficient P-state */
	u8 energy_efficient_p_state;
	/* 0x0cda: Enable or Disable Energy Efficient Turbo */
	u8 energy_efficient_turbo;
	/* 0x0cdb: Enable or Disable T states */
	u8 t_states;
	/* 0x0cdc: Enable or Disable Bi-Directional PROCHOT# */
	u8 bi_proc_hot;
	/* 0x0cdd: Enable or Disable PROCHOT# signal being driven externally */
	u8 disable_proc_hot_out;
	/* 0x0cde: Enable or Disable PROCHOT# Response */
	u8 proc_hot_response;
	/* 0x0cdf: Enable or Disable VR Thermal Alert */
	u8 disable_vr_thermal_alert;
	/* 0x0ce0: Enable or Disable Thermal Reporting */
	u8 enable_all_thermal_functions;
	/* 0x0ce1: Enable or Disable Thermal Monitor */
	u8 thermal_monitor;
	/* 0x0ce2: Enable or Disable CPU power states (C-states) */
	u8 cx;
	/* 0x0ce3: Configure C-State Configuration Lock */
	u8 pmg_cst_cfg_ctrl_lock;
	/* 0x0ce4: Enable or Disable Enhanced C-states */
	u8 c1e;
	/* 0x0ce5: Enable or Disable Package Cstate Demotion */
	u8 pkg_c_state_demotion;
	/* 0x0ce6: Enable or Disable Package Cstate UnDemotion */
	u8 pkg_c_state_un_demotion;
	/* 0x0ce7: Enable or Disable CState-Pre wake */
	u8 c_state_pre_wake;
	/* 0x0ce8: Enable or Disable TimedMwait Support. */
	u8 timed_mwait;
	/* 0x0ce9: Enable or Disable IO to MWAIT redirection */
	u8 cst_cfg_ctr_io_mwait_redirection;
	/* 0x0cea: Set the Max Pkg Cstate */
	u8 pkg_c_state_limit;
	/* 0x0ceb: TimeUnit for C-State Latency Control0 */
	u8 cstate_latency_control0_time_unit;
	/* 0x0cec: TimeUnit for C-State Latency Control1 */
	u8 cstate_latency_control1_time_unit;
	/* 0x0ced: TimeUnit for C-State Latency Control2 */
	u8 cstate_latency_control2_time_unit;
	/* 0x0cee: TimeUnit for C-State Latency Control3 */
	u8 cstate_latency_control3_time_unit;
	/* 0x0cef: TimeUnit for C-State Latency Control4 */
	u8 cstate_latency_control4_time_unit;
	/* 0x0cf0: TimeUnit for C-State Latency Control5 */
	u8 cstate_latency_control5_time_unit;
	/* 0x0cf1: Interrupt Redirection Mode Select */
	u8 ppm_irm_setting;
	/* 0x0cf2: Lock prochot configuration */
	u8 proc_hot_lock;
	/* 0x0cf3: Configuration for boot TDP selection */
	u8 config_tdp_level;
	/* 0x0cf4: Max P-State Ratio */
	u8 max_ratio;
	/* 0x0cf5: P-state ratios for custom P-state table */
	u8 state_ratio[40];
	/* 0x0d1d: P-state ratios for max 16 version of custom P-state table */
	u8 state_ratio_max16[16];
	u8 rsvd34;
	/* 0x0d2e: Platform Power Pmax */
	u16 psys_pmax;
	/* 0x0d30: Interrupt Response Time Limit of C-State LatencyContol1 */
	u16 cstate_latency_control1_irtl;
	/* 0x0d32: Interrupt Response Time Limit of C-State LatencyContol2 */
	u16 cstate_latency_control2_irtl;
	/* 0x0d34: Interrupt Response Time Limit of C-State LatencyContol3 */
	u16 cstate_latency_control3_irtl;
	/* 0x0d36: Interrupt Response Time Limit of C-State LatencyContol4 */
	u16 cstate_latency_control4_irtl;
	/* 0x0d38: Interrupt Response Time Limit of C-State LatencyContol5 */
	u16 cstate_latency_control5_irtl;
	u8 rsvd35[2];
	/* 0x0d3c: Package Long duration turbo mode power limit */
	u32 power_limit1;
	/* 0x0d40: Package Short duration turbo mode power limit */
	u32 power_limit2_power;
	/* 0x0d44: Package PL3 power limit */
	u32 power_limit3;
	/* 0x0d48: Package PL4 power limit */
	u32 power_limit4;
	/* 0x0d4c: Tcc Offset Time Window for RATL */
	u32 tcc_offset_time_window_for_ratl;
	/* 0x0d50: Short term Power Limit value for custom cTDP level 1 */
	u32 custom1_power_limit1;
	/* 0x0d54: Long term Power Limit value for custom cTDP level 1 */
	u32 custom1_power_limit2;
	/* 0x0d58: Short term Power Limit value for custom cTDP level 2 */
	u32 custom2_power_limit1;
	/* 0x0d5c: Long term Power Limit value for custom cTDP level 2 */
	u32 custom2_power_limit2;
	/* 0x0d60: Short term Power Limit value for custom cTDP level 3 */
	u32 custom3_power_limit1;
	/* 0x0d64: Long term Power Limit value for custom cTDP level 3 */
	u32 custom3_power_limit2;
	/* 0x0d68: Platform PL1 power */
	u32 psys_power_limit1_power;
	/* 0x0d6c: Platform PL2 power */
	u32 psys_power_limit2_power;
	/* 0x0d70: Race To Halt */
	u8 race_to_halt;
	/* 0x0d71: Set Three Strike Counter Disable */
	u8 three_strike_counter_disable;
	/* 0x0d72: Set HW P-State Interrupts Enabled for MISC_PWR_MGMT */
	u8 hwp_interrupt_control;
	/* 0x0d73: ReservedCpuPostMem2 */
	u8 reserved_cpu_post_mem2[4];
	/* 0x0d77: Intel Turbo Boost Max Technology 3.0 */
	u8 enable_itbm;
	/* 0x0d78: Enable or Disable C1 Cstate Demotion */
	u8 c1_state_auto_demotion;
	/* 0x0d79: Enable or Disable C1 Cstate UnDemotion */
	u8 c1_state_un_demotion;
	/* 0x0d7a: Minimum Ring ratio limit override */
	u8 min_ring_ratio_limit;
	/* 0x0d7b: Maximum Ring ratio limit override */
	u8 max_ring_ratio_limit;
	/* 0x0d7c: Enable or Disable Per Core P State OS control */
	u8 enable_per_core_p_state;
	/*
	 * 0x0d7d: Enable or Disable HwP Autonomous Per Core P State OS control
	 */
	u8 enable_hwp_auto_per_core_pstate;
	/* 0x0d7e: Enable or Disable HwP Autonomous EPP Grouping */
	u8 enable_hwp_auto_epp_grouping;
	/* 0x0d7f: Enable or Disable EPB override over PECI */
	u8 enable_epb_peci_override;
	/* 0x0d80: Enable or Disable Fast MSR for IA32_HWP_REQUEST */
	u8 enable_fast_msr_hwp_req;
	/* 0x0d81: Enable Configurable TDP */
	u8 apply_config_tdp;
	/* 0x0d82: Misc Power Management MSR Lock */
	u8 hwp_lock;
	/* 0x0d83: Dual Tau Boost */
	u8 dual_tau_boost;
	/* 0x0d84: Is Battery Present */
	u8 step_down_mode;
	/* 0x0d85: Platform ATX Telemetry Unit */
	u8 platform_atx_telemetry_unit;
	/* 0x0d86: ProcHot Demotion Algorithm configuration */
	u8 proc_hot_demotion;
	/* 0x0d87: Turbo Configuration */
	u8 turbo_configuration;
	/* 0x0d88: Enable or Disable HwP Scalability Tracking */
	u8 enable_hwp_scalability_tracking;
	/* 0x0d89: ReservedCpuPostMemTest */
	u8 reserved_cpu_post_mem_test[11];
	u8 security_post_mem_rsvd[16];
	/* 0x0da4: End of Post message */
	u8 end_of_post_message;
	/* 0x0da5: D0I3 Setting for HECI Disable */
	u8 disable_d0_i3_setting_for_heci;
	/* 0x0da6: Mctp Broadcast Cycle */
	u8 mctp_broadcast_cycle;
	/* 0x0da7: Enable LOCKDOWN SMI */
	u8 pch_lock_down_global_smi;
	/* 0x0da8: Enable LOCKDOWN BIOS Interface */
	u8 pch_lock_down_bios_interface;
	/* 0x0da9: Unlock all GPIO pads */
	u8 pch_unlock_gpio_pads;
	/* 0x0daa: PCH Unlock SideBand access */
	u8 pch_sb_access_unlock;
	u8 rsvd36[1];
	/* 0x0dac: PCIE RP Ltr Max Snoop Latency */
	u16 pcie_rp_ltr_max_snoop_latency[28];
	/* 0x0de4: PCIE RP Ltr Max No Snoop Latency */
	u16 pcie_rp_ltr_max_no_snoop_latency[28];
	/* 0x0e1c: PCIE RP Snoop Latency Override Mode */
	u8 pcie_rp_snoop_latency_override_mode[28];
	/* 0x0e38: PCIE RP Snoop Latency Override Multiplier */
	u8 pcie_rp_snoop_latency_override_multiplier[28];
	/* 0x0e54: PCIE RP Snoop Latency Override Value */
	u16 pcie_rp_snoop_latency_override_value[28];
	/* 0x0e8c: PCIE RP Non Snoop Latency Override Mode */
	u8 pcie_rp_non_snoop_latency_override_mode[28];
	/* 0x0ea8: PCIE RP Non Snoop Latency Override Multiplier */
	u8 pcie_rp_non_snoop_latency_override_multiplier[28];
	/* 0x0ec4: PCIE RP Non Snoop Latency Override Value */
	u16 pcie_rp_non_snoop_latency_override_value[28];
	/* 0x0efc: PCIE RP Slot Power Limit Scale */
	u8 pcie_rp_slot_power_limit_scale[28];
	/* 0x0f18: PCIE RP Slot Power Limit Value */
	u16 pcie_rp_slot_power_limit_value[28];
	/* 0x0f50: PCIE RP Enable Port8xh Decode */
	u8 pcie_enable_port8xh_decode;
	/* 0x0f51: PCIE Port8xh Decode Port Index */
	u8 pch_pcie_port8xh_decode_port_index;
	/* 0x0f52: PCH Energy Reporting */
	u8 pch_pm_disable_energy_report;
	/* 0x0f53: PCH Sata Test Mode */
	u8 sata_test_mode;
	/* 0x0f54: PCH USB OverCurrent mapping lock enable */
	u8 pch_xhci_oc_lock;
	/* 0x0f55: Low Power Mode Enable/Disable config mask */
	u8 pmc_lpm_s0ix_sub_state_enable_mask;
	/* 0x0f56: PCIE RP Ltr Max Snoop Latency */
	u16 cpu_pcie_rp_ltr_max_snoop_latency[4];
	/* 0x0f5e: PCIE RP Ltr Max No Snoop Latency */
	u16 cpu_pcie_rp_ltr_max_no_snoop_latency[4];
	/* 0x0f66: PCIE RP Snoop Latency Override Mode */
	u8 cpu_pcie_rp_snoop_latency_override_mode[4];
	/* 0x0f6a: PCIE RP Snoop Latency Override Multiplier */
	u8 cpu_pcie_rp_snoop_latency_override_multiplier[4];
	/* 0x0f6e: PCIE RP Snoop Latency Override Value */
	u16 cpu_pcie_rp_snoop_latency_override_value[4];
	/* 0x0f76: PCIE RP Non Snoop Latency Override Mode */
	u8 cpu_pcie_rp_non_snoop_latency_override_mode[4];
	/* 0x0f7a: PCIE RP Non Snoop Latency Override Multiplier */
	u8 cpu_pcie_rp_non_snoop_latency_override_multiplier[4];
	/* 0x0f7e: PCIE RP Non Snoop Latency Override Value */
	u16 cpu_pcie_rp_non_snoop_latency_override_value[4];
	/* 0x0f86: PCIE RP Upstream Port Transmiter Preset */
	u8 cpu_pcie_rp_gen3_uptp[4];
	/* 0x0f8a: PCIE RP Downstream Port Transmiter Preset */
	u8 cpu_pcie_rp_gen3_dptp[4];
	/* 0x0f8e: PCIE RP Upstream Port Transmiter Preset */
	u8 cpu_pcie_rp_gen4_uptp[4];
	/* 0x0f92: PCIE RP Downstream Port Transmiter Preset */
	u8 cpu_pcie_rp_gen4_dptp[4];
	/* 0x0f96: PCIE RP Upstream Port Transmiter Preset */
	u8 cpu_pcie_rp_gen5_uptp[4];
	/* 0x0f9a: PCIE RP Downstream Port Transmiter Preset */
	u8 cpu_pcie_rp_gen5_dptp[4];
	/* 0x0f9e: Type C Port x Convert to TypeA */
	u8 enable_tcss_cov_type_a[4];
	/* 0x0fa2: PCH xhci port x for Type C Port x mapping */
	u8 mapping_pch_xhci_usb_a[4];
	/* 0x0fa6: FOMS Control Policy */
	u8 cpu_pcie_foms_cp[4];
	/* 0x0faa: PMC C10 dynamic threshold dajustment enable */
	u8 pmc_c10_dynamic_threshold_adjustment;
	/* 0x0fab: P2P mode for PCIE RP */
	u8 cpu_pcie_rp_peer_to_peer_mode[4];
	/* 0x0faf: Turbo Ratio Limit Ratio array */
	u8 turbo_ratio_limit_ratio[8];
	/* 0x0fb7: Turbo Ratio Limit Num Core array */
	u8 turbo_ratio_limit_num_core[8];
	/* 0x0fbf: ATOM Turbo Ratio Limit Ratio array */
	u8 atom_turbo_ratio_limit_ratio[8];
	/* 0x0fc7: ATOM Turbo Ratio Limit Num Core array */
	u8 atom_turbo_ratio_limit_num_core[8];
	u8 rsvd37;
	/* 0x0fd0: FspEventHandler */
	u32 fsp_event_handler;
	/* 0x0fd4: Enable VMD Global Mapping */
	u8 vmd_global_mapping;
	/* 0x0fd5: CPU PCIE Port0 Link Disable */
	u8 cpu_pcie_func0_link_disable[4];
	/* 0x0fd9: Skip VccIn Configuration */
	u8 pmc_skip_vcc_in_config;
	/* 0x0fda: CSE Data Resilience Support */
	u8 cse_data_resilience;
	u8 rsvd38;
	/* 0x0fdc: HorizontalResolution for PEI Logo */
	u32 horizontal_resolution;
	/* 0x0fe0: VerticalResolution for PEI Logo */
	u32 vertical_resolution;
	/* 0x0fe4: Touch Host Controller Active Ltr */
	u32 thc_active_ltr[2];
	/* 0x0fec: Touch Host Controller Idle Ltr */
	u32 thc_idle_ltr[2];
	/* 0x0ff4: Touch Host Controller Hid Over Spi ResetPad */
	u32 thc_hid_reset_pad[2];
	/* 0x0ffc: Touch Host Controller Hid Over Spi ResetPad Trigger */
	u32 thc_hid_reset_pad_trigger[2];
	/* 0x1004: Touch Host Controller Hid Over Spi Connection Speed */
	u32 thc_hid_connection_speed[2];
	/* 0x100c: Touch Host Controller Hid Over Spi Limit PacketSize */
	u32 thc_limit_packet_size[2];
	/* 0x1014: Touch Host Controller Hid Over Spi Limit PacketSize */
	u32 thc_performance_limitation[2];
	/*
	 * 0x101c: Touch Host Controller Hid Over Spi Input Report Header
	 * Address
	 */
	u32 thc_hid_input_report_header_address[2];
	/*
	 * 0x1024: Touch Host Controller Hid Over Spi Input Report Body Address
	 */
	u32 thc_hid_input_report_body_address[2];
	/* 0x102c: Touch Host Controller Hid Over Spi Output Report Address */
	u32 thc_hid_output_report_address[2];
	/* 0x1034: Touch Host Controller Hid Over Spi Read Opcode */
	u32 thc_hid_read_opcode[2];
	/* 0x103c: Touch Host Controller Hid Over Spi Write Opcode */
	u32 thc_hid_write_opcode[2];
	/* 0x1044: Touch Host Controller Hid Over Spi Flags */
	u32 thc_hid_flags[2];
	/* 0x104c: Force LTR Override */
	u8 cpu_pcie_rp_test_force_ltr_override[4];
	/* 0x1050: MemoryBuffer */
	u64 memory_buffer;
	/* 0x1058: MemorySize */
	u32 memory_size;
	u8 rsvd40[2];
	u8 reserved_fsps_upd[2]; /* size 0x1020, offsets 0x40..0x1060 */
};

/**
 * struct fsps_upd - complete FSP-S UPD region, passed to FspSiliconInit()
 */
struct __packed fsps_upd {
	struct fsp_upd_header header;
	struct fsps_arch_upd arch;
	struct fsp_s_config config;
	u8 reserved[6];
	u16 terminator;
};

static_assert(sizeof(struct fsp_s_config) == 0x1020,
	      "FSP_S_CONFIG must match the FSP's layout");
static_assert(sizeof(struct fsps_upd) == 0x1068,
	      "FSPS_UPD must match the FSP's layout");

#endif
