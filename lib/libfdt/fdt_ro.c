#include <linux/kconfig.h>
#include <linux/libfdt_env.h>
#include <libfdt.h>

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

/*
 * Measure how often U-Boot looks up a node's parent. The implementation is
 * imported from dtc, so rather than change it, rename it as it is included
 * and give the counting wrapper below the original name. Every caller then
 * reaches the wrapper without knowing, including those in other parts of
 * libfdt, such as fdt_overlay.c
 *
 * libfdt.h is included first so that it still declares the original name,
 * which gives the wrapper its prototype
 */
#define fdt_parent_offset fdt_parent_offset_
#include "../../scripts/dtc/libfdt/fdt_ro.c"
#undef fdt_parent_offset

#include <bootstage.h>
#include <stdio.h>
#include <linux/compiler.h>
#include <asm/global_data.h>

DECLARE_GLOBAL_DATA_PTR;

/*
 * Record where the pre-relocation lookups come from, so that the callers
 * which are worth optimising can be identified. Diagnostic only
 */
static ulong fdt_parent_caller[FDT_PARENT_CALLERS] __section(".data");
static uint fdt_parent_caller_cnt __section(".data");

void fdt_parent_report(void)
{
	uint i;

	if (!fdt_parent_caller_cnt)
		return;
	printf("\nPre-reloc parent lookups from:\n");
	for (i = 0; i < fdt_parent_caller_cnt; i++)
		printf("    %lx\n", fdt_parent_caller[i]);
}

int fdt_parent_offset(const void *fdt, int nodeoffset)
{
	enum bootstage_id id;
	const char *name;
	bool reloc;
	int ret;

	/*
	 * Count the lookups done before and after relocation separately. The
	 * caches are only enabled once U-Boot has relocated, so the same walk
	 * costs very different amounts in each case
	 */
	reloc = gd && (gd->flags & GD_FLG_RELOC);
	id = reloc ? BOOTSTAGE_ID_ACCUM_DT_PARENT_R :
		BOOTSTAGE_ID_ACCUM_DT_PARENT;
	name = reloc ? "dt_parent_r" : "dt_parent_f";

	if (IS_ENABLED(CONFIG_BOOTSTAGE_ACCUM_DT)) {
		if (!reloc && fdt_parent_caller_cnt < FDT_PARENT_CALLERS)
			fdt_parent_caller[fdt_parent_caller_cnt++] =
				(ulong)__builtin_return_address(0);
		bootstage_start(id, name);
	}
	ret = fdt_parent_offset_(fdt, nodeoffset);
	if (IS_ENABLED(CONFIG_BOOTSTAGE_ACCUM_DT))
		bootstage_accum(id);

	return ret;
}
