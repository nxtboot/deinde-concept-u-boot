/* SPDX-License-Identifier: GPL-2.0+ */
/*
  Default header file for malloc-2.8.x, written by Doug Lea
  and released to the public domain, as explained at
  http://creativecommons.org/publicdomain/zero/1.0/ 
 
  This header is for ANSI C/C++ only.  You can set any of
  the following #defines before including:

  * If USE_DL_PREFIX is defined, it is assumed that malloc.c 
    was also compiled with this option, so all routines
    have names starting with "dl".

  * If HAVE_USR_INCLUDE_MALLOC_H is defined, it is assumed that this
    file will be #included AFTER <malloc.h>. This is needed only if
    your system defines a struct mallinfo that is incompatible with the
    standard one declared here.  Otherwise, you can include this file
    INSTEAD of your system system <malloc.h>.  At least on ANSI, all
    declarations should be compatible with system versions

  * If MSPACES is defined, declarations for mspace versions are included.
*/

#ifdef CONFIG_SYS_MALLOC_LEGACY

#include <malloc_old.h>

#else

#ifndef MALLOC_280_H
#define MALLOC_280_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>   /* for size_t */

#ifndef ONLY_MSPACES
#define ONLY_MSPACES 0     /* define to a value */
#elif ONLY_MSPACES != 0
#define ONLY_MSPACES 1
#endif  /* ONLY_MSPACES */
#ifndef NO_MALLINFO
#define NO_MALLINFO 0
#endif  /* NO_MALLINFO */

#ifndef MSPACES
#if ONLY_MSPACES
#define MSPACES 1
#else   /* ONLY_MSPACES */
#define MSPACES 0
#endif  /* ONLY_MSPACES */
#endif  /* MSPACES */

#if !ONLY_MSPACES

/*
 * Rename the U-Boot alloc functions so that sandbox can still use the system
 * ones
 */
#ifdef CONFIG_SANDBOX
#define USE_DL_PREFIX
#endif

/*
 * When using simple malloc (SPL/TPL), redirect to simple implementations.
 * Skip this when compiling dlmalloc.c itself to avoid conflicts.
 */
#if CONFIG_IS_ENABLED(SYS_MALLOC_SIMPLE) && !defined(COMPILING_DLMALLOC)
#define malloc malloc_simple
#define realloc realloc_simple
#define calloc calloc_simple
#define memalign memalign_simple
#if IS_ENABLED(CONFIG_VALGRIND)
#define free free_simple
#else
static inline void free(void *ptr) {}
#endif
void *calloc(size_t nmemb, size_t size);
void *realloc_simple(void *ptr, size_t size);
#else /* !SYS_MALLOC_SIMPLE || COMPILING_DLMALLOC */

#ifndef USE_DL_PREFIX
#define dlcalloc               calloc
#define dlfree                 free
#define dlmalloc               malloc
#define dlmemalign             memalign
#define dlposix_memalign       posix_memalign
#define dlrealloc              realloc
#define dlvalloc               valloc
#define dlpvalloc              pvalloc
#define dlmallinfo             mallinfo
#define dlmallopt              mallopt
#define dlmalloc_trim          malloc_trim
#define dlmalloc_stats         malloc_stats
#define dlmalloc_usable_size   malloc_usable_size
#define dlmalloc_footprint     malloc_footprint
#define dlmalloc_max_footprint malloc_max_footprint
#define dlmalloc_footprint_limit malloc_footprint_limit
#define dlmalloc_set_footprint_limit malloc_set_footprint_limit
#define dlmalloc_inspect_all   malloc_inspect_all
#define dlindependent_calloc   independent_calloc
#define dlindependent_comalloc independent_comalloc
#define dlbulk_free            bulk_free
#else /* USE_DL_PREFIX */
/* Ensure that U-Boot actually uses dlmalloc versions */
#define calloc(n, s)           dlcalloc(n, s)
#define free(p)                dlfree(p)
#define malloc(s)              dlmalloc(s)
#define memalign(a, s)         dlmemalign(a, s)
#define posix_memalign(p, a, s) dlposix_memalign(p, a, s)
#define realloc(p, s)          dlrealloc(p, s)
#define valloc(s)              dlvalloc(s)
#define pvalloc(s)             dlpvalloc(s)
#define mallinfo()             dlmallinfo()
#define mallopt(p, v)          dlmallopt(p, v)
#define malloc_trim(s)         dlmalloc_trim(s)
#define malloc_stats()         dlmalloc_stats()
#define malloc_usable_size(p)  dlmalloc_usable_size(p)
#endif /* USE_DL_PREFIX */
#endif /* !SYS_MALLOC_SIMPLE || COMPILING_DLMALLOC */

