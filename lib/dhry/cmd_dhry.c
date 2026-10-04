// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2015 Google, Inc
 */

#include <command.h>
#include <div64.h>
#include <getopt.h>
#include <time.h>
#include <vsprintf.h>
#include "dhry.h"

static int do_dhry(struct getopt_state *gs)
{
	ulong start, duration, vax_mips;
	u64 dhry_per_sec;
	int iterations = 1000000;
	const char *arg;

	arg = getopt_pop(gs);
	if (arg)
		iterations = dectoul(arg, NULL);

	start = get_timer(0);
	dhry(iterations);
	duration = get_timer(start);
	if (!duration) {
		printf("%d iterations take under 1ms: try more\n", iterations);
		return CMD_RET_FAILURE;
	}
	dhry_per_sec = lldiv(iterations * 1000ULL, duration);
	vax_mips = lldiv(dhry_per_sec, 1757);
	printf("%d iterations in %lu ms: %lu/s, %lu DMIPS\n", iterations,
	       duration, (ulong)dhry_per_sec, vax_mips);

	return 0;
}

U_BOOT_CMD_NOOPTS(
	dhry,	2,	1,	do_dhry,
	"[iterations] - run dhrystone benchmark",
	"\n    - run the Dhrystone 2.1 benchmark, a rough measure of CPU speed\n"
);
