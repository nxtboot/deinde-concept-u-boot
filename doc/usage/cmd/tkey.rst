.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: tkey (command)

tkey command
============

Synopsis
--------

::

    tkey connect [device-name]
    tkey fwmode
    tkey getkey <uss> [verify-hash]
    tkey info
    tkey loadapp [uss]
    tkey signer
    tkey wrapkey <password>

Description
-----------

The *tkey* command provides an interface to interact with Tillitis TKey
security tokens. The TKey is a USB security device that can be used for
cryptographic operations, particularly for deriving encryption keys in a
secure and reproducible manner.

The TKey operates in two modes:

Firmware mode
    The device starts in this mode after being plugged in. In firmware mode,
    the device provides access to its Unique Device Identifier (UDI) and allows
    loading applications.

App mode
    After an application is loaded, the device enters app mode. The UDI is no
    longer accessible, but the loaded app can perform cryptographic operations.

The primary use case is full disk encryption (FDE) key derivation, where the
TKey combines a User-Supplied Secret (USS, typically a password) with its
internal UDI to generate deterministic encryption keys.


tkey connect
~~~~~~~~~~~~

Test connectivity to a TKey device and optionally select a specific device for
subsequent commands.

When called without arguments, this command connects to the first available TKey
device in the system. When a device name is provided, it connects to that
specific device.

Once connected, the selected device is remembered and will be used by all
subsequent tkey commands (info, getkey, loadapp, etc.) until a different device
is selected with another connect command.

device-name
    Optional name of a specific TKey device to connect to


tkey fwmode
~~~~~~~~~~~

Check whether the TKey device is currently in firmware mode or app mode.

Firmware mode
    The device has just been plugged in or reset. The UDI is accessible and
    apps can be loaded.

App mode
    An application has been loaded and is running. The UDI is not accessible.


tkey getkey
~~~~~~~~~~~

Derive a disk encryption key by loading the embedded signer app with a
User-Supplied Secret (USS). This is the main command for full-disk-encryption
workflows.

The command performs these steps:

#. Loads the embedded signer app with the provided USS
#. Retrieves the public key from the signer app
#. Derives the disk encryption key (BLAKE2b hash of the public key)
#. Computes a verification hash (BLAKE2b hash of the disk key)

The USS is typically a user password or passphrase. The same USS always produces
the same disk encryption key, making this suitable for unlocking encrypted
disks.

If a verification hash is provided as the second argument, the command verifies
that the USS is correct by comparing the computed hash with the expected hash.
This is useful for validating user passwords before attempting to decrypt a
disk.

uss
    User-Supplied Secret (password/passphrase) for key derivation

verify-hash
    Optional 64-character hex string to verify the USS is correct

The command outputs:

Public Key
    32-byte Ed25519 public key derived from USS

Disk Key
    32-byte encryption key (BLAKE2b-256 of public key)

Verification Hash
    32-byte hash of disk key (save this for later verification)


tkey info
~~~~~~~~~

Display information about the TKey device, including its name, version, and
Unique Device Identifier (UDI).

The UDI is only available in firmware mode. If the device is in app mode, the
command will report that the UDI is not available and suggest replugging the
device.


tkey loadapp
~~~~~~~~~~~~

Load the embedded signer application to the TKey device. This can only be done
when the device is in firmware mode.

An optional USS (User-Supplied Secret) can be provided, which will be used by
the signer app for key derivation. If no USS is provided, the app loads without
a secret.

After loading, the device transitions to app mode and the UDI becomes
inaccessible.

uss
    Optional User-Supplied Secret for the signer app


tkey signer
~~~~~~~~~~~

Display information about the embedded signer application binary that is
compiled into U-Boot.


tkey wrapkey
~~~~~~~~~~~~

Derive a wrapping key from a password and the device's UDI. This uses the
BLAKE2b-256 hash function to combine the UDI with the password.

The wrapping key can be used to encrypt/decrypt other secrets. Unlike getkey,
this command does not load an app - it only requires the UDI, so it must be
run in firmware mode.

The same password always produces the same wrapping key for a given device,
but different TKey devices (with different UDIs) will produce different
wrapping keys even with the same password.

password
    Password to combine with UDI for key derivation


Example
-------

Connect to the first available device::

    => tkey connect
    Connected to TKey device

Connect to a specific device by name::

    => tkey connect tkey@0
    Connected to TKey device

Check device mode::

    => tkey fwmode
    firmware mode

    => tkey loadapp
    Loading signer app (a9c bytes)...

    => tkey fwmode
    app mode

Get device information (firmware mode)::

    => tkey info
    Name0: tk1  Name1: mkdf Version: 4
    UDI: a0a1a2a3a4a5a6a7

Get device information (app mode)::

    => tkey info
    Name0: tk1  Name1: sign Version: 4
    UDI not available - replug device

Derive disk encryption key without verification::

    => tkey getkey mypassword
    Public Key: 1a2b3c4d...
    Disk Key: 9f8e7d6c...
    Verification Hash: 5a4b3c2d...

Derive disk encryption key with verification (correct password)::

    => tkey getkey mypassword 5a4b3c2d1e0f...
    Public Key: 1a2b3c4d...
    Disk Key: 9f8e7d6c...

    password correct

Derive disk encryption key with verification (wrong password)::

    => tkey getkey wrongpassword 5a4b3c2d1e0f...
    Public Key: aaaa1111...
    Disk Key: bbbb2222...

    wrong password
    Expected: 5a4b3c2d1e0f...
    Got: cccc3333...

Load app without USS::

    => tkey loadapp
    Loading signer app (a9c bytes)...

Load app with USS::

    => tkey loadapp mypassword
    Loading signer app (a9c bytes) with USS...

Show signer binary information::

    => tkey signer
    signer binary: a9c bytes at 0x1234-0x5678

Derive wrapping key from password::

    => tkey wrapkey mypassword
    Wrapping Key: 95229cd376898f3fb022a627349dc985bc4675da580d2696bdd6f71f488e306c


Configuration
-------------

The tkey command is available if CONFIG_CMD_TKEY is enabled. The command
requires a TKey driver to be configured (USB or serial).


See also
--------

* :doc:`../tkey-fde` - TKey full disk encryption guide
* :doc:`luks` - LUKS command reference
* :doc:`bootctl` - Boot control, which can unlock a volume with a TKey
* `Tillitis TKey documentation <https://tillitis.se/>`_