#if !NO_MALLINFO 
#ifndef HAVE_USR_INCLUDE_MALLOC_H
#ifndef _MALLOC_H
#ifndef MALLINFO_FIELD_TYPE
#define MALLINFO_FIELD_TYPE size_t
#endif /* MALLINFO_FIELD_TYPE */
#ifndef STRUCT_MALLINFO_DECLARED
#define STRUCT_MALLINFO_DECLARED 1
struct mallinfo {
  MALLINFO_FIELD_TYPE arena;    /* non-mmapped space allocated from system */
  MALLINFO_FIELD_TYPE ordblks;  /* number of free chunks */
  MALLINFO_FIELD_TYPE smblks;   /* always 0 */
  MALLINFO_FIELD_TYPE hblks;    /* always 0 */
  MALLINFO_FIELD_TYPE hblkhd;   /* space in mmapped regions */
  MALLINFO_FIELD_TYPE usmblks;  /* maximum total allocated space */
  MALLINFO_FIELD_TYPE fsmblks;  /* always 0 */
  MALLINFO_FIELD_TYPE uordblks; /* total allocated space */
  MALLINFO_FIELD_TYPE fordblks; /* total free space */
  MALLINFO_FIELD_TYPE keepcost; /* releasable (via malloc_trim) space */
};
#endif /* STRUCT_MALLINFO_DECLARED */
#endif  /* _MALLOC_H */
#endif  /* HAVE_USR_INCLUDE_MALLOC_H */
#endif  /* !NO_MALLINFO */

/**
 * struct malloc_leak_snap - Snapshot of heap allocations for leak checking
 *
 * Used by malloc_leak_check_start/end to record the set of in-use chunk
 * addresses before a test, so that new (leaked) allocations can be identified
 * afterwards.
 *
 * @addr: Array of chunk addresses
 * @count: Number of entries in @addr
 */
struct malloc_leak_snap {
	unsigned long *addr;
	int count;
};

/*
  malloc(size_t n)
  Returns a pointer to a newly allocated chunk of at least n bytes, or
  null if no space is available, in which case errno is set to ENOMEM
  on ANSI C systems.

  If n is zero, malloc returns a minimum-sized chunk. (The minimum
  size is 16 bytes on most 32bit systems, and 32 bytes on 64bit
  systems.)  Note that size_t is an unsigned type, so calls with
  arguments that would be negative if signed are interpreted as
  requests for huge amounts of space, which will often fail. The
  maximum supported value of n differs across systems, but is in all
  cases less than the maximum representable value of a size_t.
*/
void* dlmalloc(size_t);

/*
  free(void* p)
  Releases the chunk of memory pointed to by p, that had been previously
  allocated using malloc or a related routine such as realloc.
  It has no effect if p is null. If p was not malloced or already
  freed, free(p) will by default cuase the current program to abort.
*/
void  dlfree(void*);

/*
  calloc(size_t n_elements, size_t element_size);
  Returns a pointer to n_elements * element_size bytes, with all locations
  set to zero.
*/
void* dlcalloc(size_t, size_t);

/*
  realloc(void* p, size_t n)
  Returns a pointer to a chunk of size n that contains the same data
  as does chunk p up to the minimum of (n, p's size) bytes, or null
  if no space is available.

  The returned pointer may or may not be the same as p. The algorithm
  prefers extending p in most cases when possible, otherwise it
  employs the equivalent of a malloc-copy-free sequence.

  If p is null, realloc is equivalent to malloc.

  If space is not available, realloc returns null, errno is set (if on
  ANSI) and p is NOT freed.

  if n is for fewer bytes than already held by p, the newly unused
  space is lopped off and freed if possible.  realloc with a size
  argument of zero (re)allocates a minimum-sized chunk.

  The old unix realloc convention of allowing the last-free'd chunk
  to be used as an argument to realloc is not supported.
*/
void* dlrealloc(void*, size_t);

/*
  realloc_in_place(void* p, size_t n)
  Resizes the space allocated for p to size n, only if this can be
  done without moving p (i.e., only if there is adjacent space
  available if n is greater than p's current allocated size, or n is
  less than or equal to p's size). This may be used instead of plain
  realloc if an alternative allocation strategy is needed upon failure
  to expand space; for example, reallocation of a buffer that must be
  memory-aligned or cleared. You can use realloc_in_place to trigger
  these alternatives only when needed.

  Returns p if successful; otherwise null.
*/
void* dlrealloc_in_place(void*, size_t);

