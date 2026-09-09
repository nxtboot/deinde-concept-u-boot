/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Logging (to memory) of calls from an EFI app
 *
 * Copyright 2024 Google LLC
 * Written by Simon Glass <sjg@chromium.org>
 */

#ifndef __EFI_LOG_H
#define __EFI_LOG_H

#include <linux/types.h>
#include <efi.h>

/**
 * enum efil_tag - Types of logging records which can be created
 */
enum efil_tag {
	EFILT_ALLOCATE_PAGES,
	EFILT_FREE_PAGES,
	EFILT_ALLOCATE_POOL,
	EFILT_FREE_POOL,
	EFILT_OPEN_PROTOCOL,
	EFILT_LOCATE_PROTOCOL,
	EFILT_LOAD_IMAGE,
	EFILT_EXIT_BOOT_SERVICES,
	EFILT_LOCATE_HANDLE,
	EFILT_LOCATE_HANDLE_BUFFER,
	EFILT_LOCATE_DEVICE_PATH,
	EFILT_CLOSE_PROTOCOL,
	EFILT_INSTALL_PROTOCOL_INTERFACE,
	EFILT_UNINSTALL_PROTOCOL_INTERFACE,
	EFILT_REINSTALL_PROTOCOL_INTERFACE,
	EFILT_REGISTER_PROTOCOL_NOTIFY,
	EFILT_OPEN_PROTOCOL_INFORMATION,
	EFILT_PROTOCOLS_PER_HANDLE,
	EFILT_INSTALL_CONFIGURATION_TABLE,
	EFILT_GET_NEXT_MONOTONIC_COUNT,
	EFILT_STALL,
	EFILT_SET_WATCHDOG_TIMER,
	EFILT_CALCULATE_CRC32,
	EFILT_CALL,

	EFILT_TESTING,

	EFILT_COUNT,
};

/**
 * struct efil_rec_hdr - Header for each logging record
 *
 * @tag: Tag which indicates the type of the record
 * @size: Size of the record in bytes
 * @ended: true if record has been completed (i.e. the function returned), false
 *	if it is still pending
 * @e_ret: Records the return function from the logged function
 * @start_us: Microseconds since boot when the function was called
 * @dur_us: Microseconds the function took, valid only when @ended
 */
struct efil_rec_hdr {
	enum efil_tag tag;
	int size;
	bool ended;
	efi_status_t e_ret;
	u32 start_us;
	u32 dur_us;
};

/**
 * struct efil_hdr - Holds the header for the log
 *
 * @upto: Offset at which to store the next log record
 * @size: Total size of the log in bytes
 * @missed: Number of records dropped because the log was full
 */
struct efil_hdr {
	int upto;
	int size;
	int missed;
};

/** Maximum number of arguments recorded for a generic call */
#define EFIL_CALL_MAX_ARGS	4

/**
 * enum efil_prot - Protocols whose member functions can be logged
 *
 * A protocol is identified by an index rather than by its GUID, since a name
 * table is needed for each protocol anyway and an index keeps the record small
 * enough to log a full boot
 */
enum efil_prot {
	EFILP_NONE,
	EFILP_FILE,
	EFILP_SIMPLE_FS,

	EFILP_COUNT,
};

/**
 * enum efil_file_method - member functions of EFI_FILE_PROTOCOL
 *
 * These must be in the same order as file_method_name[]
 */
enum efil_file_method {
	EFILF_OPEN,
	EFILF_CLOSE,
	EFILF_DELETE,
	EFILF_READ,
	EFILF_WRITE,
	EFILF_GETPOS,
	EFILF_SETPOS,
	EFILF_GETINFO,
	EFILF_SETINFO,
	EFILF_FLUSH,
	EFILF_OPEN_EX,
	EFILF_READ_EX,
	EFILF_WRITE_EX,
	EFILF_FLUSH_EX,

	EFILF_COUNT,
};

/**
 * enum efil_simple_fs_method - member functions of the simple-file-system
 *
 * These must be in the same order as simple_fs_method_name[]
 */
enum efil_simple_fs_method {
	EFILS_OPEN_VOLUME,

	EFILS_COUNT,
};

/**
 * struct efil_call - holds info from a call to a protocol member function
 *
 * This is a generic record, used where a typed record would cost more than the
 * decoding is worth. Protocols have hundreds of member functions between them,
 * so each one costs a single line at the call site rather than a tag, a struct
 * and a pair of functions
 *
 * @prot: Protocol being called (enum efil_prot)
 * @method: Member function being called, an index into the protocol's name
 *	table
 * @nargs: Number of arguments in @arg
 * @arg: Arguments to the call
 * @e_arg: Value of interest on return, e.g. the size actually read
 */
