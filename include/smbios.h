/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2015, Bin Meng <bmeng.cn@gmail.com>
 *
 * Adapted from coreboot src/include/smbios.h
 */

#ifndef _SMBIOS_H_
#define _SMBIOS_H_

#include <linux/types.h>
#include <smbios_def.h>

/* SMBIOS spec version implemented */
#define SMBIOS_MAJOR_VER	3
#define SMBIOS_MINOR_VER	7

/**
 * struct smbios_info - Information about SMBIOS tables
 *
 * @table: Pointer to the first table
 * @count: Number of tables
 * @max_size: Maximum size of the tables pointed to by struct_table_address
 * @version: table version in the form 0xMMmmrr, where MM is the major version
 * number (2 or 3), mm is the minor version number and rr is * the revision
 * (always 0 for major-version 2)
 */
struct smbios_info {
	struct smbios_header *table;
	int count;
	int max_size;
	int version;
};

enum {
	SMBIOS_STR_MAX	= 64,	/* Maximum length allowed for a string */
};

/* SMBIOS structure types */
enum {
	SMBIOS_BIOS_INFORMATION = 0,
	SMBIOS_SYSTEM_INFORMATION = 1,
	SMBIOS_BOARD_INFORMATION = 2,
	SMBIOS_SYSTEM_ENCLOSURE = 3,
	SMBIOS_PROCESSOR_INFORMATION = 4,
	SMBIOS_CACHE_INFORMATION = 7,
	SMBIOS_SYSTEM_SLOTS = 9,
	SMBIOS_PHYS_MEMORY_ARRAY = 16,
	SMBIOS_MEMORY_DEVICE = 17,
	SMBIOS_MEMORY_ARRAY_MAPPED_ADDRESS = 19,
	SMBIOS_SYSTEM_BOOT_INFORMATION = 32,
	SMBIOS_END_OF_TABLE = 127
};

#define SMBIOS_INTERMEDIATE_OFFSET	16
#define SMBIOS_STRUCT_EOS_BYTES		2

struct str_lookup_table {
	u16 idx;
	const char *str;
};

/**
 * struct smbios_entry - SMBIOS 2.1 (32-bit) Entry Point structure
 *
 * This structure represents the SMBIOS Entry Point as defined in the
 * SMBIOS specification version 2.1+. It serves as the starting point
 * for locating SMBIOS tables in system memory.
 *
 * @anchor: Entry Point Structure anchor string "_SM_"
 * @checksum: Checksum of the Entry Point Structure
 * @length: Length of the Entry Point Structure, formatted area
 * @major_ver: Major version of the SMBIOS specification implemented
 * @minor_ver: Minor version of the SMBIOS specification implemented
 * @max_struct_size: Size of the largest SMBIOS structure
 * @entry_point_rev: Entry Point Structure Revision implemented
 * @formatted_area: Reserved formatted area, set to 0
 * @intermediate_anchor: Intermediate Entry Point Structure anchor "_DMI_"
 * @intermediate_checksum: Checksum of intermediate Entry Point Structure
 * @struct_table_length: Total length in bytes of SMBIOS Structure Table
 * @struct_table_address: 32-bit physical starting address of table
 * @struct_count: Total number of SMBIOS structures present in the table
 * @bcd_rev: BCD revision of the SMBIOS specification (e.g. 0x21 for v2.1)
 */
struct __packed smbios_entry {
	u8 anchor[4];
	u8 checksum;
	u8 length;
	u8 major_ver;
	u8 minor_ver;
	u16 max_struct_size;
	u8 entry_point_rev;
	u8 formatted_area[5];
	u8 intermediate_anchor[5];
	u8 intermediate_checksum;
	u16 struct_table_length;
	u32 struct_table_address;
	u16 struct_count;
	u8 bcd_rev;
};