/*
  memalign(size_t alignment, size_t n);
  Returns a pointer to a newly allocated chunk of n bytes, aligned
  in accord with the alignment argument.

  The alignment argument should be a power of two. If the argument is
  not a power of two, the nearest greater power is used.
  8-byte alignment is guaranteed by normal malloc calls, so don't
  bother calling memalign with an argument of 8 or less.

  Overreliance on memalign is a sure way to fragment space.
*/
void* dlmemalign(size_t, size_t);

/*
  int posix_memalign(void** pp, size_t alignment, size_t n);
  Allocates a chunk of n bytes, aligned in accord with the alignment
  argument. Differs from memalign only in that it (1) assigns the
  allocated memory to *pp rather than returning it, (2) fails and
  returns EINVAL if the alignment is not a power of two (3) fails and
  returns ENOMEM if memory cannot be allocated.
*/
int dlposix_memalign(void**, size_t, size_t);

/*
  valloc(size_t n);
  Equivalent to memalign(pagesize, n), where pagesize is the page
  size of the system. If the pagesize is unknown, 4096 is used.
*/
void* dlvalloc(size_t);

/*
  mallopt(int parameter_number, int parameter_value)
  Sets tunable parameters The format is to provide a
  (parameter-number, parameter-value) pair.  mallopt then sets the
  corresponding parameter to the argument value if it can (i.e., so
  long as the value is meaningful), and returns 1 if successful else
  0.  SVID/XPG/ANSI defines four standard param numbers for mallopt,
  normally defined in malloc.h.  None of these are use in this malloc,
  so setting them has no effect. But this malloc also supports other
  options in mallopt:

  Symbol            param #  default    allowed param values
  M_TRIM_THRESHOLD     -1   2*1024*1024   any   (-1U disables trimming)
  M_GRANULARITY        -2     page size   any power of 2 >= page size
  M_MMAP_THRESHOLD     -3      256*1024   any   (or 0 if no MMAP support)
*/
int dlmallopt(int, int);

#define M_TRIM_THRESHOLD     (-1)
#define M_GRANULARITY        (-2)
#define M_MMAP_THRESHOLD     (-3)


/*
  malloc_footprint();
  Returns the number of bytes obtained from the system.  The total
  number of bytes allocated by malloc, realloc etc., is less than this
  value. Unlike mallinfo, this function returns only a precomputed
  result, so can be called frequently to monitor memory consumption.
  Even if locks are otherwise defined, this function does not use them,
  so results might not be up to date.
*/
size_t dlmalloc_footprint(void);

/*
  malloc_max_footprint();
  Returns the maximum number of bytes obtained from the system. This
  value will be greater than current footprint if deallocated space
  has been reclaimed by the system. The peak number of bytes allocated
  by malloc, realloc etc., is less than this value. Unlike mallinfo,
  this function returns only a precomputed result, so can be called
  frequently to monitor memory consumption.  Even if locks are
  otherwise defined, this function does not use them, so results might
  not be up to date.
*/
size_t dlmalloc_max_footprint(void);

/*
  malloc_footprint_limit();
  Returns the number of bytes that the heap is allowed to obtain from
  the system, returning the last value returned by
  malloc_set_footprint_limit, or the maximum size_t value if
  never set. The returned value reflects a permission. There is no
  guarantee that this number of bytes can actually be obtained from
  the system.  
*/
size_t dlmalloc_footprint_limit(void);

/*
  malloc_set_footprint_limit();
  Sets the maximum number of bytes to obtain from the system, causing
  failure returns from malloc and related functions upon attempts to
  exceed this value. The argument value may be subject to page
  rounding to an enforceable limit; this actual value is returned.
  Using an argument of the maximum possible size_t effectively
  disables checks. If the argument is less than or equal to the
  current malloc_footprint, then all future allocations that require
  additional system memory will fail. However, invocation cannot
  retroactively deallocate existing used memory.
*/
size_t dlmalloc_set_footprint_limit(size_t bytes);

/*
  malloc_inspect_all(void(*handler)(void *start,
                                    void *end,
                                    size_t used_bytes,
                                    void* callback_arg),
                      void* arg);
  Traverses the heap and calls the given handler for each managed
  region, skipping all bytes that are (or may be) used for bookkeeping
  purposes.  Traversal does not include include chunks that have been
  directly memory mapped. Each reported region begins at the start
  address, and continues up to but not including the end address.  The
  first used_bytes of the region contain allocated data. If
  used_bytes is zero, the region is unallocated. The handler is
  invoked with the given callback argument. If locks are defined, they
  are held during the entire traversal. It is a bad idea to invoke
  other malloc functions from within the handler.

  For example, to count the number of in-use chunks with size greater
  than 1000, you could write:
  static int count = 0;
  void count_chunks(void* start, void* end, size_t used, void* arg) {
    if (used >= 1000) ++count;
  }
  then:
    malloc_inspect_all(count_chunks, NULL);

  malloc_inspect_all is compiled only if MALLOC_INSPECT_ALL is defined.
*/
void dlmalloc_inspect_all(void(*handler)(void*, void *, size_t, void*),
                           void* arg);

