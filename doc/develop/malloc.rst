.. SPDX-License-Identifier: GPL-2.0-or-later

Dynamic Memory Allocation
=========================

U-Boot uses Doug Lea's malloc implementation (dlmalloc) for dynamic memory
allocation. This provides the standard C library functions malloc(), free(),
realloc(), calloc(), and memalign().

Overview
--------

U-Boot's malloc implementation has two phases:

1. **Pre-relocation (simple malloc)**: Before U-Boot relocates itself to the
   top of RAM, a simple malloc implementation is used. This allocates memory
   from a small fixed-size pool and does not support free(). This is
   controlled by CONFIG_SYS_MALLOC_F_LEN.

2. **Post-relocation (full malloc)**: After relocation, the full dlmalloc
   implementation is initialized with a larger heap. The heap size is
   controlled by CONFIG_SYS_MALLOC_LEN.

The transition between these phases is managed by the GD_FLG_FULL_MALLOC_INIT
flag in global_data.

dlmalloc Version
----------------

U-Boot uses dlmalloc version 2.8.6 (updated from 2.6.6 in 2025), which
provides:

- Efficient memory allocation with low fragmentation
- Small bins for allocations up to 256 bytes (32 bins)
- Tree bins for larger allocations (32 bins)
- Best-fit allocation strategy
- Boundary tags for coalescing free blocks

Data Structures
---------------

The allocator uses two main static structures:

**malloc_state** (~944 bytes on 64-bit systems):

- ``smallbins``: 33 pairs of pointers for small allocations (528 bytes)
- ``treebins``: 32 tree root pointers for large allocations (256 bytes)
- ``top``: Pointer to the top chunk (wilderness)
- ``dvsize``, ``topsize``: Sizes of designated victim and top chunks
- Bookkeeping: footprint tracking, bitmaps, segment info

**malloc_params** (48 bytes on 64-bit systems):

- Page size, granularity, thresholds for mmap and trim

For comparison, the older dlmalloc 2.6.6 used a single 2064-byte ``av_`` array
on 64-bit systems. The 2.8.6 version uses about half the static data while
providing better algorithms.

Kconfig Options
---------------

Main U-Boot (post-relocation)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``CONFIG_SYS_MALLOC_LEN``
    Hex value defining the size of the main malloc pool after relocation.
    This is the heap available for driver model, file systems, and general
    dynamic memory allocation. Default: 0x400000 (4 MB), varies by platform.

``CONFIG_SYS_MALLOC_F``
    Bool to enable malloc() pool before relocation. Required for driver model
    and many boot features. Default: y if DM is enabled.

``CONFIG_SYS_MALLOC_F_LEN``
    Hex value for the size of pre-relocation malloc pool. This small pool is
    used before DRAM is initialized. Default: 0x2000 (8 KB), varies by platform.

``CONFIG_SYS_MALLOC_CLEAR_ON_INIT``
    Bool to zero the malloc pool on initialization. This slows boot but ensures
    malloc returns zeroed memory. Disable for faster boot when using large
    heaps. Default: y

``CONFIG_SYS_MALLOC_DEFAULT_TO_INIT``
    Bool to call malloc_init() when mem_malloc_init() is called. Used when
    moving malloc from one memory region to another. Default: n

``CONFIG_SYS_MALLOC_BOOTPARAMS``
    Bool to malloc a buffer for bi_boot_params instead of using a fixed
    location. Default: n

``CONFIG_VALGRIND``
    Bool to annotate malloc operations for Valgrind memory debugging. Only
    useful when running sandbox builds under Valgrind. See
    :ref:`sandbox_valgrind` for details. Default: n

``CONFIG_SYS_MALLOC_SMALL``
    Bool to enable code-size optimisations for dlmalloc. This option combines
    several optimisations:

    - Disables tree bins for allocations >= 256 bytes, using simple linked-list
      bins instead. This changes large-allocation performance from O(log n) to
      O(n) but saves ~1.5-2KB.
    - Simplifies memalign() by removing fallback retry logic. Saves ~100-150 bytes.
    - Disables in-place realloc optimisation. Saves ~200 bytes.
    - Uses static malloc parameters instead of runtime-configurable ones.
    - Converts small chunk macros to functions to reduce code duplication.

    These optimisations may increase fragmentation and reduce performance for
    workloads with many large or aligned allocations, but are suitable for most
    U-Boot use cases where code size is more important. Default: n

