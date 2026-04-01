.. SPDX-License-Identifier: GPL-2.0+

Virtual Filesystem (VFS)
========================

Overview
--------

The VFS provides a unified path namespace for U-Boot, allowing multiple
filesystems to be mounted at different paths and accessed through a single
interface. It is inspired by the Linux VFS but heavily simplified for
U-Boot's needs.

Without the VFS, each filesystem device (UCLASS_FS) is accessed
independently using the legacy ``<interface> <dev:part>`` syntax. The VFS
adds:

- A VFS root directory at ``/``
- A mount tree where UCLASS_MOUNT devices are children of the directory
  they are mounted on
- Path resolution that walks the mount tree following each component
- Current working directory with ``.`` and ``..`` support
- Filesystem auto-detection from block devices
- A full set of commands: ``mount``, ``umount``, ``ls``, ``cd``, ``pwd``,
  ``cat``, ``load``, ``save``, ``size``, ``stat``, ``mkdir``, ``rm``,
  ``mv``, ``df``, and ``fs cp``
- Tab completion for all path arguments
- Boot integration with standard boot (bootflow)


Architecture
------------

The VFS is built on the U-Boot driver model:

UCLASS_MOUNT
    Each mount point is a DM device whose uclass-private data is a
    ``struct vfsmount``. Mount devices are children of the UCLASS_DIR
    device they are mounted on (or the VFS root directory).

VFS root (UCLASS_DIR)
    A special standalone directory device at the top of the mount tree.
    Created automatically by ``vfs_init()``. Has no parent filesystem -
    mount points appear as its children.

The device hierarchy after mounting looks like this::

    dm_root()
    ├── vfs_root         (UCLASS_DIR, created by vfs_init)
    │   └── mount.0      (UCLASS_MOUNT, name="host") → hostfs
    ├── hostfs            (UCLASS_FS, from device tree)
    │   └── hostfs.dir    (UCLASS_DIR)
    │       └── mount.1   (UCLASS_MOUNT, name="data") → ext4_fs.0
    ├── ext4_fs.0         (UCLASS_FS, from fs_mount_blkdev)

Nested mounts are supported: mounting at ``/host/data`` creates a mount
device as a child of the hostfs root DIR.


Linux naming
~~~~~~~~~~~~

Where practical, Linux VFS names are reused:

=========================  ==========================================
Linux                      U-Boot
=========================  ==========================================
``struct vfsmount``        ``struct vfsmount`` (UCLASS_MOUNT uc priv)
``struct mount``           ``struct udevice`` (UCLASS_MOUNT)
``struct super_block``     ``struct udevice`` (UCLASS_FS)
``vfs_kern_mount()``       ``vfs_mount()``
``init_mount_tree()``      ``vfs_init()``
=========================  ==========================================


Path resolution
~~~~~~~~~~~~~~~

``vfs_find_mount()`` walks the mount tree from the VFS root, checking each
path component for a child mount. When a mount is found, resolution
continues into the mounted filesystem's root directory.

For example, with mounts at ``/host`` and ``/host/data``::

    /host/data/file.txt
    ├── vfs_root → "host" mount → hostfs root DIR
    ├── hostfs root DIR → "data" mount → ext4 root DIR
    └── "file.txt" → resolved within ext4

Relative paths are resolved against the current working directory.
``.`` and ``..`` components are canonicalised before resolution.


Filesystem drivers
~~~~~~~~~~~~~~~~~~

Four filesystem drivers are included:

**Sandbox hostfs** (``fs/sandbox/sandboxfs.c``)
    Provides access to the host filesystem. Supports the full FILE
    uclass (read_iter, write_iter) and all fs_ops (mkdir, unlink,
    rename, readlink). Used for testing.

**ext4l** (``fs/ext4l/fs.c``)
    Wraps the Linux-ported ext4 implementation. Supports path-level
    read/write (read_file, write_file), mkdir, unlink, rename, statfs,
    and directory listing. Follows symlinks internally.

**ext4** (``fs/ext4/fs.c``)
    Wraps the original ext4 implementation. Supports read/write,
    unlink and symlink creation. Available on boards that do not use
    the ext4l driver.

**FAT** (``fs/fat/fs.c``)
    Wraps the existing FAT implementation. Supports path-level
    read/write, mkdir, unlink, rename and statfs.

Block-backed drivers are auto-detected by iterating all ``UCLASS_FS``
drivers whose name ends in ``_fs``.


Commands
--------

When ``CONFIG_CMD_VFS`` is enabled, the following commands are available.
They replace the legacy commands (which remain available via
``CONFIG_CMD_FS_LEGACY`` for boards without VFS).

