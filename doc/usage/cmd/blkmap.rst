.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: blkmap (command)

blkmap command
==============

Synopsis
--------

::

    blkmap info
    blkmap part
    blkmap dev [<dev>]
    blkmap read <addr> <blk#> <cnt>
    blkmap write <addr> <blk#> <cnt>
    blkmap get <label> dev [<var>]
    blkmap create <label>
    blkmap destroy <label>
    blkmap map <label> <blk#> <cnt> linear <interface> <dev> <blk#>
    blkmap map <label> <blk#> <cnt> mem <addr>

Description
-----------

The *blkmap* command is used to create and manage virtual block devices that
are composed of one or more "slices" mapped from other block devices or memory
regions.

See :doc:`../blkmap` for an overview of the blkmap subsystem and usage examples.

label
    Unique label assigned to a blkmap device during creation. Used to identify
    the device for subsequent operations (map, get, destroy).

blk#
    Block number. Blocks are 512 bytes in size. When used as a starting position,
    this specifies where in the device to begin the operation. When used as a
    mapping parameter, it indicates which block in the source or destination to
    use.

cnt
    Number of blocks (count). Each block is 512 bytes, so cnt=1 represents 512
    bytes, cnt=2048 represents 1MB, etc.

addr
    Memory address in hexadecimal format (e.g., ${loadaddr}, 0x82000000). Used
    for specifying where to read/write data or where a memory region is located.

blkmap info
~~~~~~~~~~~

List all configured blkmap devices and their properties::

    blkmap info

This displays information about all active blkmap devices, including device
number, vendor, product, revision, type, and capacity.

blkmap part
~~~~~~~~~~~

List available partitions on the current blkmap device::

    blkmap part

This command displays the partition table of the currently selected blkmap
device. If the device has no partition table (whole-disk filesystem), it will
report "no blkmap partition table available".

blkmap dev
~~~~~~~~~~

Show or set the current blkmap device::

    blkmap dev [<dev>]

dev
    Optional device number to set as current. If omitted, displays the current
    device information.

blkmap read
~~~~~~~~~~~

Read data from the current blkmap device::

    blkmap read <addr> <blk#> <cnt>

blkmap write
~~~~~~~~~~~~

Write data to the current blkmap device::

    blkmap write <addr> <blk#> <cnt>

**Note**: Write support is limited and may not be available for all blkmap
types.

blkmap get
~~~~~~~~~~

Get the device number for a labeled blkmap device::

    blkmap get <label> dev [<var>]

var
    Optional environment variable name to store the device number. If omitted,
    the device number is printed to stdout.

This is useful for scripting, allowing you to find a blkmap device by its label
and store or use its device number.

blkmap create
~~~~~~~~~~~~~

Create a new blkmap device with the specified label::

    blkmap create <label>

After creation, the device has no mappings and is empty. Use ``blkmap map``
to add slices to the device.

blkmap destroy
~~~~~~~~~~~~~~

Destroy a blkmap device and free its resources::

    blkmap destroy <label>

This removes the blkmap device and all its mappings. Any data stored only in
the blkmap device will be lost.

blkmap map - linear
~~~~~~~~~~~~~~~~~~~

Map a region from another block device into the blkmap device::

    blkmap map <label> <blk#> <cnt> linear <interface> <dev> <blk#>

label
    Label of the blkmap device to map into

blk#
    Starting block number in the blkmap device where this mapping begins

cnt
    Number of blocks to map

interface
    Source device interface (e.g., mmc, usb, scsi)

dev
    Source device number

blk# (last parameter)
    Starting block number on the source device

This creates a linear mapping that redirects reads from a region of the blkmap
device to the corresponding region on another block device. Multiple mappings
can be added to compose a single virtual device from multiple sources.

blkmap map - mem
~~~~~~~~~~~~~~~~

Map a memory region as a block device::

    blkmap map <label> <blk#> <cnt> mem <addr>

label
    Label of the blkmap device to map into