``CONFIG_SYS_MALLOC_LEGACY``
    Bool to use the legacy dlmalloc 2.6.6 implementation instead of the modern
    dlmalloc 2.8.6. The legacy allocator has smaller code size (~450 bytes less)
    but uses more static data (~500 bytes more on 64-bit). Provided for
    compatibility and testing. New boards should use the modern allocator.
    Default: n

``CONFIG_MALLOC_DEBUG``
    Bool to enable malloc debugging features. This enables the
    ``malloc_get_info()`` function to retrieve memory statistics and supports
    the ``malloc`` command. Default: y if UNIT_TEST is enabled.

``CONFIG_MCHECK_HEAP_PROTECTION``
    Bool to enable heap protection using the mcheck library. This adds canary
    values before and after each allocation to detect buffer overflows,
    underflows, double-frees, and memory corruption. When enabled, caller
    backtraces are recorded for each allocation and displayed by
    ``malloc dump``. This significantly increases memory overhead and should
    only be used for debugging. Default: n

``CONFIG_MCHECK_BACKTRACE``
    Bool to enable backtrace collection for mcheck caller information. When
    enabled (the default), each allocation records a stack backtrace showing
    where it was made. This is useful for debugging memory leaks but adds
    overhead to every malloc call. Disable this to reduce mcheck overhead
    while keeping the canary checks and double-free detection. Default: y

xPL Boot Phases
~~~~~~~~~~~~~~~

The SPL (Secondary Program Loader), TPL (Tertiary Program Loader), and VPL
(Verification Program Loader) boot phases each have their own malloc
configuration options. These are prefixed with ``SPL_``, ``TPL_``, or ``VPL_``
and typically mirror the main U-Boot options.

Similar to U-Boot proper, xPL phases can use simple malloc (``malloc_simple``)
for pre-DRAM allocation. However, unlike U-Boot proper which transitions from
simple malloc to full dlmalloc after relocation, xPL phases that enable
``CONFIG_SPL_SYS_MALLOC_SIMPLE`` (or equivalent) cannot transition to full
malloc within that phase, since the dlmalloc code is not included in the
binary.

Note: When building with ``CONFIG_XPL_BUILD``, the code uses
``CONFIG_IS_ENABLED()`` macros to automatically select the appropriate
phase-specific option (e.g., ``IS_ENABLED(CONFIG_SYS_MALLOC_F)`` expands to
``CONFIG_SPL_SYS_MALLOC_F`` when building SPL).

SPL (Secondary Program Loader)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``CONFIG_SPL_SYS_MALLOC_F``
    Bool to enable malloc() pool in SPL before DRAM is initialized. Required
    for driver model in SPL. Default: y if SPL_FRAMEWORK and SYS_MALLOC_F.

``CONFIG_SPL_SYS_MALLOC_F_LEN``
    Hex value for SPL pre-DRAM malloc pool size. Default: inherits from
    CONFIG_SYS_MALLOC_F_LEN.

``CONFIG_SPL_SYS_MALLOC_SIMPLE``
    Bool to use only malloc_simple functions in SPL instead of full dlmalloc.
    The simple allocator is smaller (saves ~600 bytes) but cannot free()
    memory. Default: n

``CONFIG_SPL_SYS_MALLOC``
    Bool to enable a full malloc pool in SPL after DRAM is initialized.
    Used with CONFIG_SPL_CUSTOM_SYS_MALLOC_ADDR. Default: n

``CONFIG_SPL_HAS_CUSTOM_MALLOC_START``
    Bool to use a custom address for SPL malloc pool instead of the default
    location. Requires CONFIG_SPL_CUSTOM_SYS_MALLOC_ADDR. Default: n

``CONFIG_SPL_CUSTOM_SYS_MALLOC_ADDR``
    Hex address for SPL malloc pool when using custom location.