#if !NO_MALLINFO
/*
  mallinfo()
  Returns (by copy) a struct containing various summary statistics:

  arena:     current total non-mmapped bytes allocated from system
  ordblks:   the number of free chunks
  smblks:    always zero.
  hblks:     current number of mmapped regions
  hblkhd:    total bytes held in mmapped regions
  usmblks:   the maximum total allocated space. This will be greater
                than current total if trimming has occurred.
  fsmblks:   always zero
  uordblks:  current total allocated space (normal or mmapped)
  fordblks:  total free space
  keepcost:  the maximum number of bytes that could ideally be released
               back to system via malloc_trim. ("ideally" means that
               it ignores page restrictions etc.)

  Because these fields are ints, but internal bookkeeping may
  be kept as longs, the reported values may wrap around zero and
  thus be inaccurate.
*/

struct mallinfo dlmallinfo(void);
#endif  /* NO_MALLINFO */

/*
  independent_calloc(size_t n_elements, size_t element_size, void* chunks[]);

  independent_calloc is similar to calloc, but instead of returning a
  single cleared space, it returns an array of pointers to n_elements
  independent elements that can hold contents of size elem_size, each
  of which starts out cleared, and can be independently freed,
  realloc'ed etc. The elements are guaranteed to be adjacently
  allocated (this is not guaranteed to occur with multiple callocs or
  mallocs), which may also improve cache locality in some
  applications.

  The "chunks" argument is optional (i.e., may be null, which is
  probably the most typical usage). If it is null, the returned array
  is itself dynamically allocated and should also be freed when it is
  no longer needed. Otherwise, the chunks array must be of at least
  n_elements in length. It is filled in with the pointers to the
  chunks.

  In either case, independent_calloc returns this pointer array, or
  null if the allocation failed.  If n_elements is zero and "chunks"
  is null, it returns a chunk representing an array with zero elements
  (which should be freed if not wanted).

  Each element must be freed when it is no longer needed. This can be
  done all at once using bulk_free.

  independent_calloc simplifies and speeds up implementations of many
  kinds of pools.  It may also be useful when constructing large data
  structures that initially have a fixed number of fixed-sized nodes,
  but the number is not known at compile time, and some of the nodes
  may later need to be freed. For example:

  struct Node { int item; struct Node* next; };

  struct Node* build_list() {
    struct Node** pool;
    int n = read_number_of_nodes_needed();
    if (n <= 0) return 0;
    pool = (struct Node**)(independent_calloc(n, sizeof(struct Node), 0);
    if (pool == 0) die();
    // organize into a linked list...
    struct Node* first = pool[0];
    for (i = 0; i < n-1; ++i)
      pool[i]->next = pool[i+1];
    free(pool);     // Can now free the array (or not, if it is needed later)
    return first;
  }
*/
void** dlindependent_calloc(size_t, size_t, void**);

/*
  independent_comalloc(size_t n_elements, size_t sizes[], void* chunks[]);

  independent_comalloc allocates, all at once, a set of n_elements
  chunks with sizes indicated in the "sizes" array.    It returns
  an array of pointers to these elements, each of which can be
  independently freed, realloc'ed etc. The elements are guaranteed to
  be adjacently allocated (this is not guaranteed to occur with
  multiple callocs or mallocs), which may also improve cache locality
  in some applications.

  The "chunks" argument is optional (i.e., may be null). If it is null
  the returned array is itself dynamically allocated and should also
  be freed when it is no longer needed. Otherwise, the chunks array
  must be of at least n_elements in length. It is filled in with the
  pointers to the chunks.

  In either case, independent_comalloc returns this pointer array, or
  null if the allocation failed.  If n_elements is zero and chunks is
  null, it returns a chunk representing an array with zero elements
  (which should be freed if not wanted).

  Each element must be freed when it is no longer needed. This can be
  done all at once using bulk_free.

  independent_comallac differs from independent_calloc in that each
  element may have a different size, and also that it does not
  automatically clear elements.

  independent_comalloc can be used to speed up allocation in cases
  where several structs or objects must always be allocated at the
  same time.  For example:

  struct Head { ... }
  struct Foot { ... }

  void send_message(char* msg) {
    int msglen = strlen(msg);
    size_t sizes[3] = { sizeof(struct Head), msglen, sizeof(struct Foot) };
    void* chunks[3];
    if (independent_comalloc(3, sizes, chunks) == 0)
      die();
    struct Head* head = (struct Head*)(chunks[0]);
    char*        body = (char*)(chunks[1]);
    struct Foot* foot = (struct Foot*)(chunks[2]);
    // ...
  }

  In general though, independent_comalloc is worth using only for
  larger values of n_elements. For small values, you probably won't
  detect enough difference from series of malloc calls to bother.

  Overuse of independent_comalloc can increase overall memory usage,
  since it cannot reuse existing noncontiguous small chunks that
  might be available for some of the elements.
*/
void** dlindependent_comalloc(size_t, size_t*, void**);

