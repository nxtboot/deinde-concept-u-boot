.. SPDX-License-Identifier: GPL-2.0+:

BLS Bootmeth
============

The `Boot Loader Specification`_ (BLS) bootmeth allows U-Boot to boot
operating systems configured with BLS Type #1 entries. This format is used by
Fedora, RHEL and other distributions.

.. _Boot Loader Specification: https://uapi-group.org/specifications/specs/boot_loader_specification/

For each partition the bootmeth tries every boot prefix (``{"/", "/boot/"}``
by default; selectable via the ``filename-prefixes`` property on the bootstd
device). Within each prefix it scans ``loader/entries/`` for ``.conf`` files
first, then falls back to the singular ``loader/entry.conf`` if the directory
is missing or empty.

The bootmeth sets two flags at bind time:

* ``BOOTMETHF_MULTI`` -- each ``.conf`` file in the entries directory produces
  a separate bootflow, with ``bflow->entry`` indexing which one is returned.
* ``BOOTMETHF_ANY_PART`` -- the iterator visits every partition rather than
  only bootable partitions and the ESP / XBOOTLDR / MBR-0xea targets carved out
  by ``part_is_bls_target()``. Distros that drive their entries through
  ``kernel-install`` typically place ``loader/entries/`` on whichever partition
  holds ``/boot``, which on Debian/Ubuntu layouts is the ext4 rootfs rather
  than the ESP, and ``BOOTMETHF_ANY_PART`` is what lets BLS reach those.

Because ``BOOTMETHF_ANY_PART`` invites the bootmeth onto every partition,
``bls_read_bootflow()`` looks the partition up before mounting and returns
``-ENOENT`` for partitions below ``BLS_MIN_PART_BLOCKS`` blocks. This avoids
running the ext4 superblock probe (which reads sector 2) on raw single-LBA
slots like ChromeOS kernel partitions, where the probe would otherwise trip
``fs_devread()``'s "Read outside partition" error path.

When invoked on a bootdev, the ``bls_read_bootflow()`` function searches for an
entry file, reads it and passes it to ``bls_parse_entry()`` which processes
the key-value pairs into a ``struct bls_entry``. The parser uses an enum-based
token lookup to map field names, with most values pointing directly into the
bootflow buffer (zero-copy). Only ``options`` is allocated separately since
multiple occurrences are concatenated. Unknown fields are silently ignored for
forward compatibility. Images (kernel, initrd, devicetree) are registered in the
bootflow with ``bootflow_img_add()`` during discovery but not loaded until boot.

At boot time, ``bls_to_pxe_label()`` converts the bootflow into a PXE label
structure, mapping BLS fields to their PXE equivalents (``title`` to menu,
``options`` to append, etc.). The existing ``pxe_load_files()`` and
``pxe_boot()`` infrastructure then handles file loading and execution, including
FIT support.

The compatible string "u-boot,boot-loader-specification" is used for the driver.
It is present if `CONFIG_BOOTMETH_BLS` is enabled.

See :doc:`/usage/bls` for usage details and field reference.