``CONFIG_SPL_SYS_MALLOC_SIZE``
    Hex value for SPL malloc pool size when using CONFIG_SPL_SYS_MALLOC.
    Default: 0x100000 (1 MB).

``CONFIG_SPL_SYS_MALLOC_CLEAR_ON_INIT``
    Bool to zero SPL malloc pool on initialization. Useful when malloc pool
    is in a region that must be zeroed before first use. Default: inherits
    from CONFIG_SYS_MALLOC_CLEAR_ON_INIT.

``CONFIG_SPL_SYS_MALLOC_SMALL``
    Bool to enable code-size optimisations for dlmalloc in SPL. Enables the
    same optimisations as CONFIG_SYS_MALLOC_SMALL (disables tree bins,
    simplifies memalign, disables in-place realloc, uses static parameters,
    converts small chunk macros to functions). SPL typically has predictable
    memory usage where these optimisations have minimal impact, making the
    code size savings worthwhile. Default: y

``CONFIG_SPL_STACK_R_MALLOC_SIMPLE_LEN``
    Hex value for malloc_simple heap size after switching to DRAM stack in SPL.
    Only used when CONFIG_SPL_STACK_R and CONFIG_SPL_SYS_MALLOC_SIMPLE are
    enabled. Provides a larger heap than the initial SRAM pool. Default:
    0x100000 (1 MB).

TPL (Tertiary Program Loader)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``CONFIG_TPL_SYS_MALLOC_F``
    Bool to enable malloc() pool in TPL. Default: y if TPL and SYS_MALLOC_F.

``CONFIG_TPL_SYS_MALLOC_F_LEN``
    Hex value for TPL malloc pool size. Default: inherits from
    CONFIG_SPL_SYS_MALLOC_F_LEN.

``CONFIG_TPL_SYS_MALLOC_SIMPLE``
    Bool to use only malloc_simple in TPL instead of full dlmalloc. Saves
    code size at the cost of no free() support. Default: n

VPL (Verification Program Loader)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``CONFIG_VPL_SYS_MALLOC_F``
    Bool to enable malloc() pool in VPL. Default: y if VPL and SYS_MALLOC_F.

``CONFIG_VPL_SYS_MALLOC_F_LEN``
    Hex value for VPL malloc pool size. Default: inherits from
    CONFIG_SPL_SYS_MALLOC_F_LEN.

``CONFIG_VPL_SYS_MALLOC_SIMPLE``
    Bool to use only malloc_simple in VPL. Default: y

dlmalloc Compile-Time Options
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

These options are set in the U-Boot section of ``common/dlmalloc.c``:

``NO_MALLOC_STATS``
    Disable malloc_stats() function. Default: 1 (disabled)

``NO_MALLINFO``
    Disable mallinfo() function. Default: 1 for non-sandbox builds

``INSECURE``
    Disable runtime heap validation checks. This reduces code size but removes
    detection of heap corruption. Default: 1 for non-sandbox builds

``NO_REALLOC_IN_PLACE``
    Disable in-place realloc optimisation. Enabled by CONFIG_SYS_MALLOC_SMALL.
    Saves ~200 bytes of code. Default: 0

``NO_TREE_BINS``
    Disable tree bins for large allocations (>= 256 bytes), using simple
    linked-list bins instead. Enabled by CONFIG_SYS_MALLOC_SMALL. Saves
    ~1.5-2KB but changes performance from O(log n) to O(n). Default: 0

``SIMPLE_MEMALIGN``
    Simplify memalign() by removing fallback retry logic. Enabled by
    CONFIG_SYS_MALLOC_SMALL. Saves ~100-150 bytes. Default: 0

``STATIC_MALLOC_PARAMS``
    Use static malloc parameters instead of runtime-configurable ones.
    Enabled by CONFIG_SYS_MALLOC_SMALL. Default: 0

``SMALLCHUNKS_AS_FUNCS``
    Convert small chunk macros (insert_small_chunk, unlink_first_small_chunk)
    to functions to reduce code duplication. Enabled by CONFIG_SYS_MALLOC_SMALL.
    Default: 0

