.. SPDX-License-Identifier: GPL-2.0+

Boot Loader Specification (BLS)
===============================

U-Boot supports Boot Loader Specification (BLS) Type #1 boot entries as defined
in the `Boot Loader Specification`_.

.. _Boot Loader Specification: https://uapi-group.org/specifications/specs/boot_loader_specification/

Overview
--------

BLS provides a standardised way to describe boot entries. U-Boot's BLS support
allows it to boot operating systems configured with BLS entries, which is used
by Fedora, RHEL, Ubuntu (via ``kernel-install``) and other distributions.

U-Boot scans both the standard ``loader/entries/<machine-id>-<version>.conf``
layout used by ``kernel-install`` and a single ``loader/entry.conf`` fallback
under each boot prefix. When an entries directory is present, each ``.conf``
file becomes a separate bootflow; ``loader/entry.conf`` is consulted only when
the directory is absent or empty.

Configuration
-------------

Enable BLS support with::

    CONFIG_BOOTMETH_BLS=y

This automatically selects ``CONFIG_PXE_UTILS`` for booting.

Discovery
---------

The BLS bootmeth sets ``BOOTMETHF_ANY_PART``, so the bootflow iterator hands
it every partition on each bootdev rather than just the bootable partition or
those typed as ESP / XBOOTLDR / MBR-0xea. This matches the way ``kernel-install``
places entries: distros that mount the ESP at ``/boot/efi`` keep
``loader/entries/`` on the ext4 rootfs at ``/boot/loader/entries/``, while
distros that mount the ESP at ``/boot`` keep them on the ESP at
``loader/entries/``. Both cases are reached without any board configuration.

For each partition the bootmeth tries each filename prefix configured on the
bootstd device (``/`` and ``/boot/`` by default; see ``filename-prefixes`` in
:doc:`/develop/bootstd/overview`) and within each prefix tries the entries
directory first, then the singular ``entry.conf`` fallback::

    <prefix>loader/entries/*.conf
    <prefix>loader/entry.conf

Partitions too small to hold any filesystem are filtered out before the
filesystem probe runs, so raw data partitions (for example ChromeOS kernel
slots, which are a single LBA) do not produce spurious "Read outside
partition" probe errors.

BLS Entry Format
----------------

BLS entries use a simple key-value format, one field per line. Lines starting
with ``#`` are comments. Example::

    title Fedora Linux 39
    version 6.7.0-1.fc39.x86_64
    options root=/dev/sda3 ro quiet
    linux /vmlinuz-6.7.0-1.fc39.x86_64
    initrd /initramfs-6.7.0-1.fc39.x86_64.img
    devicetree /dtbs/6.7.0-1.fc39.x86_64/board.dtb

Supported Fields
----------------

**Required (at least one):**

* ``linux`` - Path to Linux kernel image; supports FITs with
  ``path#config`` syntax
* ``fit`` - Path to FIT (U-Boot extension, not in the BLS spec)

**Optional:**

* ``title`` - Human-readable menu display name
* ``version`` - OS version identifier (parsed but not used for sorting)
* ``options`` - Kernel command line parameters (may appear multiple times; all
  occurrences are concatenated)
* ``initrd`` - Initial ramdisk path (may appear multiple times; all initrds
  are loaded)
* ``devicetree`` - Device tree blob path
* ``devicetree-overlay`` - Device tree overlays (parsed but not yet supported)
* ``architecture`` - Target architecture (parsed but not used for filtering)
* ``machine-id`` - OS identifier (parsed but not used for filtering)
* ``sort-key`` - Primary sorting key (parsed but not used for sorting)

**Not supported:**

These fields relate to `Unified Kernel Images`_ (UKIs), which combine a UEFI
boot stub, kernel, initrd and other resources into a single UEFI PE file. They
are not currently supported by U-Boot:

* ``efi`` - EFI program path
* ``uki`` - Unified Kernel Image path
* ``uki-url`` - Remote UKI reference
* ``profile`` - Multi-profile UKI selector

Fields that support multiple occurrences:

* ``options`` - All values are concatenated with spaces
* ``initrd`` - All paths are loaded consecutively in memory

.. _Unified Kernel Images: https://uapi-group.org/specifications/specs/unified_kernel_image/

U-Boot Extensions
-----------------

The following fields are U-Boot extensions not defined in the BLS spec:

* ``fit`` - Specifies a FIT path, as an alternative to ``linux``. When
  ``fit`` is present it takes priority over ``linux``. This allows the entry to
  explicitly indicate that the image is a FIT, rather than relying on the
  ``path#config`` syntax in the ``linux`` field.

Example::

    title Ubuntu 24.04
    version 6.8.0
    fit /boot/ubuntu-6.8.0.fit
    options root=/dev/sda3 ro quiet
    initrd /boot/initrd-6.8.0.img

FIT Support
-----------

FITs can be specified in two ways:

1. Using the ``linux`` field with ``path#config`` syntax::

    linux /boot/image.fit#config-1

2. Using the ``fit`` field (U-Boot extension)::

    fit /boot/image.fit

The PXE boot infrastructure handles FIT parsing automatically in both cases.
The second option is preferred since the standard 'best match' algorithm
(enabled by ``CONFIG_FIT_BEST_MATCH=y``) should normally used to select the
correct configuration.

Usage
-----

BLS boot entries are discovered automatically by standard boot::

    => bootflow scan
    => bootflow list
    => bootflow select 0
    => bootflow boot

Each ``.conf`` file in a discovered ``loader/entries/`` directory, or the
single ``loader/entry.conf`` fallback, becomes its own bootflow.

Implementation Notes
--------------------

* Boot execution reuses U-Boot's PXE infrastructure for kernel loading
* Unknown fields are ignored for forward compatibility
* The bootmethod is ordered as ``bootmeth_2bls`` (after extlinux)
* Zero-copy parsing: most fields point into bootflow buffer (except ``options``
  which is allocated for concatenation)
* ``BOOTMETHF_MULTI`` is set, so each ``.conf`` file in the entries directory
  produces a separate bootflow
* ``BOOTMETHF_ANY_PART`` is set, so the iterator visits every partition rather
  than only those tagged as bootable or as BLS-target

Current Limitations
-------------------

* Only the first ``.conf`` file in ``loader/entries/`` is loaded per call;
  the iterator advances ``bflow->entry`` to retrieve subsequent ones
* No devicetree-overlay support
* No architecture/machine-id filtering
* No version-based or sort-key sorting
* No `Unified Kernel Image`_ (UKI) support

.. _Unified Kernel Image: https://uapi-group.org/specifications/specs/unified_kernel_image/

See Also
--------

* :doc:`/develop/bootstd/index`
* :doc:`/usage/cmd/bootflow`
* `Boot Loader Specification <https://uapi-group.org/specifications/specs/boot_loader_specification/>`_
* `Unified Kernel Image <https://uapi-group.org/specifications/specs/unified_kernel_image/>`_
