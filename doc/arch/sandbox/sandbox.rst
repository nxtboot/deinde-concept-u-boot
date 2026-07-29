.. SPDX-License-Identifier: GPL-2.0+ */
.. Copyright (c) 2014 The Chromium OS Authors.
.. sectionauthor:: Simon Glass <sjg@chromium.org>

Sandbox
=======

Native Execution of U-Boot
--------------------------

The 'sandbox' architecture is designed to allow U-Boot to run under Linux on
almost any hardware. To achieve this it builds U-Boot (so far as possible)
as a normal C application with a main() and normal C libraries.

All of U-Boot's architecture-specific code therefore cannot be built as part
of the sandbox U-Boot. The purpose of running U-Boot under Linux is to test
all the generic code, not specific to any one architecture. The idea is to
create unit tests which we can run to test this upper level code.

Sandbox allows development of many types of new features in a traditional way,
rather than needing to test each iteration on real hardware. Many U-Boot
features were developed on sandbox, including the core driver model, most
uclasses, verified boot, bloblist, logging and dozens of others. Sandbox has
enabled many large-scale code refactors as well.

CONFIG_SANDBOX is defined when building a native board.

The board name is 'sandbox' but the vendor name is unset, so there is a
single board in board/sandbox.

CONFIG_SANDBOX_BIG_ENDIAN should be defined when running on big-endian
machines.

There are two versions of the sandbox: One using 32-bit-wide integers, and one
using 64-bit-wide integers. The 32-bit version can be build and run on either
32 or 64-bit hosts by either selecting or deselecting CONFIG_SANDBOX_32BIT; by
default, the sandbox it built for a 32-bit host. The sandbox using 64-bit-wide
integers can only be built on 64-bit hosts.

Note that standalone/API support is not available at present.


Prerequisites
-------------

Install the dependencies noted in :doc:`../../build/gcc`.


Basic Operation
---------------

To run sandbox U-Boot use something like::

   make sandbox_defconfig all
   ./u-boot

Note: If you get errors about 'sdl-config: Command not found' you may need to
install libsdl2.0-dev or similar to get SDL support. Alternatively you can
build sandbox without SDL (i.e. no display/keyboard support) by disabling
CONFIG_SANDBOX_SDL in the .config file.

U-Boot will start on your computer, showing a sandbox emulation of the serial
console::

   U-Boot 2014.04 (Mar 20 2014 - 19:06:00)

   DRAM:  128 MiB
   Using default environment

   In:    serial
   Out:   lcd
   Err:   lcd
   =>

You can issue commands as you would normally. If the command you want is
not supported you can add it to include/configs/sandbox.h.

To exit, type 'poweroff' or press Ctrl-C.


Console / LCD support
---------------------

Assuming that CONFIG_SANDBOX_SDL is enabled when building, you can run the
sandbox with LCD and keyboard emulation, using something like::

   ./u-boot -d u-boot.dtb -l

This will start U-Boot with a window showing the contents of the LCD. If
that window has the focus then you will be able to type commands as you
would on the console. You can adjust the display settings in the device
tree file - see arch/sandbox/dts/sandbox.dts.


Command-line Options
--------------------

Various options are available, mostly for test purposes. Use -h to see
available options. Some of these are described below:

--autoboot_keyed
  Use this to enable keyed autoboot. Sandbox disables this function by default
  even if CONFIG_AUTOBOOT_KEYED is enabled, since it interfers with tests and
  normal usage

-A, --no_term_present
  Assume no terminal is present. This is used for pager testing.

-b. boot
  The distro boot feature doesn't run by default on sandbox, since it normally
  not vert useful. For the distro_bootcmds to succeed, quite a bit of setup is
  required (e.g. network configured or host image bound), so running them
  by default isn't that useful. Note that standard boot has surplanted
  distro boot in any case.

-B, --bind <label>:<filename>[:hex_blksz[:rem]]
  Bind a file to a device in sandbox. This allows a disk image to be accessed
  from within sandbox, e.g. for testing filesystems. The 'host bind' command
  can be used, so long as `CONFIG_CMDLINE` is enabled, but this flag can
  sometimes be useful in scripts.

  Typically a unique number as the label. The filename is delimited by colon.
  After that an optional hex blocksize can be provided (e.g. 200 for a normally
  512-byte block) and 'rem' can be used to mark the device as removeable.

  Example: -B 0:root.img:200:rem