/**
 * struct smbios3_entry - SMBIOS 3.0 (64-bit) Entry Point structure
 *
 * This structure represents the SMBIOS 3.0+ Entry Point as defined in the
 * SMBIOS specification version 3.0+. It provides 64-bit addressing support
 * and serves as the starting point for locating SMBIOS tables in memory.
 *
 * @anchor: Entry Point Structure anchor string "_SM3_"
 * @checksum: Checksum of the Entry Point Structure
 * @length: Length of the Entry Point Structure
 * @major_ver: Major version of the SMBIOS specification implemented
 * @minor_ver: Minor version of the SMBIOS specification implemented
 * @doc_rev: Revision of the SMBIOS specification document
 * @entry_point_rev: Revision of the Entry Point Structure
 * @reserved: Reserved field, must be 0
 * @table_maximum_size: Maximum size of SMBIOS Structure Table
 * @struct_table_address: 64-bit physical starting address of table
 */
struct __packed smbios3_entry {
	u8 anchor[5];
	u8 checksum;
	u8 length;
	u8 major_ver;
	u8 minor_ver;
	u8 doc_rev;
	u8 entry_point_rev;
	u8 reserved;
	u32 table_maximum_size;
	u64 struct_table_address;
};

/**
 * struct smbios_header - Common header for all SMBIOS structures
 *
 * This header appears at the beginning of every SMBIOS structure and
 * provides basic identification and size information for the structure.
 *
 * @type: SMBIOS structure type (0-127 for standard types)
 * @length: Length of the formatted portion of the structure in bytes
 * @handle: Unique 16-bit identifier for this structure instance
 */
struct __packed smbios_header {
	u8 type;
	u8 length;
	u16 handle;
};

/**
 * struct smbios_type0 - SMBIOS Type 0 (BIOS Information) structure
 *
 * This structure contains information about the BIOS/UEFI firmware
 * including vendor, version, release date, size, characteristics,
 * and version information for both BIOS and embedded controller.
 *
 * @hdr: Common SMBIOS structure header
 * @vendor: String number for BIOS vendor name
 * @bios_ver: String number for BIOS version
 * @bios_start_segment: Segment location of BIOS starting address
 * @bios_release_date: String number for BIOS release date
 * @bios_rom_size: Size of BIOS image
 * @bios_characteristics: BIOS characteristics bit field
 * @bios_characteristics_ext1: BIOS characteristics extension byte 1
 * @bios_characteristics_ext2: BIOS characteristics extension byte 2
 * @bios_major_release: Major release number of system BIOS
 * @bios_minor_release: Minor release number of system BIOS
 * @ec_major_release: Major release number of embedded controller
 * @ec_minor_release: Minor release number of embedded controller
 * @extended_bios_rom_size: Extended size of BIOS image
 * @eos: End-of-structure marker (double null bytes)
 */
struct __packed smbios_type0 {
	struct smbios_header hdr;
	u8 vendor;
	u8 bios_ver;
	u16 bios_start_segment;
	u8 bios_release_date;
	u8 bios_rom_size;
	u64 bios_characteristics;
	u8 bios_characteristics_ext1;
	u8 bios_characteristics_ext2;
	u8 bios_major_release;
	u8 bios_minor_release;
	u8 ec_major_release;
	u8 ec_minor_release;
	u16 extended_bios_rom_size;
	char eos[SMBIOS_STRUCT_EOS_BYTES];
};

#define SMBIOS_TYPE1_LENGTH_V20		0x08
#define SMBIOS_TYPE1_LENGTH_V21		0x19
#define SMBIOS_TYPE1_LENGTH_V24		0x1b

/**
 * struct smbios_type1 - SMBIOS Type 1 (System Information) structure
 *
 * This structure contains information that identifies the system as a
 * whole. It includes manufacturer, model, version, serial number, UUID,
 * and other system-level identification information.
 *
 * @hdr: Common SMBIOS structure header
 * @manufacturer: String number for manufacturer name
 * @product_name: String number for product name
 * @version: String number for version
 * @serial_number: String number for serial number
 * @uuid: Universal unique identifier for the system (16 bytes)
 * @wakeup_type: Identifies the event that caused the system to power up
 * @sku_number: String number for the system SKU
 * @family: String number for the family of systems
 * @eos: End-of-structure marker (double null bytes)
 */
struct __packed smbios_type1 {
	struct smbios_header hdr;
	u8 manufacturer;
	u8 product_name;
	u8 version;
	u8 serial_number;
	u8 uuid[16];
	u8 wakeup_type;
	u8 sku_number;
	u8 family;
	char eos[SMBIOS_STRUCT_EOS_BYTES];
};