struct efil_call {
	u8 prot;
	u8 method;
	u8 nargs;
	u64 arg[EFIL_CALL_MAX_ARGS];
	u64 e_arg;
};

/** struct efil_install_configuration_table - holds info from the call */
struct efil_install_configuration_table {
	efi_guid_t guid;
	void *table;
};

/**
 * struct efil_get_next_monotonic_count - holds info from the call
 *
 * @e_count: Contains the value of *@count on return from the EFI function
 */
struct efil_get_next_monotonic_count {
	u64 *count;
	u64 e_count;
};

/** struct efil_stall - holds info from efi_stall() call */
struct efil_stall {
	u64 microseconds;
};

/** struct efil_set_watchdog_timer - holds info from the call */
struct efil_set_watchdog_timer {
	u64 timeout;
	u64 watchdog_code;
	u64 data_size;
	u16 *watchdog_data;
};

/**
 * struct efil_calculate_crc32 - holds info from efi_calculate_crc32() call
 *
 * @e_crc32: Contains the value of *@crc32_p on return from the EFI function
 */
struct efil_calculate_crc32 {
	const void *data;
	efi_uintn_t data_size;
	u32 *crc32_p;
	u32 e_crc32;
};

enum efil_test_t {
	EFI_LOG_TEST0,
	EFI_LOG_TEST1,

	EFI_LOG_TEST_COUNT,
};

/**
 * struct efil_testing - used for testing the log
 */
struct efil_testing {
	enum efil_test_t enum_val;
	efi_uintn_t int_val;
	u64 *memory;
	void **buffer;
	u64 e_memory;
	void *e_buffer;
};

/**
 * struct efil_allocate_pages - holds info from efi_allocate_pages() call
 *
 * @e_memory: Contains the value of *@memory on return from the EFI function
 */
struct efil_allocate_pages {
	enum efi_allocate_type type;
	enum efi_memory_type memory_type;
	efi_uintn_t pages;
	u64 *memory;
	u64 e_memory;
};

/** struct efil_free_pages - holds info from efi_free_pages() call */
struct efil_free_pages {
	u64 memory;
	efi_uintn_t pages;
};

/** struct efil_allocate_pool - holds info from efi_allocate_pool() call
 *
 * @e_buffer: Contains the value of *@buffer on return from the EFI function
 */
struct efil_allocate_pool {
	enum efi_memory_type pool_type;
	efi_uintn_t size;
	void **buffer;
	void *e_buffer;
};

/** struct efil_free_pool - holds log-info from efi_free_pool() call */
struct efil_free_pool {
	void *buffer;
};

/**
 * struct efil_open_protocol - holds info from efi_open_protocol() call
 *
 * @e_interface: Contains the value of *@interface on return
 */
struct efil_open_protocol {
	efi_handle_t handle;
	efi_guid_t protocol;
	void **interface;
	efi_handle_t agent_handle;
	efi_handle_t controller_handle;
	u32 attributes;
	void *e_interface;
};

/**
 * struct efil_locate_protocol - holds info from efi_locate_protocol() call
 *
 * @e_interface: Contains the value of *@interface on return
 */
struct efil_locate_protocol {
	efi_guid_t protocol;
	void *registration;
	void **interface;
	void *e_interface;
};

/**
 * struct efil_load_image - holds info from efi_load_image() call
 *
 * @e_image_handle: Contains the value of *@image_handle on return
 */
struct efil_load_image {
	bool boot_policy;
	efi_handle_t parent_image;
	void *source_buffer;
	efi_uintn_t source_size;
	efi_handle_t *image_handle;
	efi_handle_t e_image_handle;
};

/** struct efil_exit_boot_services - holds info from the call of that name */
struct efil_exit_boot_services {
	efi_handle_t image_handle;
	efi_uintn_t map_key;
};

/**
 * struct efil_locate_handle - holds info from efi_locate_handle() call
 *
 * @e_buffer_size: Contains the value of *@buffer_size on return
 */
struct efil_locate_handle {
	enum efi_locate_search_type search_type;
	efi_guid_t protocol;
	void *search_key;
	efi_uintn_t *buffer_size;
	efi_uintn_t e_buffer_size;
};

/**
 * struct efil_locate_handle_buffer - holds info from the call of that name
 *
 * @e_no_handles: Contains the value of *@no_handles on return
 */
struct efil_locate_handle_buffer {
	enum efi_locate_search_type search_type;
	efi_guid_t protocol;
	void *search_key;
	efi_uintn_t *no_handles;
	efi_uintn_t e_no_handles;
};

