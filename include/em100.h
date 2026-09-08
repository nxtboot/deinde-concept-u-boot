/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Message format for the terminal feature of the Dediprog EM100Pro
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * The emulator watches the SPI bus for a command which is not a flash
 * access, 0x11 by default, and treats what follows as a message for the
 * host. This is shared by the driver-model debug device and by the debug
 * UART, which sends messages before driver model is running.
 */

#ifndef __EM100_H
#define __EM100_H

#include <linux/types.h>

/* Format of a message sent to the EM100's uFIFO */
enum {
	/* signature marking the start of a message, sent little-endian */
	EM100_MSG_SIGNATURE	= 0x47364440,

	/* tells the EM100 to put what follows in the uFIFO */
	EM100_UFIFO_CMD		= 0xc0,

	/* message holds printable ASCII */
	EM100_TYPE_ASCII	= 0x05,

	/* the length field is a single byte */
	EM100_MAX_DATA		= 255,

	/* command the EM100 treats as a message, not a flash read */
	EM100_DEFAULT_CMD	= 0x11,

	/* reserved byte, uFIFO command, signature, type and length */
	EM100_HDR_LEN		= 8,
};

/**
 * em100_put_header() - Write the header which starts each message
 *
 * @buf: Buffer to write to, at least EM100_HDR_LEN bytes long
 * @len: Number of data bytes which will follow the header
 */
static inline void em100_put_header(u8 *buf, uint len)
{
	buf[0] = 0;			/* reserved */
	buf[1] = EM100_UFIFO_CMD;
	buf[2] = EM100_MSG_SIGNATURE & 0xff;
	buf[3] = (EM100_MSG_SIGNATURE >> 8) & 0xff;
	buf[4] = (EM100_MSG_SIGNATURE >> 16) & 0xff;
	buf[5] = (EM100_MSG_SIGNATURE >> 24) & 0xff;
	buf[6] = EM100_TYPE_ASCII;
	buf[7] = len;
}

#endif
