.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: showvar (command)

showvar command
===============

Synopsis
--------

::

    showvar [<name>...]

Description
-----------

The showvar command prints the local variables of the hush shell.

Local variables are those created by a plain assignment at the shell prompt or
in a script, such as 'fred=42'. They are distinct from the environment
variables shown by :doc:`printenv<printenv>`, which are created with 'setenv'
and may be stored to persistent media. A local variable is visible only to the
running shell and is lost when U-Boot restarts.

With no arguments the values of all local variables are printed, one per line,
in the order in which the shell holds them. HUSH_VERSION is always present,
since the shell sets it at start-up.

With one or more arguments only the named variables are printed. Naming a
variable which does not exist produces an error message on the console but does
not stop the remaining names from being processed.

name
    name of a local variable to print

The command is provided by the old hush parser, so it is available only when
CONFIG_HUSH_OLD_PARSER is enabled.

Example
-------

Print all local variables::

    => fred=42
    => wilma=hello
    => showvar
    HUSH_VERSION=0.01
    fred=42
    wilma=hello
    =>

Print a single variable::

    => showvar fred
    fred=42
    =>

An environment variable is not a local variable, so it is not found::

    => setenv barney stone
    => showvar barney
    ## Error: "barney" not defined
    =>

Return value
------------

The return value $? is 0 (true) if every requested variable exists. If any name
is not defined, $? is set to the number of names which could not be found::

    => showvar fred missing
    fred=42
    ## Error: "missing" not defined
    => echo $?
    1
    =>