/** struct efil_locate_device_path - holds info from the call of that name */
struct efil_locate_device_path {
	efi_guid_t protocol;
	void *device_path;
	efi_handle_t *device;
	efi_handle_t e_device;
};

/** struct efil_close_protocol - holds info from efi_close_protocol() call */
struct efil_close_protocol {
	efi_handle_t handle;
	efi_guid_t protocol;
	efi_handle_t agent_handle;
	efi_handle_t controller_handle;
};

/** struct efil_install_protocol_interface - info from the call of that name */
struct efil_install_protocol_interface {
	efi_handle_t *handle;
	efi_guid_t protocol;
	int protocol_interface_type;
	void *protocol_interface;
	efi_handle_t e_handle;
};

/** struct efil_uninstall_protocol_interface - info from that call */
struct efil_uninstall_protocol_interface {
	efi_handle_t handle;
	efi_guid_t protocol;
	void *protocol_interface;
};

/** struct efil_reinstall_protocol_interface - info from that call */
struct efil_reinstall_protocol_interface {
	efi_handle_t handle;
	efi_guid_t protocol;
	void *old_interface;
	void *new_interface;
};

/** struct efil_register_protocol_notify - info from that call */
struct efil_register_protocol_notify {
	efi_guid_t protocol;
	struct efi_event *event;
	void **registration;
	void *e_registration;
};

/** struct efil_open_protocol_information - info from that call */
struct efil_open_protocol_information {
	efi_handle_t handle;
	efi_guid_t protocol;
	efi_uintn_t *entry_count;
	efi_uintn_t e_entry_count;
};

/** struct efil_protocols_per_handle - info from that call */
struct efil_protocols_per_handle {
	efi_handle_t handle;
	efi_uintn_t *protocol_buffer_count;
	efi_uintn_t e_protocol_buffer_count;
};

/*
 * The functions below are in pairs, with a 'start' and 'end' call for each EFI
 * function. The 'start' function (efi_logs_...) is called when the function is
 * started. It records all the arguments. The 'end' function (efi_loge_...) is
 * called when the function is ready to return. It records any output arguments
 * as well as the return value.
 *
 * The start function returns the offset of the log record. This must be passed
 * to the end function, so it can add the status code and any other useful
 * information. It is not possible for the end functions to remember the offset
 * from the associated start function, since EFI functions may be called in a
 * nested way and there is no obvious way to determine the log record to which
 * the end function refers.
 *
 * If the start function returns an error code (i.e. an offset < 0) then it is
 * safe to pass that to the end function. It will simply ignore the operation.
 * Common errors are -ENOENT if there is no log and -ENOSPC if the log is full
 */

#if CONFIG_IS_ENABLED(EFI_LOG)

/**
 * efi_logs_testing() - Record a test call to an efi function
 *
 * @enum_val:	enum value
 * @int_val:	integer value
 * @buffer:	place to write pointer address
 * @memory:	place to write memory address
 * Return:	log-offset of this new record, or -ve error code
 */
int efi_logs_testing(enum efil_test_t enum_val, efi_uintn_t int_value,
		     void *buffer, u64 *memory);

/**
 * efi_loge_testing() - Record a return from a test call
 *
 * This stores the value of the pointers also
 *
 * ofs: Offset of the record to end
 * efi_ret: status code to record
 */
int efi_loge_testing(int ofs, efi_status_t efi_ret);

/**
 * efi_logs_allocate_pages() - Record a call to efi_allocate_pages()
 *
 * @type:		type of allocation to be performed
 * @memory_type:	usage type of the allocated memory
 * @pages:		number of pages to be allocated
 * @memory:		place to write address of allocated memory
 * Return:		log-offset of this new record, or -ve error code
 */
int efi_logs_allocate_pages(enum efi_allocate_type type,
			    enum efi_memory_type memory_type, efi_uintn_t pages,
			    u64 *memory);

/**
 * efi_loge_allocate_pages() - Record a return from efi_allocate_pages()
 *
 * This stores the value of the memory pointer also
 *
 * ofs: Offset of the record to end
 * efi_ret: status code to record
 */
int efi_loge_allocate_pages(int ofs, efi_status_t efi_ret);

/**
 * efi_logs_free_pages() - Record a call to efi_free_pages()
 *
 * @memory:	start of the memory area to be freed
 * @pages:	number of pages to be freed
 * Return:	log-offset of this new record, or -ve error code
 */
int efi_logs_free_pages(u64 memory, efi_uintn_t pages);

