/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Reference count type stubs for U-Boot
 *
 * Based on Linux refcount.h - simplified for single-threaded U-Boot.
 */
#ifndef _LINUX_REFCOUNT_H
#define _LINUX_REFCOUNT_H

#include <asm/atomic.h>

/**
 * typedef refcount_t - reference count type
 *
 * Variant of atomic_t for reference counting with saturation semantics.
 * In U-Boot this is a simple wrapper around atomic_t.
 */
typedef struct {
	atomic_t refs;
} refcount_t;

#define REFCOUNT_INIT(n)	{ .refs = ATOMIC_INIT(n), }

/**
 * refcount_set() - set a refcount's value
 * @r: the refcount
 * @n: value to set
 */
#define refcount_set(r, n)	atomic_set(&(r)->refs, (n))

/**
 * refcount_read() - get a refcount's value
 * @r: the refcount
 *
 * Return: the refcount's value
 */
#define refcount_read(r)	atomic_read(&(r)->refs)

/**
 * refcount_inc() - increment a refcount
 * @r: the refcount to increment
 */
#define refcount_inc(r)		atomic_inc(&(r)->refs)

/**
 * refcount_dec() - decrement a refcount
 * @r: the refcount to decrement
 */
#define refcount_dec(r)		atomic_dec(&(r)->refs)

/**
 * refcount_dec_and_test() - decrement a refcount and test if it is 0
 * @r: the refcount
 *
 * Return: true if the resulting refcount is 0, false otherwise
 */
#define refcount_dec_and_test(r) atomic_dec_and_test(&(r)->refs)

/**
 * refcount_inc_not_zero() - increment a refcount unless it is 0
 * @r: the refcount to increment
 *
 * Return: true if the increment succeeded, false if refcount was 0
 */
static inline bool refcount_inc_not_zero(refcount_t *r)
{
	return atomic_add_unless(&r->refs, 1, 0);
}

#endif /* _LINUX_REFCOUNT_H */