/*
  bulk_free(void* array[], size_t n_elements)
  Frees and clears (sets to null) each non-null pointer in the given
  array.  This is likely to be faster than freeing them one-by-one.
  If footers are used, pointers that have been allocated in different
  mspaces are not freed or cleared, and the count of all such pointers
  is returned.  For large arrays of pointers with poor locality, it
  may be worthwhile to sort this array before calling bulk_free.
*/
size_t  dlbulk_free(void**, size_t n_elements);

/*
  pvalloc(size_t n);
  Equivalent to valloc(minimum-page-that-holds(n)), that is,
  round up n to nearest pagesize.
 */
void*  dlpvalloc(size_t);

/*
  malloc_trim(size_t pad);

  If possible, gives memory back to the system (via negative arguments
  to sbrk) if there is unused memory at the `high' end of the malloc
  pool or in unused MMAP segments. You can call this after freeing
  large blocks of memory to potentially reduce the system-level memory
  requirements of a program. However, it cannot guarantee to reduce
  memory. Under some allocation patterns, some large free blocks of
  memory will be locked between two used chunks, so they cannot be
  given back to the system.

  The `pad' argument to malloc_trim represents the amount of free
  trailing space to leave untrimmed. If this argument is zero, only
  the minimum amount of memory to maintain internal data structures
  will be left. Non-zero arguments can be supplied to maintain enough
  trailing space to service future expected allocations without having
  to re-obtain memory from the system.

  Malloc_trim returns 1 if it actually released any memory, else 0.
*/
int  dlmalloc_trim(size_t);

/*
  malloc_stats();
  Prints on stderr the amount of space obtained from the system (both
  via sbrk and mmap), the maximum amount (which may be more than
  current if malloc_trim and/or munmap got called), and the current
  number of bytes allocated via malloc (or realloc, etc) but not yet
  freed. Note that this is the number of bytes allocated, not the
  number requested. It will be larger than the number requested
  because of alignment and bookkeeping overhead. Because it includes
  alignment wastage as being in use, this figure may be greater than
  zero even when no user-level chunks are allocated.

  The reported current and maximum system memory can be inaccurate if
  a program makes other calls to system memory allocation functions
  (normally sbrk) outside of malloc.

  malloc_stats prints only the most commonly interesting statistics.
  More information can be obtained by calling mallinfo.
  
  malloc_stats is not compiled if NO_MALLOC_STATS is defined.
*/
void  dlmalloc_stats(void);

#endif /* !ONLY_MSPACES */

/*
  malloc_usable_size(void* p);

  Returns the number of bytes you can actually use in
  an allocated chunk, which may be more than you requested (although
  often not) due to alignment and minimum size constraints.
  You can use this many bytes without worrying about
  overwriting other allocated objects. This is not a particularly great
  programming practice. malloc_usable_size can be more useful in
  debugging and assertions, for example:

  p = malloc(n);
  assert(malloc_usable_size(p) >= 256);
*/
size_t dlmalloc_usable_size(const void*);

#if MSPACES

/*
  mspace is an opaque type representing an independent
  region of space that supports mspace_malloc, etc.
*/
typedef void* mspace;

/*
  create_mspace creates and returns a new independent space with the
  given initial capacity, or, if 0, the default granularity size.  It
  returns null if there is no system memory available to create the
  space.  If argument locked is non-zero, the space uses a separate
  lock to control access. The capacity of the space will grow
  dynamically as needed to service mspace_malloc requests.  You can
  control the sizes of incremental increases of this space by
  compiling with a different DEFAULT_GRANULARITY or dynamically
  setting with mallopt(M_GRANULARITY, value).
*/
mspace create_mspace(size_t capacity, int locked);

/*
  destroy_mspace destroys the given space, and attempts to return all
  of its memory back to the system, returning the total number of
  bytes freed. After destruction, the results of access to all memory
  used by the space become undefined.
*/
size_t destroy_mspace(mspace msp);

/*
  create_mspace_with_base uses the memory supplied as the initial base
  of a new mspace. Part (less than 128*sizeof(size_t) bytes) of this
  space is used for bookkeeping, so the capacity must be at least this
  large. (Otherwise 0 is returned.) When this initial space is
  exhausted, additional memory will be obtained from the system.
  Destroying this space will deallocate all additionally allocated
  space (if possible) but not the initial base.
*/
mspace create_mspace_with_base(void* base, size_t capacity, int locked);