#define SMBIOS_TYPE2_CON_OBJ_HANDLE_SIZE sizeof(u16)

/**
 * struct smbios_type2 - SMBIOS Type 2 (Baseboard Information) structure
 *
 * This structure contains information about the motherboard or system
 * baseboard including manufacturer, model, serial number, asset tag,
 * feature flags, and information about contained objects.
 *
 * @hdr: Common SMBIOS structure header
 * @manufacturer: String number for baseboard manufacturer name
 * @product_name: String number for baseboard product name
 * @version: String number for baseboard version
 * @serial_number: String number for baseboard serial number
 * @asset_tag_number: String number for asset tag
 * @feature_flags: Collection of flags identifying baseboard features
 * @chassis_location: String number describing baseboard location in chassis
 * @chassis_handle: Handle of chassis containing this baseboard
 * @board_type: Type of board (motherboard, processor card, etc.)
 * @number_contained_objects: Number of contained object handles
 * @eos: End-of-structure marker (double null bytes)
 *
 * Note: Dynamic bytes for contained object handles are inserted before @eos
 */
struct __packed smbios_type2 {
	struct smbios_header hdr;
	u8 manufacturer;
	u8 product_name;
	u8 version;
	u8 serial_number;
	u8 asset_tag_number;
	u8 feature_flags;
	u8 chassis_location;
	u16 chassis_handle;
	u8 board_type;
	u8 number_contained_objects;
	/*
	 * Dynamic bytes will be inserted here to store the objects.
	 * length is equal to 'number_contained_objects'.
	 */
	char eos[SMBIOS_STRUCT_EOS_BYTES];
};

/**
 * enum smbios_chassis_type - SMBIOS System Enclosure Chassis Types
 *
 * Defines the standard chassis types as specified in the SMBIOS specification.
 * The chassis type indicates the physical characteristics and form factor
 * of the system enclosure.
 */
enum smbios_chassis_type {
	SMBIOSCH_OTHER = 0x01,
	SMBIOSCH_UNKNOWN = 0x02,
	SMBIOSCH_DESKTOP = 0x03,
	SMBIOSCH_LOW_PROFILE_DESKTOP = 0x04,
	SMBIOSCH_PIZZA_BOX = 0x05,
	SMBIOSCH_MINI_TOWER = 0x06,
	SMBIOSCH_TOWER = 0x07,
	SMBIOSCH_PORTABLE = 0x08,
	SMBIOSCH_LAPTOP = 0x09,
	SMBIOSCH_NOTEBOOK = 0x0a,
	SMBIOSCH_HAND_HELD = 0x0b,
	SMBIOSCH_DOCKING_STATION = 0x0c,
	SMBIOSCH_ALL_IN_ONE = 0x0d,
	SMBIOSCH_SUB_NOTEBOOK = 0x0e,
	SMBIOSCH_SPACE_SAVING = 0x0f,
	SMBIOSCH_LUNCH_BOX = 0x10,
	SMBIOSCH_MAIN_SERVER = 0x11,
	SMBIOSCH_EXPANSION = 0x12,
	SMBIOSCH_SUB_CHASSIS = 0x13,
	SMBIOSCH_BUS_EXPANSION = 0x14,
	SMBIOSCH_PERIPHERAL = 0x15,
	SMBIOSCH_RAID = 0x16,
	SMBIOSCH_RACK_MOUNT = 0x17,
	SMBIOSCH_SEALED_CASE_PC = 0x18,
	SMBIOSCH_MULTI_SYSTEM = 0x19,
	SMBIOSCH_COMPACT_PCI = 0x1a,
	SMBIOSCH_ADVANCED_TCA = 0x1b,
	SMBIOSCH_BLADE = 0x1c,
	SMBIOSCH_BLADE_ENCLOSURE = 0x1d,
	SMBIOSCH_TABLET = 0x1e,
	SMBIOSCH_CONVERTIBLE = 0x1f,
	SMBIOSCH_DETACHABLE = 0x20,
	SMBIOSCH_IOT_GATEWAY = 0x21,
	SMBIOSCH_EMBEDDED_PC = 0x22,
	SMBIOSCH_MINI_PC = 0x23,
	SMBIOSCH_STICK_PC = 0x24,
};

