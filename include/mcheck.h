/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 1996-2024 Free Software Foundation, Inc.
 * This file is part of the GNU C Library.
 */
#ifndef _MCHECK_H
#define _MCHECK_H       1

/*
 * Return values for `mprobe': these are the kinds of inconsistencies that
 * `mcheck' enables detection of.
 */
enum mcheck_status {
	MCHECK_DISABLED = -1,         /* Consistency checking is not turned on.  */
	MCHECK_OK,                    /* Block is fine.  */
	MCHECK_FREE,                  /* Block freed twice.  */
	MCHECK_HEAD,                  /* Memory before the block was clobbered.  */
	MCHECK_TAIL                   /* Memory after the block was clobbered.  */
};

typedef void (*mcheck_abortfunc_t)(enum mcheck_status, const void *p);

int mcheck(mcheck_abortfunc_t func);

/*
 * Similar to `mcheck' but performs checks for all block whenever one of
 * the memory handling functions is called.  This can be very slow.
 */
int mcheck_pedantic(mcheck_abortfunc_t f);

/* Force check of all blocks now.  */
void mcheck_check_all(void);

/*
 * Check for aberrations in a particular malloc'd block. These are the
 * same checks that `mcheck' does, when you free or reallocate a block.
 */
enum mcheck_status mprobe(void *__ptr);

/* Called during RAM relocation to reset the heap registry */
void mcheck_on_ramrelocation(size_t offset);

/**
 * mcheck_set_disabled() - Disable mcheck at runtime
 *
 * When disabled, mcheck wrapper functions pass through directly to the
 * underlying allocator without adding headers or checking for corruption.
 * This is useful for debugging when mcheck interferes with test results.
 *
 * @disabled: true to disable mcheck, false to enable
 */
#if !CONFIG_IS_ENABLED(SYS_MALLOC_LEGACY)
void mcheck_set_disabled(bool disabled);
#else
static inline void mcheck_set_disabled(bool disabled) {}
#endif

#endif
