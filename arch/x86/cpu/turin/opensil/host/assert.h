/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * The glue in opensil.c and any other U-Boot code which includes openSIL's
 * headers gets U-Boot's own assert() from here, not the one in ../include
 * which the library is built with
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <log.h>