/**
 * efi_loge_free_pages() - Record a return from efi_free_pages()
 *
 * ofs: Offset of the record to end
 * efi_ret: status code to record
 */
int efi_loge_free_pages(int ofs, efi_status_t efi_ret);

/**
 * efi_logs_allocate_pool() - Record a call to efi_allocate_pool()
 *
 * @pool_type:	type of the pool from which memory is to be allocated
 * @size:	number of bytes to be allocated
 * @buffer:	place to hold pointer to allocated memory
 * Return:	log-offset of this new record, or -ve error code
 */
int efi_logs_allocate_pool(enum efi_memory_type pool_type, efi_uintn_t size,
			   void **buffer);

/**
 * efi_loge_allocate_pool() - Record a return from efi_allocate_pool()
 *
 * This stores the value of the buffer pointer also
 *
 * ofs: Offset of the record to end
 * efi_ret: status code to record
 */
int efi_loge_allocate_pool(int ofs, efi_status_t efi_ret);

/**
 * efi_logs_free_pool() - Record a call to efi_free_pool()
 *
 * @buffer:	start of memory to be freed
 * Return:	log-offset of this new record, or -ve error code
 */
int efi_logs_free_pool(void *buffer);

/**
 * efi_loge_free_pool() - Record a return from efi_free_pool()
 *
 * ofs: Offset of the record to end
 * efi_ret: status code to record
 */
int efi_loge_free_pool(int ofs, efi_status_t efi_ret);

/**
 * efi_logs_open_protocol() - Record a call to efi_open_protocol()
 *
 * @handle:		handle on which the protocol is opened
 * @protocol:		GUID of the protocol
 * @interface:		place to hold the protocol interface
 * @agent_handle:	handle of the agent opening the protocol
 * @controller_handle:	handle of the controller
 * @attributes:		flags saying how the protocol is opened
 * Return:		log-offset of this new record, or -ve error code
 */
int efi_logs_open_protocol(efi_handle_t handle, const efi_guid_t *protocol,
			   void **interface, efi_handle_t agent_handle,
			   efi_handle_t controller_handle, u32 attributes);

/**
 * efi_loge_open_protocol() - Record a return from efi_open_protocol()
 *
 * This stores the value of the interface pointer also
 *
 * ofs: Offset of the record to end
 * efi_ret: status code to record
 */
int efi_loge_open_protocol(int ofs, efi_status_t efi_ret);

/**
 * efi_logs_locate_protocol() - Record a call to efi_locate_protocol()
 *
 * @protocol:		GUID of the protocol
 * @registration:	registration key, or NULL
 * @interface:		place to hold the protocol interface
 * Return:		log-offset of this new record, or -ve error code
 */
int efi_logs_locate_protocol(const efi_guid_t *protocol, void *registration,
			     void **interface);

/**
 * efi_loge_locate_protocol() - Record a return from efi_locate_protocol()
 *
 * This stores the value of the interface pointer also
 *
 * ofs: Offset of the record to end
 * efi_ret: status code to record
 */
int efi_loge_locate_protocol(int ofs, efi_status_t efi_ret);

/**
 * efi_logs_load_image() - Record a call to efi_load_image()
 *
 * @boot_policy:	true to indicate that the request is from the boot
 *			manager
 * @parent_image:	handle of the caller
 * @source_buffer:	memory holding the image, or NULL
 * @source_size:	size of @source_buffer
 * @image_handle:	place to hold the handle for the loaded image
 * Return:		log-offset of this new record, or -ve error code
 */
int efi_logs_load_image(bool boot_policy, efi_handle_t parent_image,
			void *source_buffer, efi_uintn_t source_size,
			efi_handle_t *image_handle);

/**
 * efi_loge_load_image() - Record a return from efi_load_image()
 *
 * This stores the value of the image handle also
 *
 * ofs: Offset of the record to end
 * efi_ret: status code to record
 */
int efi_loge_load_image(int ofs, efi_status_t efi_ret);

/**
 * efi_logs_exit_boot_services() - Record a call to efi_exit_boot_services()
 *
 * @image_handle:	handle of the loaded image
 * @map_key:		key of the memory map
 * Return:		log-offset of this new record, or -ve error code
 */
int efi_logs_exit_boot_services(efi_handle_t image_handle,
				efi_uintn_t map_key);

/**
 * efi_loge_exit_boot_services() - Record a return from that call
 *
 * ofs: Offset of the record to end
 * efi_ret: status code to record
 */
int efi_loge_exit_boot_services(int ofs, efi_status_t efi_ret);

/**
 * efi_logs_locate_handle() - Record a call to efi_locate_handle()
 *
 * Return: log-offset of this new record, or -ve error code
 */