``SIMPLE_SYSALLOC``
    Use simplified sys_alloc() that only supports contiguous sbrk() extension.
    Enabled automatically for non-sandbox builds. Saves code by removing mmap
    and multi-segment support. Default: 1 for non-sandbox, 0 for sandbox

``MORECORE_CONTIGUOUS``
    Assume sbrk() returns contiguous memory. Default: 1

``MORECORE_CANNOT_TRIM``
    Disable releasing memory back to the system. Default: 1

``HAVE_MMAP``
    Enable mmap() for large allocations. Default: 0 (U-Boot uses sbrk only)

Code Size
---------

The dlmalloc 2.8.6 implementation is larger than the older 2.6.6 version due
to its more sophisticated algorithms. To minimise code size for
resource-constrained systems, U-Boot provides several optimisation levels:

**Default optimisations** (always enabled for non-sandbox builds):

- INSECURE=1 (saves ~1100 bytes)
- NO_MALLINFO=1 (saves ~200 bytes)
- SIMPLE_SYSALLOC=1 (saves code by simplifying sys_alloc)

**CONFIG_SYS_MALLOC_SMALL** (additional optimisations, default y for SPL):

- NO_TREE_BINS=1 (saves ~1.5-2KB)
- NO_REALLOC_IN_PLACE=1 (saves ~200 bytes)
- SIMPLE_MEMALIGN=1 (saves ~100-150 bytes)
- STATIC_MALLOC_PARAMS=1
- SMALLCHUNKS_AS_FUNCS=1 (reduces code duplication)

With default optimisations only, the code-size increase over dlmalloc 2.6.6
is about 450 bytes, while data usage decreases by about 500 bytes.

With CONFIG_SYS_MALLOC_SMALL enabled, significant additional code savings
are achieved, making it suitable for size-constrained SPL builds.

Sandbox builds retain full functionality for testing, including mallinfo()
for memory-leak detection.

Debugging
---------

U-Boot provides several features to help debug memory-allocation issues:

CONFIG_MALLOC_DEBUG
~~~~~~~~~~~~~~~~~~~

Enable ``CONFIG_MALLOC_DEBUG`` to activate malloc debugging features. This is
enabled by default when ``CONFIG_UNIT_TEST`` is set. It provides:

- The ``malloc_get_info()`` function to retrieve memory statistics
- Allocation call counters (malloc, free, realloc counts)
- Support for the ``malloc`` command (see :doc:`/usage/cmd/malloc`)

The :doc:`/usage/cmd/malloc` command provides two subcommands:

``malloc info``
    Shows memory-allocation statistics including total heap size, memory in use,
    and call counts::

        => malloc info
        total bytes   = 96 MiB
        in use bytes  = 700.9 KiB
        malloc count  = 1234
        free count    = 567
        realloc count = 89

``malloc dump``
    Walks the entire heap and prints each chunk's address, size, and status
    (used, free, or top). This is useful for understanding heap layout and
    finding memory leaks::

        => malloc dump
        Heap dump: 19a0e000 - 1fa10000
             Address        Size  Status
        ----------------------------------
            19a0e000          10  (chunk header)
            19a0e010          a0
            19a0e0b0        6070
            19adfc30          60  <free>
            19adff90     5f3f030  top
            1fa10000              end
        ----------------------------------
        Used: c2ef0 bytes in 931 chunks
        Free: 5f3f0c0 bytes in 2 chunks + top

CONFIG_MCHECK_HEAP_PROTECTION
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Enable ``CONFIG_MCHECK_HEAP_PROTECTION`` for heap protection using the mcheck
library. This adds canary values before and after each allocation to detect:

- Buffer overflows and underflows
- Double-frees
- Memory corruption

This significantly increases memory overhead and should only be used for
debugging. U-Boot includes mcheck support via mcheck(), mcheck_pedantic(), and
mcheck_check_all().

