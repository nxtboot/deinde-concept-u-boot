.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: bootm (command)

bootm command
=============

Synopsis
--------

::

    bootm [start|restart] [<fit_addr>]#<conf>[#<extra-conf>]
    bootm [start|restart] [[<fit_addr>]:<os_subimg>] [[<fit_addr2>]:<rd_subimg2>] [[<fit_addr3>]:<fdt_subimg>]
    bootm <subcmd>

    bootm <addr1> [[<addr2> [<addr3>]]    # Legacy boot

Description
-----------

The *bootm* command is used to boot an Operating System. It has a large number
of options depending on what needs to be booted.

Note that the second form supports the first and/or second arguments to be
omitted by using a hyphen '-' instead.

fit_addr / fit_addr2 / fit_addr3
    address of FIT to boot, defaults to CONFIG_SYS_LOAD_ADDR. See notes below.

conf
    configuration unit to boot (must be preceded by hash '#')

extra-conf
    extra configuration to boot. This is supported only for additional
    devicetree overlays to apply on the base device tree supplied by the first
    configuration unit.

os_subimg
    OS sub-image to boot (must be preceded by colon ':')

rd_subimg
    ramdisk sub-image to boot. Use a hyphen '-' if there is no ramdisk but an
    FDT is needed.

fdt_subimg
    FDT sub-image to boot

Bootm steps
~~~~~~~~~~~

The bootm command follows a predefined set of states to complete a boot. The
usual case, if a subcommand is omitted, is that bootm runs a complete boot,
working through each state one by one, in sequence. Some states are skipped
depending on the boot type.

Note that the bootm command automatically adds `findos` and `findother` states
when the `start` state begins. These states are documented here but cannot be
individually selected.

The states are described below:

start
    Start the boot process afresh, recording the image(s) to be booted.

restart
    Start the boot process again, but keeping the current state. This allows
    multiple FITs to be loaded, for example a first FIT containing just the
    devicetree and a second containing the OS and any overlays. In this case,
    the first `bootm` command will typically use only `start` (and its implicit
    states) and `loados`, with the second using `bootm restart` to select the
    second FIT.

preload
    Deal with any preload step, sometimes used to do a full signature check of
    the FIT, before looking at any of the data within.

    This cannot be selected from the bootm command, as it is implicit in
    `start`.

findos
    Find the OS within the FIT, etc. and set up the images.os fields

    This cannot be selected from the bootm command, as it is implicit in
    `start`.

findother
    Find other files that may be needed, including any ramdisk, devicetree,
    FPGA or loadables. After this, the images.rd... and images.ft fields are
    set up.

    For each loadable, the appropriate handler is called (as declared by the
    U_BOOT_FIT_LOADABLE_HANDLER() macro). There is no record kept of which
    loadables were loaded, other than that used by :doc:`../upl`.

    This step is only active if the image type is kernel, kernel_noload or
    multi, **and** the OS is Linux, VxWorks, EFI or TEE.

    This cannot be selected from the bootm command, as it is implicit in
    `start`.

measure
    This measures the loaded files, if `CONFIG_MEASURED_BOOT` is enabled.

    This cannot be selected from the bootm command. Currently it is only used
    when using bootm without a subcommand.

loados
    Load the OS itself to its final location. This may involve copying or
    decompressing it.

ramdisk
    Load the ramdisk to its final location, if necessary. This typically
    involves copying it out of the FIT.

fdt
    Load the devicetree to its final location. This typically involves copying
    or decompressing it from the FIT.

cmdline
    Set up the command line for the OS, e.g. using the `bootargs` environment
    variable, perhaps adding some more pieces from an `extlinux.conf` entry.

bdt
    Set up the board information for the OS (seldom used these days).

prep
    Prepare to boot the OS, e.g. setting up any tables or data structures that
    are required. After this the OS itself is ready to boot.

fake
    This is only used for testing and only available when `CONFIG_BOOTM_FAKE_GO`
    is
    enabled. It fakes a boot of the OS, performs all the normal steps right up
    to the point where U-Boot is about to jump to the OS. It then runs a list
    of commands from the `fakegocmd` environment variable. Note that the
    machine may not be stable after this occurs.

    This can be useful for debugging slow booting, for example. See
    :doc:`/develop/trace` for more details.

go
    Start the OS, after performing any last-minute tasks. At this point, the OS
    should be running and U-Boot's task is completed.

Subcommands
~~~~~~~~~~~

Except as noted above, it is possible to perform the bootm processing piecemeal.
The first command must be `bootm start` after which the others can be used,
normally in the order they are documented above. This can aid debugging but it
can also help to see what happens at each stage and the state of U-Boot and
memory after each stage.

See below for legacy boot. Booting using :doc:`../fit/index` is recommended.

Note on current image address
-----------------------------

When bootm is called without arguments, the image at current image address is
booted. The current image address is the address set most recently by a load
command, etc, and is by default equal to CONFIG_SYS_LOAD_ADDR. For example,
consider the following commands::

    tftp 200000 /tftpboot/kernel
    bootm
    # Last command is equivalent to:
    # bootm 200000

As shown above, with FIT the address portion of any argument
can be omitted. If <addr3> is omitted, then it is assumed that image at
<addr2> should be used. Similarly, when <addr2> is omitted, it is assumed that
image at <addr1> should be used. If <addr1> is omitted, it is assumed that the
current image address is to be used. For example, consider the following
commands::

    tftp 200000 /tftpboot/uImage
    bootm :kernel-1
    # Last command is equivalent to:
    # bootm 200000:kernel-1

    tftp 200000 /tftpboot/uImage
    bootm 400000:kernel-1 :ramdisk-1
    # Last command is equivalent to:
    # bootm 400000:kernel-1 400000:ramdisk-1

    tftp 200000 /tftpboot/uImage
    bootm :kernel-1 400000:ramdisk-1 :fdt-1
    # Last command is equivalent to:
    # bootm 200000:kernel-1 400000:ramdisk-1 400000:fdt-1


Legacy boot
-----------

U-Boot supports a legacy image format, enabled by `CONFIG_LEGACY_IMAGE_FORMAT`.
This is not recommended as it is quite limited and insecure. Use
:doc:`../fit/index` instead. It is documented here for old boards which still
use it.

Arguments are:

addr1
    address of legacy image to boot. If the image includes a second component
    (ramdisk) it is used as well, unless the second parameter is hyphen '-'.

addr2
    address of legacy image to use as ramdisk

addr3
    address of legacy image to use as FDT


Example syntax
--------------

This section provides various examples of possible usage::

    1.  bootm       /* boot image at the current address, equivalent to 2,3,8 */

This is equivalent to cases 2, 3 or 8, depending on the type of image at
the current image address.

Boot method: see cases 2,3,8

Legacy uImage syntax
~~~~~~~~~~~~~~~~~~~~

::

    2.  bootm <addr1>            /* single image at <addr1> */

Boot kernel image located at <addr1>.

Boot method: non-FDT

::

    3.  bootm <addr1>            /* multi-image at <addr1>  */

First and second components of the image at <addr1> are assumed to be a
kernel and a ramdisk, respectively. The kernel is booted with initrd loaded
with the ramdisk from the image.

Boot method: depends on the number of components at <addr1>, and on whether
U-Boot is compiled with OF support, which it should be.

    ==================== ======================== ========================
    Configuration        2 components             3 components
                         (kernel, initrd)         (kernel, initrd, fdt)
    ==================== ======================== ========================
    #ifdef CONFIG_OF_*                   non-FDT                     FDT
    #ifndef CONFIG_OF_*                  non-FDT                 non-FDT
    ==================== ======================== ========================

::

    4.  bootm <addr1> -            /* multi-image at <addr1>  */

Similar to case 3, but the kernel is booted without initrd.  Second
component of the multi-image is irrelevant (it can be a dummy, 1-byte file).

Boot method: see case 3

::

    5.  bootm <addr1> <addr2>        /* single image at <addr1> */

Boot kernel image located at <addr1> with initrd loaded with ramdisk
from the image at <addr2>.

Boot method: non-FDT

::

    6.  bootm <addr1> <addr2> <addr3>   /* single image at <addr1> */

<addr1> is the address of a kernel image, <addr2> is the address of a
ramdisk image, and <addr3> is the address of a FDT binary blob.  Kernel is
booted with initrd loaded with ramdisk from the image at <addr2>.

Boot method: FDT

::

    7.  bootm <addr1> -      <addr3>   /* single image at <addr1> */

<addr1> is the address of a kernel image and <addr3> is the address of
a FDT binary blob. Kernel is booted without initrd.

Boot method: FDT

FIT syntax
~~~~~~~~~~

::

    8.  bootm <addr1>

Image at <addr1> is assumed to contain a default configuration, which
is booted.

Boot method: FDT or non-FDT, depending on whether the default configuration
defines FDT

::

    9.  bootm [<addr1>]:<subimg1>

Similar to case 2: boot kernel stored in <subimg1> from the image at
address <addr1>.

Boot method: non-FDT

::

    10. bootm [<addr1>]#<conf>[#<extra-conf[#...]]

Boot configuration <conf> from the image at <addr1>.

Boot method: FDT or non-FDT, depending on whether the configuration given
defines FDT

::

    11. bootm [<addr1>]:<subimg1> [<addr2>]:<subimg2>

Equivalent to case 5: boot kernel stored in <subimg1> from the image
at <addr1> with initrd loaded with ramdisk <subimg2> from the image at
<addr2>.

Boot method: non-FDT

::

    12. bootm [<addr1>]:<subimg1> [<addr2>]:<subimg2> [<addr3>]:<subimg3>

Equivalent to case 6: boot kernel stored in <subimg1> from the image
at <addr1> with initrd loaded with ramdisk <subimg2> from the image at
<addr2>, and pass FDT blob <subimg3> from the image at <addr3>.

Boot method: FDT

::

    13. bootm [<addr1>]:<subimg1> [<addr2>]:<subimg2> <addr3>

Similar to case 12, the difference being that <addr3> is the address
of FDT binary blob that is to be passed to the kernel.

Boot method: FDT

::

    14. bootm [<addr1>]:<subimg1> -              [<addr3>]:<subimg3>

Equivalent to case 7: boot kernel stored in <subimg1> from the image
at <addr1>, without initrd, and pass FDT blob <subimg3> from the image at
<addr3>.

Boot method: FDT

    15. bootm [<addr1>]:<subimg1> -              <addr3>

Similar to case 14, the difference being that <addr3> is the address
of the FDT binary blob that is to be passed to the kernel.

Boot method: FDT



Example
-------

boot kernel "kernel-1" stored in a new uImage located at 200000::

    bootm 200000:kernel-1

boot configuration "cfg-1" from a new uImage located at 200000::

    bootm 200000#cfg-1

boot configuration "cfg-1" with extra "cfg-2" from a new uImage located
at 200000::

    bootm 200000#cfg-1#cfg-2

boot "kernel-1" from a new uImage at 200000 with initrd "ramdisk-2" found in
some other new uImage stored at address 800000::

    bootm 200000:kernel-1 800000:ramdisk-2

boot "kernel-2" from a new uImage at 200000, with initrd "ramdisk-1" and FDT
"fdt-1", both stored in some other new uImage located at 800000::

    bootm 200000:kernel-1 800000:ramdisk-1 800000:fdt-1

boot kernel "kernel-2" with initrd "ramdisk-2", both stored in a new uImage
at address 200000, with a raw FDT blob stored at address 600000::

    bootm 200000:kernel-2 200000:ramdisk-2 600000

boot kernel "kernel-2" from new uImage at 200000 with FDT "fdt-1" from the
same new uImage::

    bootm 200000:kernel-2 - 200000:fdt-1

See also
--------

* :doc:`iminfo<iminfo>` for checking an image before booting it

.. sectionauthor:: Bartlomiej Sieka <tur@semihalf.com>
.. sectionauthor:: Simon Glass <sjg@chromium.org>