/**
 * struct smbios_type3 - SMBIOS Type 3 (System Enclosure) structure
 *
 * This structure contains information about the system enclosure or chassis
 * including manufacturer, type, version, serial number, asset tag, power
 * states, thermal state, security status, and physical characteristics.
 *
 * @hdr: Common SMBIOS structure header
 * @manufacturer: String number for chassis manufacturer name
 * @chassis_type: Type of chassis (desktop, laptop, server, etc.)
 * @version: String number for chassis version
 * @serial_number: String number for chassis serial number
 * @asset_tag_number: String number for asset tag
 * @bootup_state: State of enclosure when last booted
 * @power_supply_state: State of enclosure's power supply
 * @thermal_state: Thermal state of the enclosure
 * @security_status: Physical security status of the enclosure
 * @oem_defined: OEM or BIOS vendor-specific information
 * @height: Height of enclosure in 'U's (rack units)
 * @number_of_power_cords: Number of power cords associated with enclosure
 * @element_count: Number of contained element records
 * @element_record_length: Length of each contained element record
 * @sku_number: String number for chassis or enclosure SKU number
 * @eos: End-of-structure marker (double null bytes)
 *
 * Note: Dynamic bytes for contained elements are inserted before @sku_number
 */
struct __packed smbios_type3 {
	struct smbios_header hdr;
	u8 manufacturer;
	u8 chassis_type;
	u8 version;
	u8 serial_number;
	u8 asset_tag_number;
	u8 bootup_state;
	u8 power_supply_state;
	u8 thermal_state;
	u8 security_status;
	u32 oem_defined;
	u8 height;
	u8 number_of_power_cords;
	u8 element_count;
	u8 element_record_length;
	/*
	 * Dynamic bytes will be inserted here to store the elements.
	 * length is equal to 'element_record_length' * 'element_record_length'
	 */
	u8 sku_number;
	char eos[SMBIOS_STRUCT_EOS_BYTES];
};

/**
 * struct smbios_type4 - SMBIOS Type 4 (Processor Information) structure
 *
 * This structure contains information about installed processors including
 * manufacturer, family, model, speed, cache handles, core/thread counts,
 * and other processor-specific characteristics and capabilities.
 *
 * @hdr: Common SMBIOS structure header
 * @socket_design: String number for socket designation
 * @processor_type: Type of processor (CPU, math processor, DSP, etc.)
 * @processor_family: Processor family information
 * @processor_manufacturer: String number for processor manufacturer
 * @processor_id: Processor identification information (2 DWORDs)
 * @processor_version: String number for processor version
 * @voltage: Voltage of the processor
 * @external_clock: External clock frequency in MHz
 * @max_speed: Maximum processor speed in MHz
 * @current_speed: Current processor speed in MHz
 * @status: Processor status information
 * @processor_upgrade: Processor socket type
 * @l1_cache_handle: Handle of L1 cache information
 * @l2_cache_handle: Handle of L2 cache information
 * @l3_cache_handle: Handle of L3 cache information
 * @serial_number: String number for processor serial number
 * @asset_tag: String number for asset tag
 * @part_number: String number for processor part number
 * @core_count: Number of cores per processor socket
 * @core_enabled: Number of enabled cores per processor socket
 * @thread_count: Number of threads per processor socket
 * @processor_characteristics: Processor characteristics
 * @processor_family2: Extended processor family information
 * @core_count2: Extended number of cores per processor socket
 * @core_enabled2: Extended number of enabled cores per processor socket
 * @thread_count2: Extended number of threads per processor socket
 * @thread_enabled: Number of enabled threads per processor socket
 * @eos: End-of-structure marker (double null bytes)
 */
struct __packed smbios_type4 {
	struct smbios_header hdr;
	u8 socket_design;
	u8 processor_type;
	u8 processor_family;
	u8 processor_manufacturer;
	u32 processor_id[2];
	u8 processor_version;
	u8 voltage;
	u16 external_clock;
	u16 max_speed;
	u16 current_speed;
	u8 status;
	u8 processor_upgrade;
	u16 l1_cache_handle;
	u16 l2_cache_handle;
	u16 l3_cache_handle;
	u8 serial_number;
	u8 asset_tag;
	u8 part_number;
	u8 core_count;
	u8 core_enabled;
	u8 thread_count;
	u16 processor_characteristics;
	u16 processor_family2;
	u16 core_count2;
	u16 core_enabled2;
	u16 thread_count2;
	u16 thread_enabled;
	char eos[SMBIOS_STRUCT_EOS_BYTES];
};