/*
  mspace_track_large_chunks controls whether requests for large chunks
  are allocated in their own untracked mmapped regions, separate from
  others in this mspace. By default large chunks are not tracked,
  which reduces fragmentation. However, such chunks are not
  necessarily released to the system upon destroy_mspace.  Enabling
  tracking by setting to true may increase fragmentation, but avoids
  leakage when relying on destroy_mspace to release all memory
  allocated using this space.  The function returns the previous
  setting.
*/
int mspace_track_large_chunks(mspace msp, int enable);

#if !NO_MALLINFO
/*
  mspace_mallinfo behaves as mallinfo, but reports properties of
  the given space.
*/
struct mallinfo mspace_mallinfo(mspace msp);
#endif /* NO_MALLINFO */

/*
  An alias for mallopt.
*/
int mspace_mallopt(int, int);

/*
  The following operate identically to their malloc counterparts
  but operate only for the given mspace argument
*/
void* mspace_malloc(mspace msp, size_t bytes);
void mspace_free(mspace msp, void* mem);
void* mspace_calloc(mspace msp, size_t n_elements, size_t elem_size);
void* mspace_realloc(mspace msp, void* mem, size_t newsize);
void* mspace_realloc_in_place(mspace msp, void* mem, size_t newsize);
void* mspace_memalign(mspace msp, size_t alignment, size_t bytes);
void** mspace_independent_calloc(mspace msp, size_t n_elements,
                                 size_t elem_size, void* chunks[]);
void** mspace_independent_comalloc(mspace msp, size_t n_elements,
                                   size_t sizes[], void* chunks[]);
size_t mspace_bulk_free(mspace msp, void**, size_t n_elements);
size_t mspace_usable_size(const void* mem);
void mspace_malloc_stats(mspace msp);
int mspace_trim(mspace msp, size_t pad);
size_t mspace_footprint(mspace msp);
size_t mspace_max_footprint(mspace msp);
size_t mspace_footprint_limit(mspace msp);
size_t mspace_set_footprint_limit(mspace msp, size_t bytes);
void mspace_inspect_all(mspace msp, 
                        void(*handler)(void *, void *, size_t, void*),
                        void* arg);
#endif  /* MSPACES */

/* --------------------- U-Boot additions --------------------- */

#ifdef __UBOOT__
#include <linux/errno.h>
#include <linux/types.h>

/**
 * struct malloc_info - Memory allocation statistics
 *
 * This is filled in by malloc_get_info().
 *
 * @total_bytes: Total bytes available in the heap
 * @in_use_bytes: Current bytes allocated (in use by application)
 * @malloc_count: Number of calls to malloc()
 * @free_count: Number of calls to free()
 * @realloc_count: Number of calls to realloc()
 */
struct malloc_info {
	ulong total_bytes;
	ulong in_use_bytes;
	ulong malloc_count;
	ulong free_count;
	ulong realloc_count;
};

/* Memory pool boundaries */
extern ulong mem_malloc_start;
extern ulong mem_malloc_end;
extern ulong mem_malloc_brk;

/**
 * malloc_enable_testing() - Enable malloc failure testing
 *
 * @max_allocs: Number of allocations to allow before returning NULL
 */
void malloc_enable_testing(int max_allocs);

/**
 * malloc_disable_testing() - Disable malloc failure testing
 */
void malloc_disable_testing(void);

/**
 * malloc_dump() - Print a dump of all heap chunks
 *
 * Walks the dlmalloc heap from start to end, printing each chunk's
 * address, size, and status (used/free/top).
 */
void malloc_dump(void);

/**
 * malloc_log_start() - Start logging malloc traffic
 *
 * Enables recording of malloc/free/realloc calls to a circular buffer.
 * Use malloc_log_dump() to print the recorded entries.
 */
void malloc_log_start(void);

/**
 * malloc_log_stop() - Stop logging malloc traffic
 */
void malloc_log_stop(void);

/**
 * malloc_log_dump() - Dump the malloc traffic log
 *
 * Prints all recorded malloc/free/realloc calls with their addresses,
 * sizes, and caller information.
 */
void malloc_log_dump(void);

/**
 * malloc_log_to_file() - Write malloc()-traffic log to a host file
 *
 * @fname: Path to the output file on the host filesystem
 * Return: 0 on success, negative error code on failure
 *
 * This is only available in sandbox builds. It writes the same information
 * as malloc_log_dump() but to a file instead of the console.
 */
int malloc_log_to_file(const char *fname);