-c, --command [<cmd>;]<cmd>
  To execute commands directly, use the -c option. You can specify a single
  command, or multiple commands separated by a semicolon, as is normal in
  U-Boot. Be careful with quoting as the shell will normally process and
  swallow quotes. When -c is used, U-Boot exits after the command is complete,
  but you can force it to go to interactive mode instead with -i.

-d, --fdt <device_tree>
  A device tree binary file can be provided with -d. If you edit the source
  (it is stored at arch/sandbox/dts/sandbox.dts) you must rebuild U-Boot to
  recreate the binary file.

-D, --default_fdt
  To use the default device tree, use -D.

-f, --soft_fail
  Continue running a unit test even after failure. This can be useful during
  development, when a unit tests contains a number of mostly independent
  asserts.

-F, --noflat
  Normally sandbox runs driver model tests first with livetree (if enabled),
  then with flattree. This is useful because the devicetree code used in each
  case is different. This flag disables the flattree run, so that the tests only
  run once. This is useful when iterating on a test where the test result is
  the same in both cases.

-h
  Show help about options

-i, --interative
  Go to interactive mode after executing the commands specified by -c.

-j, --jump <filename>
  Indicates that sandbox is being executed from another U-Boot executable, which
  has been written to a temporary file on disk. This can happen when U-Boot is
  packed into a firmware file and is extracted and run from SPL. The SPL phase
  writes an elf file containing the extracted portion, then execs it. This
  argument provides the filename, so it can be removed before U-Boot exits.

-k, --select_unittests <arg>
  Select specific unit tests to run. This is only used with SPL.

-K, --double_lcd
  Doubles the size of the emulated LCD, so that it appears bigger. This can be
  useful on large or high-resolution displays.

-l, --show_lcd
  Show the LCD emulation window.

-L, --log_evel <level>
  Sets the default logging level. This has no effect `CONFIG_LOG` is enabled.
  The levels are in `enum log_level_t` in `log.h`. For example `-L 7` will show
  all log statements at LOGL_DEBUG and below. The higher the number, the more
  info is shown.

-M, --no_mcheck
  Disable mcheck heap protection at runtime. When enabled, the mcheck wrapper
  functions pass through directly to the underlying allocator without adding
  headers or checking for corruption. This is useful for debugging when mcheck
  interferes with test results, such as when memory-leak detection reports false
  positives due to accumulated allocations from other tests.

-m, --memory <filename>
  Sets the location of the file which holds sandbox's emulated RAM. This can be
  read and written across phases, so that sandbox behaves like a normal board.

-n, --ignore_missing
  Ignore missing state on read, used with `-s`. This causes sandbox to continue
  execution even if there is no state file. If `-w` is used then the state will
  be written on exit.

-N, --native
  Use native mode when selecting EFI filenames and bootp identifiers. Normally
  sandbox uses its own values, but this option forces it to use the underlying
  architecture's values. For example, the default bootfile is normally
  'BOOTSBOX.EFI' on all platforms. Using -N on a 64-bit x86 platform would
  change the default bootfile to 'BOOTX64.EFI'

-p, --program <filename>
  Provides the program name that was originally executed to start sandbox.
  Where the program contains multiple phases packed into a single image (e.g.
  TPL, VPL, SPL, U-Boot), this provides the name of the original program, so
  that each phase can locate the correct executable for the next phase. Since
  each program is extracted from the original image and executed (see -j), this
  is the only way that subsequent phases can locate the full image.

-P, --pager_bypass
  Enable pager bypass mode for testing.

-Q, --quiet_vidconsole
  Don't use vidconsole for stdout/stderr. By default, sandbox outputs to both
  serial and vidconsole. This can be slow when there is a lot of output, due to
  truetype font rendering to the internal framebuffer. Use this option to use
  only serial output, which can provide a significant speedup for output-heavy
  operations.

-r, --read
  Read driver state from a dtb file. In conjunction with `-w`, this allows
  sandbox to save and restore emulated hardware state (such as a TPM) across
  each U-Boot phase.

