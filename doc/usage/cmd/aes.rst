.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: aes (command)

aes command
===========

Synopsis
--------

::

    aes[.128,.192,.256] enc <key_addr> <iv_addr> <src_addr> <dst_addr> <len>
    aes[.128,.192,.256] dec <key_addr> <iv_addr> <src_addr> <dst_addr> <len>
    aes get_slots
    aes[.128,.192,.256] set_key <key_addr> <slot>
    aes[.128,.192,.256] select_slot <slot>
    aes[.128,.192,.256] ecb enc <src_addr> <dst_addr> <len>
    aes[.128,.192,.256] ecb dec <src_addr> <dst_addr> <len>
    aes[.128,.192,.256] cbc enc <iv_addr> <src_addr> <dst_addr> <len>
    aes[.128,.192,.256] cbc dec <iv_addr> <src_addr> <dst_addr> <len>

Description
-----------

The aes command encrypts and decrypts a block of memory with the Advanced
Encryption Standard. The key length comes from the suffix on the command name:
*aes* and *aes.128* use a 128-bit key, *aes.192* a 192-bit one and *aes.256* a
256-bit one.

There are two families of sub-command. The *enc* and *dec* forms take the key
from memory and use Cipher Block Chaining; they are always available. The
others hold the key in one of the AES device's key slots, so the key itself
need not stay in memory, and they need an AES driver (CONFIG_DM_AES).

Using a key slot takes two steps: *set_key* writes a key into a slot, then
*select_slot* makes that slot the one the *ecb* and *cbc* sub-commands use. The
selected slot also fixes the key length for those sub-commands, so a suffix on
*ecb* or *cbc* has no effect. *get_slots* reports how many slots the device has.

key_addr
    address of the key, which is 16, 24 or 32 bytes long according to the
    command suffix

iv_addr
    address of the 16-byte initialisation vector

src_addr
    address of the data to encrypt or decrypt

dst_addr
    address to write the result to, which may be the same as src_addr

len
    number of bytes to process

slot
    key slot to write or select, counting from 0

All of these are read as hexadecimal. AES works a block at a time, so len
should be a multiple of 16 bytes; where it is not, the command rounds up to a
whole number of blocks and so reads and writes more than len bytes.

The command does nothing to pad the data, to record the key length or to check
that a decryption produced anything sensible, so both ends of an exchange must
agree on all of that beforehand.

Example
-------

This encrypts 32 bytes with a key held in memory and decrypts them again,
using :doc:`cmp<cmp>` to show that the result matches the original::

    => mw.b 1000 2b 10
    => mw.b 1010 00 10
    => mw.b 2000 a5 20
    => aes enc 1000 1010 2000 3000 20
    => md.b 3000 20
    00003000: 1a e7 de 48 51 97 98 ce 71 dc 99 8f 5c 06 d2 68  ...HQ...q...\..h
    00003010: 52 40 79 f9 97 41 83 1b be 0e ab fd 69 45 a1 96  R@y..A......iE..
    => aes dec 1000 1010 3000 4000 20
    => cmp.b 2000 4000 20
    Total of 32 byte(s) were the same

The same key, loaded into a slot, drives the *ecb* sub-command. Since ECB
treats each block on its own, the two identical plaintext blocks encrypt to
identical ciphertext blocks, which is why CBC is the better choice for
anything longer than one block::

    => aes get_slots
    Available slots: 2
    => aes set_key 1000 0
    => aes select_slot 0
    => aes ecb enc 2000 5000 20
    => md.b 5000 20
    00005000: 1a e7 de 48 51 97 98 ce 71 dc 99 8f 5c 06 d2 68  ...HQ...q...\..h
    00005010: 1a e7 de 48 51 97 98 ce 71 dc 99 8f 5c 06 d2 68  ...HQ...q...\..h

Configuration
-------------

The command is only available if CONFIG_CMD_AES=y. The get_slots, set_key,
select_slot, ecb and cbc sub-commands also need CONFIG_DM_AES=y and a driver
for the AES device, such as CONFIG_AES_SOFTWARE=y.

Return value
------------

The return value $? is 0 (true) if the operation succeeds. It is 1 (false) on a
malformed command line, when no AES device is present, when the slot number is
beyond what the device offers, or when *ecb* or *cbc* is asked to run before a
key slot has been selected.

See also
--------

* :doc:`md<md>` for showing the key, plaintext or ciphertext in memory
* :doc:`mm<mm>` for entering a key or an initialisation vector into memory
* :doc:`cmp<cmp>` for checking that a decrypted block matches the original
* *hash* for producing a digest of a region of memory