When mcheck is enabled, the ``malloc dump`` command also shows caller
information for each allocation, including a backtrace showing where the
allocation was made::

    => malloc dump
    Heap dump: 18a1d000 - 1ea1f000
         Address        Size  Status
    ----------------------------------
        18a1d000          10  (chunk header)
        18a1d010          90  used  log_init:453 <-board_init_r:774
        18a1d0a0        6060  used  membuf_new:420 <-console_record
        18a3b840          90  used  of_alias_scan:911 <-board_init_

This caller information makes it easy to track down memory leaks by showing
exactly where each allocation originated.

Malloc-Traffic Log
~~~~~~~~~~~~~~~~~~

On sandbox, when mcheck is enabled, a malloc-traffic log can record all
malloc/free/realloc calls. This is useful for debugging allocation patterns
and finding where allocations without caller info originate. See
:doc:`../usage/cmd/malloc` for usage.

The log buffer is allocated from host memory using ``os_malloc()``, so it
does not affect U-Boot's heap. The size is controlled by
``CONFIG_MCHECK_LOG_SIZE`` (default 512K entries, about 80MB).

If the log fills up, it wraps around and overwrites the oldest entries.
A warning is shown when dumping if entries were lost.

Valgrind
~~~~~~~~

When running sandbox with Valgrind, the allocator includes annotations to help
detect memory errors. See :ref:`sandbox_valgrind`.

malloc testing
~~~~~~~~~~~~~~

Unit tests can use malloc_enable_testing() to simulate allocation failures.

Finding Memory Leaks
~~~~~~~~~~~~~~~~~~~~

U-Boot provides several tools for detecting and diagnosing memory leaks.
These techniques are primarily supported on sandbox, which has the full
debugging infrastructure enabled by default (``CONFIG_MALLOC_DEBUG``,
``CONFIG_MCHECK_HEAP_PROTECTION``) and access to host filesystem functions
for writing dump files.

**Leak detection in unit tests**

Unit tests can use ``ut_check_delta()`` to detect memory leaks::

    ulong mem_start;

    mem_start = ut_check_delta(0);  /* Record starting heap usage */

    /* ... test code that allocates and frees memory ... */

    ut_asserteq(0, ut_check_delta(mem_start));  /* Verify no leak */

This uses ``mallinfo().uordblks`` to compare heap usage before and after.

**Heap dump comparison**

When a leak is detected, use ``malloc_dump()`` to capture heap state before
and after the operation. In sandbox builds, ``malloc_dump_to_file()`` writes
the dump to a host file for easier comparison::

    malloc_dump_to_file("/tmp/before.txt");

    /* ... operation that may leak ... */

    malloc_dump_to_file("/tmp/after.txt");

Then compare the dumps to find leaked allocations::

    $ diff /tmp/before.txt /tmp/after.txt

Or extract just the addresses and sizes for comparison::

    $ awk '{print $1, $2}' /tmp/before.txt | sort > /tmp/b.txt
    $ awk '{print $1, $2}' /tmp/after.txt | sort > /tmp/a.txt
    $ comm -13 /tmp/b.txt /tmp/a.txt   # Show allocations only in 'after'

The dump includes caller information when ``CONFIG_MCHECK_HEAP_PROTECTION``
is enabled, showing exactly where each leaked allocation originated.

**Malloc-traffic logging**

For more detailed analysis, use the malloc-traffic log to record all
allocations during an operation::

    malloc_log_start();

    /* ... operation to trace ... */

    malloc_log_stop();
    malloc_log_to_file("/tmp/malloc_log.txt");  /* Sandbox only */

The log shows every malloc(), free(), and realloc() call with addresses, sizes,
and caller backtraces (if enabled). Search for allocations that were never
freed::

    $ grep "alloc" /tmp/malloc_log.txt   # Find all allocations
    $ grep "16ad1290" /tmp/malloc_log.txt  # Check if a specific address was freed

**Verifying debug functions don't allocate**

When using these debugging functions, verify they don't affect heap state
by checking ``malloc_get_info()`` before and after::

    struct malloc_info before, after;

    malloc_get_info(&before);
    malloc_dump_to_file("/tmp/dump.txt");
    malloc_get_info(&after);

    /* Verify no allocations occurred */
    assert(before.malloc_count == after.malloc_count);
    assert(before.in_use_bytes == after.in_use_bytes);