int efi_logs_locate_handle(enum efi_locate_search_type search_type, const efi_guid_t *protocol,
			   void *search_key, efi_uintn_t *buffer_size);

/**
 * efi_loge_locate_handle() - Record a return from efi_locate_handle()
 *
 * ofs: Offset of the record to end
 * efi_ret: status code to record
 */
int efi_loge_locate_handle(int ofs, efi_status_t efi_ret);

/**
 * efi_logs_locate_handle_buffer() - Record a call to efi_locate_handle_buffer()
 *
 * Return: log-offset of this new record, or -ve error code
 */
int efi_logs_locate_handle_buffer(enum efi_locate_search_type search_type,
				  const efi_guid_t *protocol, void *search_key,
				  efi_uintn_t *no_handles);

/**
 * efi_loge_locate_handle_buffer() - Record a return from efi_locate_handle_buffer()
 *
 * ofs: Offset of the record to end
 * efi_ret: status code to record
 */
int efi_loge_locate_handle_buffer(int ofs, efi_status_t efi_ret);

/**
 * efi_logs_locate_device_path() - Record a call to efi_locate_device_path()
 *
 * Return: log-offset of this new record, or -ve error code
 */
int efi_logs_locate_device_path(const efi_guid_t *protocol, void *device_path,
				efi_handle_t *device);

/**
 * efi_loge_locate_device_path() - Record a return from efi_locate_device_path()
 *
 * ofs: Offset of the record to end
 * efi_ret: status code to record
 */
int efi_loge_locate_device_path(int ofs, efi_status_t efi_ret);

/**
 * efi_logs_close_protocol() - Record a call to efi_close_protocol()
 *
 * Return: log-offset of this new record, or -ve error code
 */
int efi_logs_close_protocol(efi_handle_t handle, const efi_guid_t *protocol,
			    efi_handle_t agent_handle,
			    efi_handle_t controller_handle);

/**
 * efi_loge_close_protocol() - Record a return from efi_close_protocol()
 *
 * ofs: Offset of the record to end
 * efi_ret: status code to record
 */
int efi_loge_close_protocol(int ofs, efi_status_t efi_ret);

/**
 * efi_logs_install_protocol_interface() - Record a call to efi_install_protocol_interface()
 *
 * Return: log-offset of this new record, or -ve error code
 */
int efi_logs_install_protocol_interface(efi_handle_t *handle, const efi_guid_t *protocol,
					int protocol_interface_type,
					void *protocol_interface);

/**
 * efi_loge_install_protocol_interface() - Record a return from efi_install_protocol_interface()
 *
 * ofs: Offset of the record to end
 * efi_ret: status code to record
 */
int efi_loge_install_protocol_interface(int ofs, efi_status_t efi_ret);

/**
 * efi_logs_uninstall_protocol_interface() - Record a call to efi_uninstall_protocol_interface()
 *
 * Return: log-offset of this new record, or -ve error code
 */
int efi_logs_uninstall_protocol_interface(efi_handle_t handle, const efi_guid_t *protocol,
					  void *protocol_interface);

/**
 * efi_loge_uninstall_protocol_interface() - Record a return from efi_uninstall_protocol_interface()
 *
 * ofs: Offset of the record to end
 * efi_ret: status code to record
 */
int efi_loge_uninstall_protocol_interface(int ofs, efi_status_t efi_ret);

/**
 * efi_logs_reinstall_protocol_interface() - Record a call to efi_reinstall_protocol_interface()
 *
 * Return: log-offset of this new record, or -ve error code
 */
int efi_logs_reinstall_protocol_interface(efi_handle_t handle, const efi_guid_t *protocol,
					  void *old_interface, void *new_interface);

/**
 * efi_loge_reinstall_protocol_interface() - Record a return from efi_reinstall_protocol_interface()
 *
 * ofs: Offset of the record to end
 * efi_ret: status code to record
 */
int efi_loge_reinstall_protocol_interface(int ofs, efi_status_t efi_ret);

/**
 * efi_logs_register_protocol_notify() - Record a call to efi_register_protocol_notify()
 *
 * Return: log-offset of this new record, or -ve error code
 */
int efi_logs_register_protocol_notify(const efi_guid_t *protocol, struct efi_event *event,
				      void **registration);

/**
 * efi_loge_register_protocol_notify() - Record a return from efi_register_protocol_notify()
 *
 * ofs: Offset of the record to end
 * efi_ret: status code to record
 */
int efi_loge_register_protocol_notify(int ofs, efi_status_t efi_ret);