--rm_memory
  Remove the memory file when starting up. This only has any effect if `-m` is
  used.

-s, --state <filename>
  Provides the filename of the state file. This is in devicetree format, with a
  node for each device which has written its state on exit. The file can be used
  to persist state across multiple test runs, or it can be used within a single
  run consisting of multiple U-Boot phases.

-S, --signals
  Handle signals in sandbox itself, rather than letting the controlling process
  handle them. Sandbox will then catch SIGILL, SIGBUS and SIGSEGV and report
  these errors itself.

-t, --terminal <arg>

  The terminal is normally in what is called 'raw-with-sigs' mode. This means
  that you can use arrow keys for command editing and history, but if you
  press Ctrl-C, U-Boot will exit instead of handling this as a keypress.
  Other options are 'raw' (so Ctrl-C is handled within U-Boot) and 'cooked'
  (where the terminal is in cooked mode and cursor keys will not work, Ctrl-C
  will exit).

-T, --test_fdt
  To use the test device tree, use -T.

--upl
  Enable support for Universal Payload Specification. This adjusts SPL to set
  up a SPL handof and pass it to U-Boot proper. This requires `-m` to be used,
  since the handoff information is provided in emulated RAM.

-u, --unittests
  Run SPL unittests. Normally when running `u-boot-spl` the tests are not
  executed, since it interferes with normal operation.

-v, --verbose
  Show console output from tests. Normally this is suppressed.

--video_frames <dir>
  Write video frames to the specified directory for debugging purposes. Each
  time video_compress_fb() is called, this writes a file called 'frame%d.bmp'
  to the given directory where %d is the sequence number starting from 0. Note
  that sandbox removes all 'frame%d.bmp' files in that directory on startup.

-V, --video_test <ms>
  Enable video test mode with a delay (in milliseconds) between assertions. This
  allows time to examine the display during testing.

-w, --write
  Write driver state to state file on exit. In conjunction with `-r`, this allows
  sandbox to save and restore emulated hardware state (such as a TPM) across
  each U-Boot phase.

-W, --title <title>
  Set the window title for the sandbox display.


Environment Variables
---------------------

UBOOT_SB_TIME_OFFSET
    This environment variable stores the offset of the emulated real time clock
    to the host's real time clock in seconds. The offset defaults to zero.

Memory Emulation
----------------

Memory emulation is supported, with the size set by CONFIG_SANDBOX_RAM_SIZE_MB.
The -m option can be used to read memory from a file on start-up and write
it when shutting down. This allows preserving of memory contents across
test runs. You can tell U-Boot to remove the memory file after it is read
(on start-up) with the --rm_memory option.

To access U-Boot's emulated memory within the code, use map_sysmem(). This
function is used throughout U-Boot to ensure that emulated memory is used
rather than the U-Boot application memory. This provides memory starting
at 0 and extending to the size of the emulation.


Storing State
-------------

With sandbox you can write drivers which emulate the operation of drivers on
real devices. Some of these drivers may want to record state which is
preserved across U-Boot runs. This is particularly useful for testing. For
example, the contents of a SPI flash chip should not disappear just because
U-Boot exits.

State is stored in a device tree file in a simple format which is driver-
specific. You then use the -s option to specify the state file. Use -r to
make U-Boot read the state on start-up (otherwise it starts empty) and -w
to write it on exit (otherwise the stored state is left unchanged and any
changes U-Boot made will be lost). You can also use -n to tell U-Boot to
ignore any problems with missing state. This is useful when first running
since the state file will be empty.

The device tree file has one node for each driver - the driver can store
whatever properties it likes in there. See 'Writing Sandbox Drivers' below
for more details on how to get drivers to read and write their state.


Running and Booting
-------------------

Since there is no machine architecture, sandbox U-Boot cannot actually boot
a kernel, but it does support the bootm command. Filesystems, memory
commands, hashing, FIT images, verified boot and many other features are
supported.

When 'bootm' runs a kernel, sandbox will exit, as U-Boot does on a real
machine. Of course in this case, no kernel is run.

