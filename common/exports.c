#include <command.h>
#include <exports.h>
#include <malloc.h>
#include <spi.h>
#include <i2c.h>
#include <asm/global_data.h>

DECLARE_GLOBAL_DATA_PTR;

unsigned long get_version(void)
{
	return XF_VERSION;
}

int jumptable_init(void)
{
	gd->jt = malloc(sizeof(struct jt_funcs));

	/*
	 * <malloc.h> -> <malloc_old.h> installs object-like macros such as
	 *   #define calloc dlcalloc
	 *   #define realloc dlrealloc
	 * for sandbox builds (USE_DL_PREFIX).  Those rewrite every later use
	 * of the identifier, including the struct-field name position in
	 * EXPORT_FUNC below, which does not match struct jt_funcs (declared
	 * before the defines were in scope).  Drop the hijacks for the
	 * fields we are about to assign; _exports.h names the dl* symbols
	 * explicitly as the implementation side when USE_DL_PREFIX is set.
	 * The function-like malloc(x) / free(ptr) macros only substitute on
	 * call expressions, so they can stay put.
	 */
#undef calloc
#undef realloc
#undef memalign
#undef valloc
#undef pvalloc
#undef mallinfo
#undef mallopt
#undef malloc_trim
#undef malloc_usable_size
#undef malloc_stats

#define EXPORT_FUNC(f, a, x, ...)  gd->jt->x = f;
#include <_exports.h>

	return 0;
}