/**
 * enum mlog_type - Type of malloc log entry
 */
enum mlog_type {
	MLOG_ALLOC,
	MLOG_FREE,
	MLOG_REALLOC,
	MLOG_MEMALIGN,
};

#define MLOG_CALLER_LEN	128

/**
 * struct mlog_entry - Entry in the malloc traffic log
 *
 * @type: Operation type (alloc, free, realloc, memalign)
 * @ptr: Pointer returned by or passed to the operation
 * @size: Size of allocation (0 for free)
 * @old_size: Previous size for realloc, 0 otherwise
 * @caller: Backtrace string showing where the call originated
 */
struct mlog_entry {
	enum mlog_type type;
	void *ptr;
	size_t size;
	size_t old_size;
	char caller[MLOG_CALLER_LEN];
};

/**
 * struct mlog_info - Information about the malloc log
 *
 * @entry_count: Number of entries currently available in the log
 * @max_entries: Maximum number of entries the log can hold
 * @total_count: Total number of entries recorded (may exceed max if wrapped)
 */
struct mlog_info {
	uint entry_count;
	uint max_entries;
	uint total_count;
};

/**
 * malloc_log_info() - Get information about the malloc log
 *
 * @info: Returns log statistics
 * Return: 0 on success, -ENOENT if log not started
 */
int malloc_log_info(struct mlog_info *info);

/**
 * malloc_log_entry() - Get a specific entry from the malloc log
 *
 * @idx: Index of entry to retrieve (0 = oldest available)
 * @entryp: Returns pointer to the log entry
 * Return: 0 on success, -ENOENT if log not started, -ERANGE if idx out of range
 */
int malloc_log_entry(uint idx, struct mlog_entry **entryp);

/**
 * malloc_backtrace_skip() - Control backtrace collection in malloc
 *
 * When the stack is corrupted (e.g., by a stack overflow), collecting
 * a backtrace during malloc can crash. Use this function to disable
 * backtrace collection before corrupting the stack.
 *
 * @skip: true to skip backtrace collection, false to enable it
 */

/**
 * malloc_backtrace_unbusy() - Clear the backtrace reentrant guard
 *
 * The malloc backtrace collector sets a guard flag while collecting a
 * backtrace to prevent re-entrancy. If a crash or longjmp occurs during
 * collection, the guard stays set and all subsequent backtraces are
 * silently skipped. Call this to reset it.
 */

/**
 * malloc_backtrace_is_active() - Check whether backtrace collection works
 *
 * @skipp: If non-NULL, returns true if collection is disabled via
 *	malloc_backtrace_skip()
 * @busyp: If non-NULL, returns true if the reentrant guard is stuck
 * Return: true if backtrace collection is active (neither skipped nor busy)
 */
#if CONFIG_IS_ENABLED(MCHECK_HEAP_PROTECTION)
void malloc_backtrace_skip(bool skip);
void malloc_backtrace_unbusy(void);
bool malloc_backtrace_is_active(bool *skipp, bool *busyp);
#else
static inline void malloc_backtrace_skip(bool skip) {}
static inline void malloc_backtrace_unbusy(void) {}
static inline bool malloc_backtrace_is_active(bool *skipp, bool *busyp)
{
	if (skipp)
		*skipp = false;
	if (busyp)
		*busyp = false;

	return false;
}
#endif

/**
 * malloc_mcheck_overflow() - Check whether the mcheck registry filled
 *
 * The mcheck code registers each allocation's header in a fixed-size array so
 * that leak reporting and pedantic checking can find it. If the array is ever
 * exhausted, later allocations are still returned but their caller info cannot
 * be recovered.
 *
 * Return: true if the registry has overflowed at any point
 */
#if CONFIG_IS_ENABLED(MCHECK_HEAP_PROTECTION)
bool malloc_mcheck_overflow(void);
size_t malloc_mcheck_count(void);
#else
static inline bool malloc_mcheck_overflow(void) { return false; }
static inline size_t malloc_mcheck_count(void) { return 0; }
#endif

/**
 * malloc_chunk_size() - Return the dlmalloc chunk size for an allocation
 *
 * Given a pointer returned by malloc(), return the size of the underlying
 * dlmalloc chunk. This is the size shown in heap dumps and leak reports.
 *
 * @ptr: Pointer returned by malloc/calloc/realloc
 * Return: chunk size in bytes
 */
size_t malloc_chunk_size(void *ptr);

/**
 * malloc_largest_free() - Return the size of the largest free chunk
 *
 * Walks the heap and returns the largest contiguous free region that
 * malloc() could currently hand out, minus the per-chunk dlmalloc
 * overhead. Any mcheck header/canary overhead still comes off the top
 * of the caller's request. Useful for tests that want to allocate "as
 * much as possible" without tripping over fragmentation.
 *
 * Return: approximate largest request bytes malloc() would satisfy
 */