It is also possible to tell U-Boot that it has jumped from a temporary
previous U-Boot binary, with the -j option. That binary is automatically
removed by the U-Boot that gets the -j option. This allows you to write
tests which emulate the action of chain-loading U-Boot, typically used in
a situation where a second 'updatable' U-Boot is stored on your board. It
is very risky to overwrite or upgrade the only U-Boot on a board, since a
power or other failure will brick the board and require return to the
manufacturer in the case of a consumer device.


Supported Drivers
-----------------

U-Boot sandbox supports these emulations:

- Arm FF-A
- Block devices
- Chrome OS EC
- GPIO
- Host filesystem (access files on the host from within U-Boot)
- I2C
- Keyboard (Chrome OS)
- LCD
- Network
- Serial (for console only)
- Sound (incomplete - see sandbox_sdl_sound_init() for details)
- SPI
- SPI flash
- TPM (Trusted Platform Module)

A wide range of commands are implemented. Filesystems which use a block
device are supported.

Also sandbox supports driver model (CONFIG_DM) and associated commands.


Sandbox Variants
----------------

There are unfortunately quite a few variants at present:

sandbox:
  should be used for most tests
sandbox64:
  special build that forces a 64-bit host
sandbox_flattree:
  builds with dev_read\_...() functions defined as inline.
  We need this build so that we can test those inline functions, and we
  cannot build with both the inline functions and the non-inline functions
  since they are named the same.
sandbox_spl:
  builds sandbox with SPL support, so you can run spl/u-boot-spl
  and it will start up and then load ./u-boot. It is also possible to
  run ./u-boot directly.

Of these sandbox_spl can probably be removed since it is a superset of sandbox.

Most of the config options should be identical between these variants.


Linux RAW Networking Bridge
---------------------------

The sandbox_eth_raw driver bridges traffic between the bottom of the network
stack and the RAW sockets API in Linux. This allows much of the U-Boot network
functionality to be tested in sandbox against real network traffic.

For Ethernet network adapters, the bridge utilizes the RAW AF_PACKET API.  This
is needed to get access to the lowest level of the network stack in Linux. This
means that all of the Ethernet frame is included. This allows the U-Boot network
stack to be fully used. In other words, nothing about the Linux network stack is
involved in forming the packets that end up on the wire. To receive the
responses to packets sent from U-Boot the network interface has to be set to
promiscuous mode so that the network card won't filter out packets not destined
for its configured (on Linux) MAC address.

The RAW sockets Ethernet API requires elevated privileges in Linux. You can
either run as root, or you can add the capability needed like so::

   sudo /sbin/setcap "CAP_NET_RAW+ep" /path/to/u-boot

The default device tree for sandbox includes an entry for eth0 on the sandbox
host machine whose alias is "eth1". The following are a few examples of network
operations being tested on the eth0 interface.

.. code-block:: none

   sudo /path/to/u-boot -D

   DHCP
   ....

   setenv autoload no
   setenv ethrotate no
   setenv ethact eth1
   dhcp

   PING
   ....

   setenv autoload no
   setenv ethrotate no
   setenv ethact eth1
   dhcp
   ping $gatewayip

   TFTP
   ....

   setenv autoload no
   setenv ethrotate no
   setenv ethact eth1
   dhcp
   setenv serverip WWW.XXX.YYY.ZZZ
   tftpboot u-boot.bin

The bridge also supports (to a lesser extent) the localhost interface, 'lo'.

The 'lo' interface cannot use the RAW AF_PACKET API because the lo interface
doesn't support Ethernet-level traffic. It is a higher-level interface that is
expected only to be used at the AF_INET level of the API. As such, the most raw
we can get on that interface is the RAW AF_INET API on UDP. This allows us to
set the IP_HDRINCL option to include everything except the Ethernet header in
the packets we send and receive.

Because only UDP is supported, ICMP traffic will not work, so expect that ping
commands will time out.

The default device tree for sandbox includes an entry for lo on the sandbox
host machine whose alias is "eth5". The following is an example of a network
operation being tested on the lo interface.

.. code-block:: none

   TFTP
   ....

   setenv ethrotate no
   setenv ethact eth5
   tftpboot u-boot.bin


SPI Emulation
-------------

Sandbox supports SPI and SPI flash emulation.

