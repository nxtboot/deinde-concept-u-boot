.. SPDX-License-Identifier: GPL-2.0+

Booting Linux directly
======================

There are two ways for U-Boot to start a Linux kernel. With the EFI path,
U-Boot's EFI loader runs an application (typically GRUB or the kernel itself,
which is a valid EFI application) and the kernel's EFI stub then takes over:
it places the kernel in memory, gathers information from the firmware and
calls ExitBootServices() before jumping into the kernel proper. With the direct
path, U-Boot loads the kernel and jumps to it using the architecture's boot
protocol, passing everything the kernel needs in the devicetree. This is what
the bootm, booti and bootz commands do, and so what the extlinux and BLS
bootmeths, FIT images and :doc:`vbe` use.

The EFI stub does a number of useful things for the kernel. This document
describes each of them and how the direct path provides the same thing, since
none of them actually needs EFI: the kernel accepts every piece of information
involved through the devicetree, and U-Boot can supply it. Most of the
details below are for arm64, where the kernel's 'Image' format is used, but the
principles apply to other architectures.

Kernel placement
----------------

An arm64 Image must be placed at text_offset bytes (a field in its header) from
a 2MB-aligned base and run from there; the image_size field gives the memory it
needs, including its .bss. Since Linux v4.6 the header's flags field has bit 3
set, meaning the base can be anywhere in RAM; older kernels want to be as close
to the start of RAM as possible.

The EFI stub copies the kernel once, to a randomly chosen address. The kernel
itself never copies anything, so the direct path has the opportunity to avoid
copying entirely:

- An uncompressed Image which is loaded to an address that already satisfies
  the rules is left where it is. Loading to a 2MB-aligned address (plus
  text_offset, which is 0 for modern kernels) therefore costs nothing.

- A compressed Image is decompressed straight to where it will run from,
  provided the compressed data records its uncompressed size. U-Boot reads
  that with image_decomp_size(), uses booti_alloc() to reserve a suitable
  region in lmb, decompresses into it and then calls booti_check() to read
  the header and extend the reservation to the Image's .bss. Gzip always
  records the size and distros ship gzip-compressed kernels, so this is the
  common case; zstd normally records it, lzma and lz4 only if the encoder was
  asked to, and bzip2 and lzo never do.

- Otherwise the Image is moved once, with a message such as
  ``Moving Image from 8080000 to 8200000``.

With CONFIG_BOOTI_RANDOMIZE_BASE the base is chosen at random from the free
memory, using the RNG uclass and lmb_alloc_random(), so that the kernel's
physical address is not predictable. This is the physical half of KASLR and
matches what the EFI stub does. As with the stub, it is skipped if the kernel
command line contains ``nokaslr``. Since the address is chosen before a
compressed kernel is decompressed, randomisation costs no extra copy.

The lmb allocator knows about everything U-Boot has loaded, since the
filesystem code reserves each file it reads, so a randomly chosen region can
never overlap the compressed kernel, the initrd or the devicetree.

See :doc:`../usage/cmd/booti` for the user-facing details.

Randomness
----------

The kernel needs randomness from the bootloader for two purposes, and on many
CPUs it has no way to obtain any itself early in boot: the RNDR instruction is
only available from ARMv8.5, and hardware RNG drivers load far too late.

kaslr-seed
    A 64-bit value in /chosen which the kernel uses to randomise its virtual
    address and the offset of the linear map. Without it the kernel reports
    'KASLR disabled due to lack of seed' and runs at a fixed virtual address.
    U-Boot adds this automatically whenever CONFIG_DM_RNG is enabled. The
    EFI stub obtains the same seed from EFI_RNG_PROTOCOL, which U-Boot
    provides from the same RNG device.

rng-seed
    A block of bytes in /chosen which the kernel feeds into its random-number
    generator and credits as entropy, so that 'crng init done' appears at
    time zero instead of whenever enough interrupt timing has been gathered.
    U-Boot adds this when CONFIG_FDT_RNG_SEED is enabled: it sends the
    EVT_RNG_SEED event, so that a board with its own source of entropy can
    provide the seed, and otherwise reads 64 bytes from the RNG uclass. The
    EFI stub does the same with its random-seed configuration table.

Both are added by fdt_chosen(), which runs after any measurement of the
devicetree (see below), so they do not disturb measured boot.

Measured boot
-------------

With CONFIG_MEASURED_BOOT, U-Boot measures the kernel into PCR 8, the initrd
into PCR 9 and the kernel command line into PCR 1, and with
CONFIG_MEASURE_DEVICETREE the devicetree into PCR 1 as well. This is a
superset of what the EFI stub measures, which is the initrd and the load
options. The measurement happens in the BOOTM_STATE_MEASURE step, before the
devicetree fixups in BOOTM_STATE_OS_PREP, so what is measured is the
devicetree as loaded rather than as finally passed to the kernel.