union __packed cache_config {
	struct {
		u16 level:3;
		u16 bsocketed:1;
		u16 rsvd0:1;
		u16 locate:2;
		u16 benabled:1;
		u16 opmode:2;
		u16 rsvd1:6;
	} fields;
	u16 data;
};

union __packed cache_size_word {
	struct {
		u16 size:15;
		u16 granu:1;
	} fields;
	u16 data;
};

union __packed cache_size_dword {
	struct {
		u32 size:31;
		u32 granu:1;
	} fields;
	u32 data;
};

union __packed cache_sram_type {
	struct {
		u16 other:1;
		u16 unknown:1;
		u16 nonburst:1;
		u16 burst:1;
		u16 plburst:1;
		u16 sync:1;
		u16 async:1;
		u16 rsvd:9;
	} fields;
	u16 data;
};

struct __packed smbios_type7 {
	struct smbios_header hdr;
	u8 socket_design;
	union cache_config config;
	union cache_size_word max_size;
	union cache_size_word inst_size;
	union cache_sram_type supp_sram_type;
	union cache_sram_type curr_sram_type;
	u8 speed;
	u8 err_corr_type;
	u8 sys_cache_type;
	u8 associativity;
	union cache_size_dword max_size2;
	union cache_size_dword inst_size2;
	char eos[SMBIOS_STRUCT_EOS_BYTES];
};

#define SMBIOS_TYPE9_PGROUP_SIZE 5

struct pci_attr_lookup_table {
	const char *str;
	u8 slot_type;
	u8 data_bus_width;
	u8 slot_length;
	u8 chara1;
	u8 chara2;
};

union dev_func_num {
	struct {
		u8 dev_num:5;
		u8 func_num:3;
	} fields;
	u8 data;
};

struct __packed smbios_type9 {
	struct smbios_header hdr;
	u8 socket_design;
	u8 slot_type;
	u8 slot_data_bus_width;
	u8 current_usage;
	u8 slot_length;
	u16 slot_id;
	u8 slot_characteristics_1;
	u8 slot_characteristics_2;
	u16 segment_group_number;
	u8 bus_number;
	union dev_func_num device_function_number;
	u8 electrical_bus_width;
	u8 peer_grouping_count;
	/*
	 * Dynamic bytes will be inserted here to store peer_groups.
	 * length is equal to 'peer_grouping_count' * 5
	 */
	u8 slot_information;
	u8 slot_physical_width;
	u16 slot_pitch;
	u8 slot_height;
	char eos[SMBIOS_STRUCT_EOS_BYTES];
};

struct __packed smbios_type16 {
	struct smbios_header hdr;
	u8 location;
	u8 use;
	u8 error_correction;
	u32 maximum_capacity;
	u16 error_information_handle;
	u16 number_of_memory_devices;
	/* The following field is only present in SMBIOS v2.7+ */
	u64 extended_maximum_capacity;
	char eos[SMBIOS_STRUCT_EOS_BYTES];
};

struct __packed smbios_type19 {
	struct smbios_header hdr;
	u32 starting_address;
	u32 ending_address;
	u16 memory_array_handle;
	u8 partition_width;
	/* The following fields are only present in SMBIOS v2.7+ */
	u64 extended_starting_address;
	u64 extended_ending_address;
	char eos[SMBIOS_STRUCT_EOS_BYTES];
};

struct __packed smbios_type32 {
	u8 type;
	u8 length;
	u16 handle;
	u8 reserved[6];
	u8 boot_status;
	char eos[SMBIOS_STRUCT_EOS_BYTES];
};

struct __packed smbios_type127 {
	u8 type;
	u8 length;
	u16 handle;
	char eos[SMBIOS_STRUCT_EOS_BYTES];
};

/**
 * fill_smbios_header() - Fill the header of an SMBIOS table
 *
 * This fills the header of an SMBIOS table structure.
 *
 * @table:	start address of the structure
 * @type:	the type of structure
 * @length:	the length of the formatted area of the structure
 * @handle:	the structure's handle, a unique 16-bit number
 */