/**
 * efi_logs_open_protocol_information() - Record a call to efi_open_protocol_information()
 *
 * Return: log-offset of this new record, or -ve error code
 */
int efi_logs_open_protocol_information(efi_handle_t handle, const efi_guid_t *protocol,
				       efi_uintn_t *entry_count);

/**
 * efi_loge_open_protocol_information() - Record a return from efi_open_protocol_information()
 *
 * ofs: Offset of the record to end
 * efi_ret: status code to record
 */
int efi_loge_open_protocol_information(int ofs, efi_status_t efi_ret);

/**
 * efi_logs_protocols_per_handle() - Record a call to efi_protocols_per_handle()
 *
 * Return: log-offset of this new record, or -ve error code
 */
int efi_logs_protocols_per_handle(efi_handle_t handle, efi_uintn_t *protocol_buffer_count);

/**
 * efi_loge_protocols_per_handle() - Record a return from efi_protocols_per_handle()
 *
 * ofs: Offset of the record to end
 * efi_ret: status code to record
 */
int efi_loge_protocols_per_handle(int ofs, efi_status_t efi_ret);

/**
 * efi_logs_install_configuration_table() - Log a call to InstallConfigurationTable
 *
 * @guid: GUID of the table to install
 * @table: Table to install
 * Return: Offset of the log record, or -ve error code
 */
int efi_logs_install_configuration_table(const efi_guid_t *guid, void *table);

/**
 * efi_loge_install_configuration_table() - Complete the log record
 *
 * @ofs: Offset returned by efi_logs_install_configuration_table()
 * @efi_ret: Status code returned by the EFI function
 * Return: 0 if OK, -ve on error
 */
int efi_loge_install_configuration_table(int ofs, efi_status_t efi_ret);

/**
 * efi_logs_get_next_monotonic_count() - Log a call to GetNextMonotonicCount
 *
 * @count: Pointer which receives the count
 * Return: Offset of the log record, or -ve error code
 */
int efi_logs_get_next_monotonic_count(u64 *count);

/**
 * efi_loge_get_next_monotonic_count() - Complete the log record
 *
 * @ofs: Offset returned by efi_logs_get_next_monotonic_count()
 * @efi_ret: Status code returned by the EFI function
 * Return: 0 if OK, -ve on error
 */
int efi_loge_get_next_monotonic_count(int ofs, efi_status_t efi_ret);

/**
 * efi_logs_stall() - Log a call to Stall
 *
 * @microseconds: Time to stall for
 * Return: Offset of the log record, or -ve error code
 */
int efi_logs_stall(u64 microseconds);

/**
 * efi_loge_stall() - Complete the log record
 *
 * @ofs: Offset returned by efi_logs_stall()
 * @efi_ret: Status code returned by the EFI function
 * Return: 0 if OK, -ve on error
 */
int efi_loge_stall(int ofs, efi_status_t efi_ret);

/**
 * efi_logs_set_watchdog_timer() - Log a call to SetWatchdogTimer
 *
 * @timeout: Seconds before the watchdog resets the system
 * @watchdog_code: Code to log when resetting
 * @data_size: Size of @watchdog_data in bytes
 * @watchdog_data: Data to log when resetting
 * Return: Offset of the log record, or -ve error code
 */
int efi_logs_set_watchdog_timer(u64 timeout, u64 watchdog_code, u64 data_size,
				u16 *watchdog_data);

/**
 * efi_loge_set_watchdog_timer() - Complete the log record
 *
 * @ofs: Offset returned by efi_logs_set_watchdog_timer()
 * @efi_ret: Status code returned by the EFI function
 * Return: 0 if OK, -ve on error
 */
int efi_loge_set_watchdog_timer(int ofs, efi_status_t efi_ret);

/**
 * efi_logs_calculate_crc32() - Log a call to CalculateCrc32
 *
 * @data: Data to checksum
 * @data_size: Size of @data in bytes
 * @crc32_p: Pointer which receives the checksum
 * Return: Offset of the log record, or -ve error code
 */
int efi_logs_calculate_crc32(const void *data, efi_uintn_t data_size,
			     u32 *crc32_p);

/**
 * efi_loge_calculate_crc32() - Complete the log record
 *
 * @ofs: Offset returned by efi_logs_calculate_crc32()
 * @efi_ret: Status code returned by the EFI function
 * Return: 0 if OK, -ve on error
 */
int efi_loge_calculate_crc32(int ofs, efi_status_t efi_ret);