The device can be enabled via a device tree, for example::

    spi@0 {
            #address-cells = <1>;
            #size-cells = <0>;
            reg = <0 1>;
            compatible = "sandbox,spi";
            cs-gpios = <0>, <&gpio_a 0>;
            spi.bin@0 {
                    reg = <0>;
                    compatible = "spansion,m25p16", "jedec,spi-nor";
                    spi-max-frequency = <40000000>;
                    sandbox,filename = "spi.bin";
            };
    };

The file must be created in advance::

   $ dd if=/dev/zero of=spi.bin bs=1M count=2
   $ u-boot -T

Here, you can use "-T" or "-D" option to specify test.dtb or u-boot.dtb,
respectively, or "-d <file>" for your own dtb.

With this setup you can issue SPI flash commands as normal::

   =>sf probe
   SF: Detected M25P16 with page size 64 KiB, total 2 MiB
   =>sf read 0 0 10000
   SF: 65536 bytes @ 0x0 Read: OK

Since this is a full SPI emulation (rather than just flash), you can
also use low-level SPI commands::

   =>sspi 0:0 32 9f
   FF202015

This is issuing a READ_ID command and getting back 20 (ST Micro) part
0x2015 (the M25P16).

.. _sandbox_blk:

Block Device Emulation
----------------------

U-Boot can use raw disk images for block device emulation. To e.g. list
the contents of the root directory on the second partion of the image
"disk.raw", you can use the following commands::

   =>host bind 0 ./disk.raw
   =>ls host 0:2

The device can be marked removeable with 'host bind -r'.

A disk image can be created using the following commands::

   $> truncate -s 1200M ./disk.raw
   $> /usr/sbin/sgdisk --new=1:0:+64M --typecode=1:EF00 --new=2:0:0 --typecode=2:8300 disk.raw
   $> lodev=`sudo losetup -P -f --show ./disk.raw`
   $> sudo mkfs.vfat -n EFI -v ${lodev}p1
   $> sudo mkfs.ext4 -L ROOT -v ${lodev}p2

or utilize the device described in test/py/make_test_disk.py::

   #!/usr/bin/python
   import make_test_disk
   make_test_disk.makeDisk()

For more technical details, see :doc:`block_impl`.

Writing Sandbox Drivers
-----------------------

Generally you should put your driver in a file containing the word 'sandbox'
and put it in the same directory as other drivers of its type. You can then
implement the same hooks as the other drivers.

To access U-Boot's emulated memory, use map_sysmem() as mentioned above.

If your driver needs to store configuration or state (such as SPI flash
contents or emulated chip registers), you can use the device tree as
described above. Define handlers for this with the SANDBOX_STATE_IO macro.
See arch/sandbox/include/asm/state.h for documentation. In short you provide
a node name, compatible string and functions to read and write the state.
Since writing the state can expand the device tree, you may need to use
state_setprop() which does this automatically and avoids running out of
space. See existing code for examples.


VPL (Verifying Program Loader)
------------------------------

Sandbox provides an example build of vpl called `sandbox_vpl`. To build it:

.. code-block:: bash

   make sandbox_vpl_defconfig all

This can be run using:

.. code-block:: bash

   ./tpl/u-boot-tpl -d u-boot.dtb

It starts up TPL (first-stage init), then VPL, then runs SPL and finally U-Boot
proper, following the normal flow for a verified boot. At present, no
verification is actually implemented.

Here is an example trace::

   U-Boot TPL 2024.01-rc2-00129 (Nov 19 2023 - 08:10:12 -0700)
   Trying to boot from sandbox_image
   Trying to boot from sandbox_file

   U-Boot VPL 2024.01-rc2-00129 (Nov 19 2023 - 08:10:12 -0700)
   Trying to boot from vbe_simple
   Trying to boot from sandbox_image
   Trying to boot from sandbox_file

   U-Boot SPL 2024.01-rc2-00129 (Nov 19 2023 - 08:10:12 -0700)
   Trying to boot from vbe_simple
   Trying to boot from sandbox_image
   Trying to boot from sandbox_file


   U-Boot 2024.01-rc2-00129 (Nov 19 2023 - 08:10:12 -0700)

   Reset Status: COLD
   Model: sandbox
   DRAM:  256 MiB
   using memory 0x1b576000-0x1f578000 for malloc()

   Warning: host_lo MAC addresses don't match:
   Address in ROM is		96:cd:ef:82:78:51
   Address in environment is	02:00:11:22:33:44
   Core:  103 devices, 51 uclasses, devicetree: board
   MMC:
   Loading Environment from nowhere... OK
   In:    serial,cros-ec-keyb,usbkbd
   Out:   serial,vidconsole
   Err:   serial,vidconsole
   Model: sandbox
   Net:   eth0: host_lo, eth1: host_enp14s0, eth2: host_eth6, eth3: host_wlp15s0, eth4: host_virbr0, eth5: host_docker0, eth6: eth@10002000
   Hit any key to stop autoboot:  1