static inline void fill_smbios_header(void *table, int type,
				      int length, int handle)
{
	struct smbios_header *header = table;

	header->type = type;
	header->length = length - SMBIOS_STRUCT_EOS_BYTES;
	header->handle = handle;
}

/**
 * write_smbios_table() - Write SMBIOS table
 *
 * This writes SMBIOS table at a given address.
 *
 * @addr:	start address to write SMBIOS table, 16-byte-alignment
 * recommended. Note that while the SMBIOS tables themself have no alignment
 * requirement, some systems may requires alignment. For example x86 systems
 * which put tables at f0000 require 16-byte alignment
 *
 * Return:	end address of SMBIOS table (and start address for next entry)
 *		or NULL in case of an error
 */
ulong write_smbios_table(ulong addr);

/**
 * smbios_entry() - Get a valid struct smbios_entry pointer
 *
 * @address:   address where smbios tables is located
 * @size:      size of smbios table
 * @return:    NULL or a valid pointer to a struct smbios_entry
 */
const struct smbios_entry *smbios_entry(u64 address, u32 size);

/**
 * smbios_get_header() - Search for an SMBIOS header type
 *
 * @info: SMBIOS info
 * @type: SMBIOS type
 * @return: NULL or a valid pointer to a struct smbios_header
 */
const void *smbios_get_header(const struct smbios_info *info, int type);

/**
 * smbios_string() - Return string from SMBIOS
 *
 * @header:    pointer to struct smbios_header
 * @index:     string index
 * @return:    NULL or a valid char pointer
 */
char *smbios_string(const struct smbios_header *header, int index);

/**
 * smbios_update_version() - Update the version string
 *
 * This can be called after the SMBIOS tables are written (e.g. after the U-Boot
 * main loop has started) to update the BIOS version string (SMBIOS table 0).
 *
 * @version: New version string to use
 * Return: 0 if OK, -ENOENT if no version string was previously written,
 *	-ENOSPC if the new string is too large to fit
 */
int smbios_update_version(const char *version);

/**
 * smbios_update_version_full() - Update the version string
 *
 * This can be called after the SMBIOS tables are written (e.g. after the U-Boot
 * main loop has started) to update the BIOS version string (SMBIOS table 0).
 * It scans for the correct place to put the version, so does not need U-Boot
 * to have actually written the tables itself (e.g. if a previous bootloader
 * did it).
 *
 * @smbios_tab: Start of SMBIOS tables
 * @version: New version string to use
 * Return: 0 if OK, -ENOENT if no version string was previously written,
 *	-ENOSPC if the new string is too large to fit
 */
int smbios_update_version_full(void *smbios_tab, const char *version);

/**
 * smbios_prepare_measurement() - Update smbios table for the measurement
 *
 * TCG specification requires to measure static configuration information.
 * This function clear the device dependent parameters such as
 * serial number for the measurement.
 *
 * @entry: pointer to a struct smbios3_entry
 * @header: pointer to a struct smbios_header
 * @table_maximum_size: number of bytes used by the tables at @header
 */
void smbios_prepare_measurement(const struct smbios3_entry *entry,
				struct smbios_header *smbios_copy,
				int table_maximum_size);

/**
 * smbios_get_string() - get SMBIOS string from table
 *
 * @table:	SMBIOS table
 * @index:	index of the string
 * Return:	address of string, may point to empty string
 */
const char *smbios_get_string(void *table, int index);

/**
 * smbios_next_table() - Find the next table
 *
 * @info: SMBIOS info
 * @table: Table to start from
 * Return: Pointer to the next table, or NULL if @table is the last
 */
struct smbios_header *smbios_next_table(const struct smbios_info *info,
					struct smbios_header *table);

/**
 * smbios_locate() - Locate the SMBIOS tables
 *
 * @addr: Address of SMBIOS table, typically gd_smbios_start()
 * @info: Returns the SMBIOS info, on success
 * Return: 0 if OK, -EINVAL if the address is 0, -NOENT if the header signature
 * is not recognised, -EIO if the checksum is wrong
 */
int smbios_locate(ulong addr, struct smbios_info *info);

#endif /* _SMBIOS_H_ */