size_t malloc_largest_free(void);

/**
 * malloc_mcheck_hdr_size() - Return the size of the mcheck header
 *
 * When CONFIG_MCHECK_HEAP_PROTECTION is enabled, each allocation has a
 * header placed before the returned pointer. This returns its size, or
 * 0 when mcheck is disabled. This is useful for computing the chunk
 * address from a malloc pointer (chunk_addr = ptr - hdr_size).
 *
 * Return: size of struct mcheck_hdr, or 0
 */
size_t malloc_mcheck_hdr_size(void);

/**
 * malloc_leak_check_start() - Record current heap allocations
 *
 * Walk the heap and save the address of every in-use chunk. This snapshot
 * is later compared by malloc_leak_check_end() to find new allocations.
 * Storage is allocated with os_malloc() so it does not perturb the heap.
 *
 * @snap: Snapshot structure to fill in
 * Return: 0 on success, -ENOENT if heap not initialised, -ENOMEM on failure
 */
int malloc_leak_check_start(struct malloc_leak_snap *snap);

/**
 * malloc_leak_check_end() - Report allocations not present in the snapshot
 *
 * Walk the heap and print every in-use chunk whose address was not recorded
 * in the snapshot taken by malloc_leak_check_start(). Each leaked allocation
 * is printed with its address, size, and caller (when mcheck is enabled).
 * Frees the snapshot storage afterwards.
 *
 * @snap: Snapshot taken earlier with malloc_leak_check_start()
 * Return: number of leaked allocations, or -ve on error
 */
int malloc_leak_check_end(struct malloc_leak_snap *snap);

/**
 * malloc_leak_check_count() - Count allocations not present in the snapshot
 *
 * Like malloc_leak_check_end() but only counts, without printing or freeing
 * the snapshot.
 *
 * @snap: Snapshot taken earlier with malloc_leak_check_start()
 * Return: number of leaked allocations, or -ve on error
 */
int malloc_leak_check_count(struct malloc_leak_snap *snap);

/**
 * malloc_leak_check_free() - Free a snapshot without checking
 *
 * @snap: Snapshot taken earlier with malloc_leak_check_start()
 */
void malloc_leak_check_free(struct malloc_leak_snap *snap);

/**
 * mem_malloc_init() - Initialize the malloc() heap
 *
 * @start: Start address of heap memory region
 * @size: Size of heap memory region in bytes
 */
void mem_malloc_init(ulong start, ulong size);

/**
 * sbrk() - Extend the heap
 *
 * @increment: Number of bytes to add (or remove if negative)
 * Return: Previous break value on success, MORECORE_FAILURE on error
 */
void *sbrk(ptrdiff_t increment);

/**
 * malloc_simple() - Allocate memory from the simple malloc pool
 *
 * Allocates memory from a simple pool used before full malloc() is available.
 * This is used before relocation when BSS is not yet available for dlmalloc's
 * state. Memory allocated with this function cannot be freed.
 *
 * @size: Number of bytes to allocate
 * Return: Pointer to allocated memory, or NULL if pool is exhausted
 */
void *malloc_simple(size_t size);

/**
 * memalign_simple() - Allocate aligned memory from the simple malloc pool
 *
 * Allocates aligned memory from a simple pool used before full malloc() is
 * available. This is used before relocation when BSS is not yet available
 * for dlmalloc's state. Memory allocated with this function cannot be freed.
 *
 * @alignment: Required alignment (must be a power of 2)
 * @bytes: Number of bytes to allocate
 * Return: Pointer to allocated memory, or NULL if pool is exhausted
 */
void *memalign_simple(size_t alignment, size_t bytes);

/**
 * initf_malloc() - Set up the early malloc() pool
 *
 * Sets up the simple malloc() pool which is used before full malloc()
 * is available after relocation.
 *
 * Return: 0 (always succeeds)
 */
int initf_malloc(void);

/**
 * malloc_get_info() - Get memory allocation statistics
 *
 * @info: Place to put the statistics
 * Return: 0 on success, -ENOSYS if not available (MALLOC_DEBUG not enabled)
 */
#if CONFIG_IS_ENABLED(MALLOC_DEBUG)
int malloc_get_info(struct malloc_info *info);
#else
static inline int malloc_get_info(struct malloc_info *info)
{
	return -ENOSYS;
}
#endif

#endif /* __UBOOT__ */

#ifdef __cplusplus
};  /* end of extern "C" */
#endif

#endif /* MALLOC_280_H */

#endif /* !CONFIG_SYS_MALLOC_LEGACY */
