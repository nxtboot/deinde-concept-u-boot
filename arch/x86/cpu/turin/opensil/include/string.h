/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * The string functions openSIL uses, which U-Boot provides
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#ifndef __OPENSIL_STRING_H
#define __OPENSIL_STRING_H

#include <stddef.h>

void *memcpy(void *dest, const void *src, size_t count);
void *memset(void *s, int c, size_t count);

#endif