===================  ================================================
Command              Description
===================  ================================================
``mount``            List mounts, or mount a filesystem
``umount``           Unmount a filesystem (``-a`` for all)
``ls``               List directory contents
``cd``               Change working directory
``pwd``              Print working directory
``cat``              Print file contents
``load``             Load file into memory
``save``             Save memory to file
``size``             Get file size (sets ``$filesize``)
``stat``             Show file/directory metadata
``mkdir``            Create a directory
``rm``               Delete a file
``mv``               Rename/move a file or directory
``df``               Show filesystem usage statistics
``fs cp``            Copy a file (cross-filesystem supported)
===================  ================================================

All path-taking commands support tab completion and relative paths.


API
---

All functions are declared in ``include/vfs.h``.

Initialisation and mounting
~~~~~~~~~~~~~~~~~~~~~~~~~~~

``vfs_init()``
    Initialise the VFS. Creates the root directory device. Safe to call
    multiple times.

``vfs_mount(path, dev)``
    Mount a UCLASS_FS device at a path. Creates a UCLASS_MOUNT device
    as a child of the parent directory.

``vfs_umount(path)``
    Unmount and clean up the FS device and its children.

``vfs_umount_all()``
    Unmount all filesystems.

``fs_mount_blkdev(type, desc, part_num, part, mountpoint)``
    Create and mount a block-backed FS device with explicit type.

``fs_mount_blkdev_auto(desc, part_num, part, mountpoint)``
    Auto-detect filesystem type and mount.

Path resolution
~~~~~~~~~~~~~~~

``vfs_find_mount(path, &mnt, &subpath)``
    Walk the mount tree to find the deepest mount covering a path.

``vfs_open_file(path, oflags, &fil)``
    Open a file by VFS path. Returns a UCLASS_FILE device that can
    be read/written with ``file_read_at()`` / ``file_write_at()``.

``vfs_stat(path, &dent)``
    Get file/directory metadata.

``vfs_readlink(path, buf, size)``
    Read a symbolic link target.

Directory and working directory
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``vfs_ls(path)``
    List directory contents including child mount points.

``vfs_chdir(path)``
    Change the current working directory.

``vfs_getcwd()``
    Get the current working directory.

``vfs_mkdir(path)``
    Create a directory.

``vfs_unlink(path)``
    Delete a file.

``vfs_rename(old, new)``
    Rename/move (must be on same mount).

``vfs_statfs(path, &stats)``
    Get filesystem usage statistics.


Configuration
-------------

CONFIG_VFS
    Enable the VFS layer. Depends on CONFIG_FS and CONFIG_DIR.
    Default ``y`` for sandbox.

CONFIG_CMD_VFS
    Enable VFS commands (mount, ls, cat, etc.) and the ``fs``
    subcommand. Depends on CONFIG_VFS. Default ``y`` for sandbox.

CONFIG_CMD_FS_LEGACY
    Legacy filesystem commands using interface/dev:part syntax.
    Automatically disabled when VFS is enabled.


Boot integration
----------------

When a block-backed filesystem is mounted via VFS, a bootdev is
automatically created for it. The standard boot system (``bootflow
scan``) can then discover boot files (``extlinux/extlinux.conf``,
``boot.scr``, etc.) through existing bootmeths.

The fs bootdev works by exposing its underlying block device and
partition number to the bootmeth, so existing bootmeths (extlinux,
script, EFI) work unchanged.

To include VFS-mounted filesystems in the boot order::

    => mount host 0:0 /boot
    => bootdev order 'fs'
    => bootflow scan -l


Testing
-------

The VFS has extensive tests in ``test/dm/fs.c``:

============================  =========================================
Test                          What it covers
============================  =========================================
``dm_test_vfs_ls``            C API: init, mount, ls, umount
``dm_test_vfs_cmd``           Same flow via ``fs`` command
``dm_test_vfs_cd``            cd, pwd, relative paths, ``.`` and ``..``
``dm_test_vfs_path``          Multi-component paths (subdirectories)
``dm_test_vfs_cat``           cat command
``dm_test_vfs_save``          save + load round-trip
``dm_test_vfs_cp``            fs cp (same filesystem)
``dm_test_vfs_cross_cp``      fs cp between hostfs and ext4
``dm_test_vfs_rm``            rm command
``dm_test_vfs_mv``            mv command
``dm_test_vfs_mkdir``         mkdir command
``dm_test_vfs_stat``          stat command
``dm_test_vfs_symlink``       Symlink following and readlink
``dm_test_vfs_df``            df command (ext4)
``dm_test_vfs_ext4``          ext4 mount with explicit type
``dm_test_vfs_fat``           FAT mount and auto-detect
``dm_test_vfs_auto``          Filesystem auto-detection
``dm_test_vfs_bootdev``       Boot device creation on mount
``dm_test_vfs_umount_all``    umount -a
============================  =========================================

Run all VFS tests::

    /tmp/b/sandbox/u-boot -T -c "ut dm vfs_"
