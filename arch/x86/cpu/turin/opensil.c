// SPDX-License-Identifier: GPL-2.0+
/*
 * Glue between U-Boot and AMD's openSIL library
 *
 * openSIL does the silicon initialisation which U-Boot's own code does now.
 * This is the start of using it: openSIL's messages go to U-Boot's log, and
 * the library reports how much memory it needs, which shows that it links and
 * can be called. Filling in its input blocks and calling its timepoints come
 * later.
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#define LOG_CATEGORY LOGC_ARCH

#include <errno.h>
#include <log.h>
#include <stdarg.h>
#include <vsprintf.h>
#include <asm/arch/opensil.h>

#ifdef OPENSIL_MISSING
/* The build found no openSIL tree, so U-Boot's own code does it all */
int turin_opensil_init(void)
{
	log_warning("openSIL: not built in\n");

	return 0;
}
#else
#include <xSIM-api.h>

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

int turin_opensil_init(void)
{
	SIL_STATUS ret;
	size_t size;

	ret = SilDebugSetup(opensil_debug);
	if (ret != SilPass)
		return log_msg_ret("dbg", -EINVAL);
	size = xSimQueryMemoryRequirements();
	log_info("openSIL: needs %zu bytes\n", size);

	return 0;
}
#endif