/**
 * efi_logs_call() - Log a call to a protocol member function
 *
 * This is the generic record, for calls which do not warrant a typed record
 *
 * @prot: Protocol being called
 * @method: Member function being called
 * @nargs: Number of arguments which follow, at most EFIL_CALL_MAX_ARGS
 * Return: Offset of the log record, or -ve error code
 */
int efi_logs_call(enum efil_prot prot, uint method, uint nargs, ...);

/**
 * efi_loge_call() - Complete a generic log record
 *
 * @ofs: Offset returned by efi_logs_call()
 * @efi_ret: Status code returned by the EFI function
 * @e_arg: Value of interest on return, or 0 if there is none
 * Return: 0 if OK, -ve on error
 */
int efi_loge_call(int ofs, efi_status_t efi_ret, u64 e_arg);

#else /* !EFI_LOG */

static inline int efi_logs_locate_handle(enum efi_locate_search_type search_type,
					 const efi_guid_t *protocol,
					 void *search_key,
					 efi_uintn_t *buffer_size)
{
	return -ENOSYS;
}

static inline int efi_loge_locate_handle(int ofs, efi_status_t efi_ret)
{
	return -ENOSYS;
}

static inline int efi_logs_locate_handle_buffer(enum efi_locate_search_type search_type,
						const efi_guid_t *protocol,
						void *search_key,
						efi_uintn_t *no_handles)
{
	return -ENOSYS;
}

static inline int efi_loge_locate_handle_buffer(int ofs, efi_status_t efi_ret)
{
	return -ENOSYS;
}

static inline int efi_logs_locate_device_path(const efi_guid_t *protocol,
					      void *device_path,
					      efi_handle_t *device)
{
	return -ENOSYS;
}

static inline int efi_loge_locate_device_path(int ofs, efi_status_t efi_ret)
{
	return -ENOSYS;
}

static inline int efi_logs_close_protocol(efi_handle_t handle,
					  const efi_guid_t *protocol,
					  efi_handle_t agent_handle,
					  efi_handle_t controller_handle)
{
	return -ENOSYS;
}

static inline int efi_loge_close_protocol(int ofs, efi_status_t efi_ret)
{
	return -ENOSYS;
}

static inline int efi_logs_install_protocol_interface(efi_handle_t *handle,
						      const efi_guid_t *protocol,
						      int protocol_interface_type,
						      void *protocol_interface)
{
	return -ENOSYS;
}

static inline int efi_loge_install_protocol_interface(int ofs, efi_status_t efi_ret)
{
	return -ENOSYS;
}

static inline int efi_logs_uninstall_protocol_interface(efi_handle_t handle,
							const efi_guid_t *protocol,
							void *protocol_interface)
{
	return -ENOSYS;
}

static inline int efi_loge_uninstall_protocol_interface(int ofs, efi_status_t efi_ret)
{
	return -ENOSYS;
}

static inline int efi_logs_reinstall_protocol_interface(efi_handle_t handle,
							const efi_guid_t *protocol,
							void *old_interface,
							void *new_interface)
{
	return -ENOSYS;
}

static inline int efi_loge_reinstall_protocol_interface(int ofs, efi_status_t efi_ret)
{
	return -ENOSYS;
}

static inline int efi_logs_register_protocol_notify(const efi_guid_t *protocol,
						    struct efi_event *event,
						    void **registration)
{
	return -ENOSYS;
}

static inline int efi_loge_register_protocol_notify(int ofs, efi_status_t efi_ret)
{
	return -ENOSYS;
}

static inline int efi_logs_open_protocol_information(efi_handle_t handle,
						     const efi_guid_t *protocol,
						     efi_uintn_t *entry_count)
{
	return -ENOSYS;
}

static inline int efi_loge_open_protocol_information(int ofs, efi_status_t efi_ret)
{
	return -ENOSYS;
}

static inline int efi_logs_protocols_per_handle(efi_handle_t handle,
						efi_uintn_t *protocol_buffer_count)
{
	return -ENOSYS;
}

static inline int efi_loge_protocols_per_handle(int ofs, efi_status_t efi_ret)
{
	return -ENOSYS;
}

static inline int efi_logs_load_image(bool boot_policy,
				      efi_handle_t parent_image,
				      void *source_buffer,
				      efi_uintn_t source_size,
				      efi_handle_t *image_handle)
{
	return -ENOSYS;
}

static inline int efi_loge_load_image(int ofs, efi_status_t efi_ret)
{
	return -ENOSYS;
}

static inline int efi_logs_exit_boot_services(efi_handle_t image_handle,
					      efi_uintn_t map_key)
{
	return -ENOSYS;
}

static inline int efi_loge_exit_boot_services(int ofs, efi_status_t efi_ret)
{
	return -ENOSYS;
}