The event log is passed to the OS by adding "linux,sml-base" and
"linux,sml-size" to the TPM node of the outgoing devicetree and reserving the
memory, which is how Linux finds a firmware-provided log on any non-EFI boot.
This works with a distro devicetree, not just U-Boot's own. See
:doc:`../usage/measured_boot`.

Display
-------

If U-Boot has set up a display, the EFI stub passes its framebuffer to the
kernel through the screen-information structure it fills from the GOP, so that
efifb or simpledrm can use the display until the real driver loads. On the
direct path the equivalent is a simple-framebuffer node under /chosen, which
CONFIG_FDT_SIMPLEFB_HANDOFF adds to the outgoing devicetree, or fills in and
enables if the devicetree already contains one, along with a no-map
reserved-memory entry so that the kernel leaves the framebuffer alone. The
kernel populates such a node before any other device, and disables its generic
system-framebuffer support when it finds one.

The node does not list the clocks and power domains which keep the display
running, since these are phandles into a devicetree U-Boot did not write. A
kernel which gates unused clocks may therefore blank the display before its
own driver claims it. A pre-filled, disabled simple-framebuffer node in the
devicetree, as the binding recommends, avoids this, since U-Boot then only
fills in the mode and enables the node. The EFI path has the same limitation,
as the screen information carries no clock details either.

Clearing memory
---------------

An OS which holds secrets in memory, such as disk-encryption keys, can ask EFI
firmware to clear all of RAM on the next reset, so that an attacker cannot
reset the machine and boot something of their own to read them out. The stub
asks for this on every boot by setting the MemoryOverwriteRequestControl
variable, if the firmware offers it. A directly booted OS has no such channel,
so U-Boot offers to clear all of RAM on every boot instead.

The right moment is just after the RAM has been set up, in whichever phase
does that (TPL on RK3399, SPL on most other SoCs): nothing is in RAM yet, so
the whole of it can be cleared without working out what is in use, and it
happens before anything at all, including TF-A or a falcon-mode kernel, is
loaded. CONFIG_TPL_CLEAR_RAM_ON_INIT (or the SPL or VPL form) makes that
phase call ram_clear_all() at the start of its board_init_r(), which zeroes
the region each RAM driver reports and prints the time taken. A boot which
cannot clear RAM stops.

The cost is that of memset() in that phase, so the caches and the memset()
implementation both matter. On a 4GB RK3399 board, TPL with the data cache
off manages about 1.6MB/s, since every store is a separate uncached write;
with the cache on (the Rockchip TPL and SPL both turn it on as soon as DRAM
is up) but the byte-at-a-time memset() which TPL uses to save space, it takes
35s; with the arm64 assembly memset() (CONFIG_TPL_USE_ARCH_MEMSET, which
uses 'dc zva') it takes 1.0s. The option is off by default, and only a boot
which follows a session that held secrets needs it, so a request mechanism
like EFI's, with a persistent flag which the kernel can set, is the natural
next step.

Other features of the EFI stub
------------------------------

The remaining things the stub does either have a direct equivalent already or
do not apply outside EFI:

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - EFI stub
     - Direct path
   * - Initrd via the LoadFile2 protocol
     - /chosen/linux,initrd-start and linux,initrd-end
   * - Command line from the load options
     - /chosen/bootargs
   * - Disables PCI bus mastering before ExitBootServices()
     - dm_remove_devices_active() removes devices with active DMA in
       bootm_final()
   * - Memreserve table, so that GICv3 LPI tables survive kexec
     - Not needed: on a devicetree boot the kernel detects and reuses the
       tables
   * - Refuses an unauthenticated devicetree under Secure Boot
     - FIT signatures and VBE verify the devicetree along with the kernel;
       extlinux and BLS do not verify their files
   * - Screen information for efifb
     - A simple-framebuffer node under /chosen, with the memory reserved (see
       Display below)
   * - Checks the CPU supports the kernel's page-granule size and prints an
       error
     - booti compares the page size in the Image header with the CPU's and
       refuses the kernel with an error
   * - Reset-attack mitigation (asks the firmware to wipe RAM on the next
       reset)
     - CONFIG_TPL_CLEAR_RAM_ON_INIT (or SPL/VPL) clears all of RAM in the
       phase which sets it up (see Clearing memory below)
   * - Runtime services, EFI variables, remapping the image read-only
     - Not applicable: these only exist while the stub itself runs

U-Boot also writes an smbios3-entrypoint property to /chosen, but the kernel
has no code to read it, so on a direct boot SMBIOS information is not currently
available to Linux.
