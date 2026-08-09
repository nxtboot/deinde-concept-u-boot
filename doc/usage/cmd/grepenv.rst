.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: grepenv (command)

grepenv command
===============

Synopsis
--------

::

    grepenv [-e] [-n | -v | -b] string ...

Description
-----------

The grepenv command lists the environment variables whose name or value
matches one of the given strings. Each match is shown as a name=value pair,
in the same form as :doc:`printenv`, with the matches sorted by name.

It is an alias for *env grep*, described in :doc:`env`.

string
    text to look for. More than one may be given, in which case a variable is
    listed if it matches any of them. Matching is by substring unless -e is
    used.

\-e
    treat each string as a regular expression rather than a substring. This
    is available only when CONFIG_REGEX is enabled.

\-n
    match against variable names only

\-v
    match against variable values only

\-b
    match against both names and values. This is the default.

Example
-------

Show every variable whose name or value contains 'boot'::

    => setenv bootfile boot.img
    => grepenv boot
    bootcmd=bootflow scan -lb
    bootdelay=2
    bootfile=boot.img
    bootm_size=0x10000000

Search names only::

    => grepenv -n baud
    baudrate=115200

Search values only, so that the variables holding the board name are found
without listing the ones which merely have it in their name::

    => grepenv -v sandbox
    arch=sandbox
    board=sandbox
    board_name=sandbox
    cpu=sandbox

With -e the string is a regular expression, so this finds the names which
start with 'ip'::

    => grepenv -e -n ^ip
    ipaddr=192.0.2.1
    ipaddr2=192.0.2.3
    ipaddr3=192.0.2.4
    ipaddr5=192.0.2.6
    ipaddr6=192.0.2.7
    ipaddr7=192.0.2.8

Configuration
-------------

The command is only available if CONFIG_CMD_GREPENV=y. The -e flag needs
CONFIG_REGEX=y as well.

Return value
------------

The return value $? is 0 (true) if at least one variable is listed. It is 1
(false) if nothing matches, and also if the command line is invalid, in which
case the usage is shown.