Backtrace Support
-----------------

Sandbox supports printing a backtrace of the current call stack, which can be
useful for debugging. The :doc:`backtrace <../../usage/cmd/backtrace>` command
prints a backtrace showing function names, source files, and line numbers.

This uses the libbacktrace library (bundled with GCC) to provide detailed symbol
information, including for static functions.

To use it, simply run::

    => backtrace

This command is enabled with ``CONFIG_CMD_BACKTRACE``.


Debugging the init sequence
---------------------------

If you get a failure in the initcall sequence, like this::

   initcall sequence 0000560775957c80 failed at call 0000000000048134 (err=-96)

Then you use can use grep to see which init call failed, e.g.::

   $ grep 0000000000048134 u-boot.map
   stdio_add_devices

Of course another option is to run it with a debugger such as gdb::

   $ gdb u-boot
   ...
   (gdb) br initcall.h:41
   Breakpoint 1 at 0x4db9d: initcall.h:41. (2 locations)

Note that two locations are reported, since this function is used in both
board_init_f() and board_init_r().

.. code-block:: none

   (gdb) r
   Starting program: /tmp/b/sandbox/u-boot
   [Thread debugging using libthread_db enabled]
   Using host libthread_db library "/lib/x86_64-linux-gnu/libthread_db.so.1".

   U-Boot 2018.09-00264-ge0c2ba9814-dirty (Sep 22 2018 - 12:21:46 -0600)

   DRAM:  128 MiB
   MMC:

   Breakpoint 1, initcall_run_list (init_sequence=0x5555559619e0 <init_sequence_f>)
       at /scratch/sglass/cosarm/src/third_party/u-boot/files/include/initcall.h:41
   41                              printf("initcall sequence %p failed at call %p (err=%d)\n",
   (gdb) print *init_fnc_ptr
   $1 = (const init_fnc_t) 0x55555559c114 <stdio_add_devices>
   (gdb)


This approach can be used on normal boards as well as sandbox.

For debugging with GDB or LLDB, it is preferable to reduce the compiler
optimization level (CONFIG_CC_OPTIMIZE_FOR_DEBUG=y) and to disable Link Time
Optimization (CONFIG_LTO=n).

SDL_CONFIG
----------

If sdl-config is on a different path from the default, set the SDL_CONFIG
environment variable to the correct pathname before building U-Boot.


.. _sandbox_valgrind:

Using valgrind / memcheck
-------------------------

It is possible to run U-Boot under valgrind to check memory allocations::

    valgrind ./u-boot

However, this does not give very useful results. The sandbox allocates a memory
pool via mmap(). U-Boot's internal malloc() and free() work on this memory pool.
Custom allocators and deallocators are invisible to valgrind by default. To
expose U-Boot's malloc() and free() to valgrind, enable ``CONFIG_VALGRIND``.
Enabling this option will inject placeholder assembler code which valgrind
interprets. This is used to annotate sections of memory as safe or unsafe, and
to inform valgrind about malloc()s and free()s. There are currently no standard
placeholder assembly sequences for RISC-V, so this option cannot be enabled on
that architecture.

Malloc's bookkeeping information is marked as unsafe by default. However, this
will generate many false positives when malloc itself accesses this information.
These warnings can be suppressed with::

    valgrind --suppressions=scripts/u-boot.supp ./u-boot

Additionally, you may experience false positives if U-Boot is using a smaller
pointer size than your host architecture. This is because the pointers used by
U-Boot will only contain 32 bits of addressing information. When interpreted as
64-bit pointers, valgrind will think that they are not initialized properly. To
fix this, enable ``CONFIG_SANDBOX64`` (such as via ``sandbox64_defconfig``)
when running on a 64-bit host.

Additional options
^^^^^^^^^^^^^^^^^^

The following valgrind options are useful in addition to the above examples:

``--trace-childen=yes``
    tells valgrind to keep tracking subprocesses, such
    as when U-Boot jumps from TPL to SPL, or from SPL to U-Boot proper.

``--track-origins=yes``
    will (for a small overhead) tell valgrind to keep
    track of who allocated some troublesome memory.

``--error-limit``
    will enable printing more than 1000 errors in a single session.

``--vgdb=yes --vgdb-error=0``
    will let you use GDB to attach like::

        gdb -ex "target remote | vgdb" u-boot

    This is very helpful for inspecting the program state when there is
    an error.

The following U-Boot option are also helpful:

``-Tc 'ut all'``
    lets U-Boot run unit tests automatically. Note
    that not all unit tests will succeed in the default configuration.

``-t cooked``
    will keep the console in a sane state if you
    terminate it early (instead of having to run tset).

Future work
^^^^^^^^^^^

The biggest limitation to the current approach is that supressions don't
"un-taint" uninitialized memory accesses. Currently, dlmalloc's bookkeeping
information is marked as a "red zone." This means that all reads to that zone
are marked as illegal by valgrind. This is fine for regular code, but dlmalloc
really does need to access this area, so we suppress its violations. However, if
dlmalloc then passes a result calculated from a "tainted" access, that result is
still tainted. So the first accessor will raise a warning. This means that every
construct like

.. code-block::

    foo = malloc(sizeof(*foo));
    if (!foo)
        return -ENOMEM;

will raise a warning when we check the result of malloc. Whoops.

There are at least four possible ways to address this:

* Don't mark dlmalloc bookkeeping information as a red zone. This is the
  simplest solution, but reduces the power of valgrind immensely, since we can
  no longer determine that (e.g.) access past the end of an array is undefined.
* Implement red zones properly. This would involve growing every allocation by a
  fixed amount (16 bytes or so) and then using that extra space for a real red
  zone that neither regular code nor dlmalloc needs to access. Unfortunately,
  this would probably some fairly intensive surgery to dlmalloc to add/remove
  the offset appropriately.
* Mark bookkeeping information as valid before we use it in dlmalloc, and then
  mark it invalid before returning. This would be the most correct, but it would
  be very tricky to implement since there are so many code paths to mark. I
  think it would be the most effort out of the three options here.
* Use the host malloc and free instead of U-Boot's custom allocator. This will
  eliminate the need to annotate dlmalloc. However, using a different allocator
  for sandbox will mean that bugs in dlmalloc will only be tested when running
  on real (or emulated) hardware.

Until one of the above options are implemented, it will remain difficult
to sift through the massive amount of spurious warnings.

Testing
-------

U-Boot sandbox can be used to run various tests, mostly in the test/
directory.

See :doc:`../../develop/tests_sandbox` for more information and
:doc:`../../develop/testing` for information about testing generally.


Memory Map
----------

Sandbox has its own emulated memory starting at 0. Here are some of the things
that are mapped into that memory:

========   ========================   ===============================
Addr       Config                     Usage
========   ========================   ===============================
    b000   CONFIG_BLOBLIST_ADDR       Blob list
   10000   CFG_MALLOC_F_ADDR          Early memory allocation
   f0000   CONFIG_PRE_CON_BUF_ADDR    Pre-console buffer
   f4000   CFG_MALLOC_F_ADDR          Early memory allocation
  100000   TCG Event log              TCG Event Log
  180000   CONFIG_SYS_FDT_LOAD_ADDR   Device tree
  200000   CONFIG_TRACE_EARLY_ADDR    Early trace buffer (if enabled). Also used
  400000   CONFIG_TEXT_BASE           Load buffer for U-Boot (sandbox_spl only)
 4000000   SANDBOX_SPL_FIT_ADDR       FIT read by the "full" FIT-loader test
                                      (sandbox_spl only)
 8000000                              nvmxip devices (see test.dts)
10000000                              PCI address space (see test.dts)
20000000                              PCI EA space (see PCI_CAP_EA_BASE_LO0)

ff000000                              Memory-mapping tags start here
========   ========================   ===============================