**Automatic leak checking with ut -L**

The ``ut`` command accepts a ``-L`` flag that checks for memory leaks around
each test. It snapshots every in-use heap chunk at the start of
``ut_run_test()`` and compares after both ``test_pre_run()`` and
``test_post_run()`` have completed. Any new allocations are reported with
their address, size and caller backtrace::

    => ut -LE dm dm_test_acpi_bgrt
    Test: acpi_bgrt: acpi.c
    Leak: 2 allocs
      14a5c5c0 110 stdio_clone:230 <-stdio_register_dev:244 <-vidconsole_post_probe:961
      14a5c6d0  b0 map_to_sysmem:210 <-video_post_probe:823 <-device_probe:589
    Result: PASS: acpi_bgrt: acpi.c

The snapshot is stored using ``os_malloc()`` so it does not affect the
heap being measured. Caller backtraces are available when
``CONFIG_MCHECK_HEAP_PROTECTION`` is enabled (the default for sandbox).

When using uman, pass ``--leak-check``::

    $ um t --leak-check dm_test_acpi_bgrt

This makes it easy to scan an entire test suite for leaks::

    $ um t --leak-check -V dm

**Interactive leak checking with the malloc command**

The ``malloc leak`` command provides interactive leak detection at the
U-Boot command line. Take a snapshot before an operation and check
afterwards::

    => malloc leak start
    Heap snapshot: 974 allocs
    => setenv foo bar
    => malloc leak end
      14a2a9a0 90 sandbox_strdup:353 <-hsearch_r:403 <-env_do_env_set:130
      14a2aa30 90 sandbox_strdup:353 <-hsearch_r:403 <-env_do_env_set:130
    2 leaked allocs

Use ``malloc leak`` (without arguments) to check the count without
releasing the snapshot, so you can continue testing::

    => malloc leak start
    Heap snapshot: 974 allocs
    => <some operation>
    => malloc leak
    No leaks
    => <another operation>
    => malloc leak
    3 new allocs
    => malloc leak end
    ...

**Practical workflow**

1. Run ``um t --leak-check -V dm`` (or another suite) to find leaky tests
2. Use the caller backtrace in the ``-L`` output to find the allocation site
3. If more detail is needed, use ``malloc leak start`` / ``malloc leak end``
   interactively, or enable ``malloc_log_start()`` to trace all allocations
4. Fix the leak and verify the test passes

**Dumping heap state on exit**

When running sandbox, the ``--malloc_dump`` command-line option writes a heap
dump to a file when U-Boot exits cleanly (via ``poweroff`` or ``reset``). This
is useful for capturing heap state at the end of a test session::

    ./u-boot -Tf -c "poweroff" --malloc_dump /tmp/heap.txt

The pytest framework also supports this via ``--malloc-dump``::

    test/py/test.py -B sandbox --malloc-dump /tmp/heap.txt -k test_source

The filename may contain ``%d`` which is replaced with a sequence number
that increments each time U-Boot restarts during the test session, so each
instance produces a separate dump::

    test/py/test.py -B sandbox --malloc-dump /tmp/heap%d.txt -k test_vboot

API Reference
-------------

Standard C functions:

- ``void *malloc(size_t size)`` - Allocate memory
- ``void free(void *ptr)`` - Free allocated memory
- ``void *realloc(void *ptr, size_t size)`` - Resize allocation
- ``void *calloc(size_t nmemb, size_t size)`` - Allocate zeroed memory
- ``void *memalign(size_t alignment, size_t size)`` - Aligned allocation

Pre-relocation simple malloc (from malloc_simple.c):

- ``void *malloc_simple(size_t size)`` - Simple bump allocator
- ``void *memalign_simple(size_t alignment, size_t size)`` - Aligned version

See Also
--------

- :doc:`memory` - Memory management overview
- :doc:`global_data` - Global data and the GD_FLG_FULL_MALLOC_INIT flag
- :doc:`/usage/cmd/malloc` - malloc command reference