blk#
    Starting block number in the blkmap device where this mapping begins

cnt
    Number of blocks to map

addr
    Memory address of the data to map

This creates a mapping that exposes a memory region as block device sectors.
The memory region must be at least ``cnt * 512`` bytes in size.

Usage Examples
--------------

See :doc:`../blkmap` for complete examples including:

* Netbooting an Ext4 image
* Accessing filesystems inside FIT images
* Creating composite devices from multiple sources
* Using memory-backed block devices

Implementation Details
----------------------

Blkmap devices are implemented as standard U-Boot block devices (UCLASS_BLK)
with a custom driver. Each device maintains an ordered list of "slices" -
mappings from a range of blocks to a source (another device or memory region).

See :doc:`../blkmap` for more details on the implementation.

Configuration
-------------

The blkmap command is available when CONFIG_CMD_BLKMAP is enabled::

    CONFIG_BLKMAP=y
    CONFIG_CMD_BLKMAP=y

Return Value
------------

The return value $? is set to 0 on success, 1 on failure.

Examples
--------

List all blkmap devices
~~~~~~~~~~~~~~~~~~~~~~~

::

    => blkmap info
    Device 0: Vendor: U-Boot Rev: 1.0 Prod: blkmap
                Type: Hard Disk
                Capacity: 60.0 MB = 0.0 GB (122880 x 512)

Check or set current device
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

::

    => blkmap dev
    Device 0: Vendor: U-Boot Rev: 1.0 Prod: blkmap
                Type: Hard Disk
                Capacity: 60.0 MB = 0.0 GB (122880 x 512)
    ... is now current device

    => blkmap dev 0
    Device 0: Vendor: U-Boot Rev: 1.0 Prod: blkmap
                Type: Hard Disk
                Capacity: 60.0 MB = 0.0 GB (122880 x 512)
    ... is now current device

Create a new device
~~~~~~~~~~~~~~~~~~~

::

    => blkmap create mydisk
    => blkmap info
    Device 0: Vendor: U-Boot Rev: 1.0 Prod: blkmap
                Type: Hard Disk
                Capacity: 0.0 MB = 0.0 GB (1 x 512)

Map a linear region from MMC
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Map MMC blocks 1000-1999 to blkmap blocks 0-999::

    => blkmap create composed
    => blkmap map composed 0 1000 linear mmc 0 1000

Map memory as a block device
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Map 1MB of memory as blocks::

    => blkmap create ramdisk
    => blkmap map ramdisk 0 2048 mem ${loadaddr}

Read from a blkmap device
~~~~~~~~~~~~~~~~~~~~~~~~~~

::

    => blkmap dev 0
    => blkmap read ${loadaddr} 0 10
    blkmap read: device 0 block # 0, count 10 ... 10 blocks read: OK

Get device number by label
~~~~~~~~~~~~~~~~~~~~~~~~~~~

::

    => blkmap get testdev dev mydev
    => printenv mydev
    mydev=0

Complete workflow example
~~~~~~~~~~~~~~~~~~~~~~~~~~

Create, map, and use a device::

    => blkmap create testdev
    => blkmap map testdev 0 2048 linear mmc 0 2048
    => blkmap dev 0
    => blkmap read ${loadaddr} 0 1

Destroy a device
~~~~~~~~~~~~~~~~

::

    => blkmap destroy mydisk

Accessing whole-disk filesystems
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Blkmap devices often contain whole-disk filesystems (no partition table).
Access them using partition 0 or omitting the partition specification::

    => ls blkmap 0 /
    => ext4load blkmap 0 ${loadaddr} /boot/vmlinuz
    => cat blkmap 0 /etc/config.txt

See Also
--------

* :doc:`clone<clone>` - Copying raw blocks between devices, for which a blkmap
  device makes a convenient source or destination
* :doc:`../blkmap` - Blkmap device documentation and examples
* :doc:`../luks` - LUKS encryption support (uses blkmap for decrypted access)
* :doc:`../tkey-fde` - TKey full disk encryption