static inline int efi_logs_locate_protocol(const efi_guid_t *protocol,
					   void *registration, void **interface)
{
	return -ENOSYS;
}

static inline int efi_loge_locate_protocol(int ofs, efi_status_t efi_ret)
{
	return -ENOSYS;
}

static inline int efi_logs_open_protocol(efi_handle_t handle,
					 const efi_guid_t *protocol,
					 void **interface,
					 efi_handle_t agent_handle,
					 efi_handle_t controller_handle,
					 u32 attributes)
{
	return -ENOSYS;
}

static inline int efi_loge_open_protocol(int ofs, efi_status_t efi_ret)
{
	return -ENOSYS;
}

static inline int efi_logs_allocate_pages(enum efi_allocate_type type,
					  enum efi_memory_type memory_type,
					  efi_uintn_t pages, u64 *memory)
{
	return -ENOSYS;
}

static inline int efi_loge_allocate_pages(int ofs, efi_status_t efi_ret)
{
	return -ENOSYS;
}

static inline int efi_logs_free_pages(u64 memory, efi_uintn_t pages)
{
	return -ENOSYS;
}

static inline int efi_loge_free_pages(int ofs, efi_status_t efi_ret)
{
	return -ENOSYS;
}

static inline int efi_logs_allocate_pool(enum efi_memory_type pool_type,
					 efi_uintn_t size, void **buffer)
{
	return -ENOSYS;
}

static inline int efi_loge_allocate_pool(int ofs, efi_status_t efi_ret)
{
	return -ENOSYS;
}

static inline int efi_logs_free_pool(void *buffer)
{
	return -ENOSYS;
}

static inline int efi_loge_free_pool(int ofs, efi_status_t efi_ret)
{
	return -ENOSYS;
}

static inline int efi_logs_testing(enum efil_test_t enum_val,
				   efi_uintn_t int_value, void *buffer,
				   u64 *memory)
{
	return -ENOSYS;
}

static inline int efi_loge_testing(int ofs, efi_status_t efi_ret)
{
	return -ENOSYS;
}

static inline int efi_logs_install_configuration_table(const efi_guid_t *guid,
						       void *table)
{
	return 0;
}

static inline int efi_loge_install_configuration_table(int ofs,
						       efi_status_t efi_ret)
{
	return 0;
}

static inline int efi_logs_get_next_monotonic_count(u64 *count)
{
	return 0;
}

static inline int efi_loge_get_next_monotonic_count(int ofs,
						    efi_status_t efi_ret)
{
	return 0;
}

static inline int efi_logs_stall(u64 microseconds)
{
	return 0;
}

static inline int efi_loge_stall(int ofs, efi_status_t efi_ret)
{
	return 0;
}

static inline int efi_logs_set_watchdog_timer(u64 timeout, u64 watchdog_code,
					      u64 data_size, u16 *watchdog_data)
{
	return 0;
}

static inline int efi_loge_set_watchdog_timer(int ofs, efi_status_t efi_ret)
{
	return 0;
}

static inline int efi_logs_calculate_crc32(const void *data,
					   efi_uintn_t data_size, u32 *crc32_p)
{
	return 0;
}

static inline int efi_loge_calculate_crc32(int ofs, efi_status_t efi_ret)
{
	return 0;
}

static inline int efi_logs_call(enum efil_prot prot, uint method, uint nargs,
				...)
{
	return 0;
}

static inline int efi_loge_call(int ofs, efi_status_t efi_ret, u64 e_arg)
{
	return 0;
}

#endif /* EFI_LOG */

/* below are some general functions */

/**
 * efi_log_show() - Show the EFI log
 *
 * Displays the log of EFI boot-services calls which are so-far enabled for
 * logging
 *
 * Return: 0 on success, or -ve error code
 */
int efi_log_show(void);

/**
 * efi_log_summary() - Show a summary of the EFI log
 *
 * Prints how many calls were made, how many failed and how many are still
 * pending, then a count for each type of call. Prints nothing if there is no
 * log or it holds no records.
 */
#if CONFIG_IS_ENABLED(EFI_LOG)
void efi_log_summary(void);
#else
static inline void efi_log_summary(void) {}
#endif

/**
 * efi_log_reset() - Reset the log, erasing all records
 *
 * Return 0 if OK, -ENOENT if the log could not be found

 */
int efi_log_reset(void);

/**
 * efi_log_init() - Create a log in the bloblist, then reset it
 *
 * Return 0 if OK, -ENOMEM if the bloblist is not large enough
 */
int efi_log_init(void);

#endif /* __EFI_LOG_H */
