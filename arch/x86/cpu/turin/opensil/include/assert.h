/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * assert() for openSIL, using U-Boot's __assert_fail()
 *
 * openSIL is built outside U-Boot's own build, with only this directory and
 * the compiler's own headers on its include path, so it gets the few C
 * library declarations it needs from here.
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#ifndef __OPENSIL_ASSERT_H
#define __OPENSIL_ASSERT_H

void __assert_fail(const char *assertion, const char *file, unsigned int line,
		   const char *function);

#define assert(x) \
	({ if (!(x)) __assert_fail(#x, __FILE__, __LINE__, __func__); })

#endif
