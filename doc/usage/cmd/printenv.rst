.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: printenv (command)

printenv command
================

Synopsis
--------

::

    printenv [-a] [name ...]
    printenv -e [-guid guid][-n][-s][-v] [name]

Description
-----------

The printenv command is used to print environment or UEFI variables.

\-a
    Print environment variables starting with a period ('.').

\-e
    Print UEFI variables. Without -e environment variables are printed.

\-guid *guid*
    Specify vendor GUID *guid*. If none is specified, all UEFI variables with
    the specified name are printed irrespective of their vendor GUID.

\-n
    don't show hexadecimal dump of value

\-s
    sort variables by name before displaying

\-v
    show verbose output including GUID, attributes, data size and hexadecimal
    dump of value (if not -n)

name
    Variable name. If no name is provided, all variables are printed.
    Multiple environment variable names may be specified.

Examples
--------

The following examples demonstrates the effect of the *-a* flag when displaying
environment variables:

::

    => env set .foo bar
    => printenv
    arch=sandbox
    baudrate=115200
    board=sandbox
    ...
    stdout=serial,vidconsole

    Environment size: 644/8188 bytes
    => printenv -a
    .foo=bar
    arch=sandbox
    baudrate=115200
    board=sandbox
    ...
    stdout=serial,vidconsole

    Environment size: 653/8188 bytes
    =>

The next example shows the different output modes when displaying UEFI
variables and how to specify a vendor GUID. By default, only the variable
name is shown. The *-v* flag shows full verbose output, while *-n* shows
details but omits the hexadecimal dump. The *-s* flag sorts variables by
name:

::

    => printenv -e PlatformLangCodes
    PlatformLangCodes
    => printenv -e -v -n PlatformLangCodes
    PlatformLangCodes:
        8be4df61-93ca-11d2-aa0d-00e098032b8c (EFI_GLOBAL_VARIABLE_GUID)
        BS|RT|RO, DataSize = 0x6
    => printenv -e -v -guid 8be4df61-93ca-11d2-aa0d-00e098032b8c PlatformLangCodes
    PlatformLangCodes:
        8be4df61-93ca-11d2-aa0d-00e098032b8c (EFI_GLOBAL_VARIABLE_GUID)
        BS|RT|RO, DataSize = 0x6
        00000000: 65 6e 2d 55 53 00                                en-US.
    => print -e -s
    525400123456
    525400123456
    Attempt 1
    Attempt 2
    Attempt 3
    Attempt 4
    Attempt 5
    Attempt 6
    Attempt 7
    Attempt 8
    Boot0000
    Boot0001
    Boot0002
    Boot0003
    BootCurrent
    BootOptionSupport
    BootOrder
    ClientId
    ConIn
    ConInDev
    ConOut
    ConOutDev
    ErrOut
    ErrOutDev
    HDDP
    InitialAttemptOrder
    Key0000
    Key0001
    Lang
    LangCodes
    MTC
    MemoryTypeInformation
    OsIndicationsSupported
    PlatformLang
    PlatformLangCodes
    PlatformRecovery0000
    SbatLevel
    Timeout
    VarErrorFlag
    cat
    cd..
    cd\
    copy
    debuglasterror
    del
    dir
    lasterror
    md
    mem
    mount
    move
    nonesting
    path
    profiles
    ren
    uefishellsupport
    uefishellversion
    uefiversion

Configuration
-------------

UEFI variables are only supported if CONFIG_CMD_NVEDIT_EFI=y. The value of UEFI
variables can only be displayed if CONFIG_HEXDUMP=y.

Return value
------------

The return value $? is 1 (false) if a specified variable is not found.
Otherwise $? is set to 0 (true).
