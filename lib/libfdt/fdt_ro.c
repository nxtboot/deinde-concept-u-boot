#include <linux/kconfig.h>
#include <linux/libfdt_env.h>

/*
 * Ask libfdt to find a node's parent in a single pass, tracking up to this
 * depth. No real devicetree comes close to 16; a deeper one still works, by
 * searching the tree twice as before. Zero leaves the faster code out
 */
#if CONFIG_IS_ENABLED(OF_PRERELOC_FAST)
#define FDT_PARENT_MAX_DEPTH	16
#else
#define FDT_PARENT_MAX_DEPTH	0
#endif

#include "../../scripts/dtc/libfdt/fdt_ro.c"
