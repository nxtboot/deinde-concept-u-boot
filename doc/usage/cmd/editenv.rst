.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: editenv (command)

editenv command
===============

Synopsis
--------

::

    editenv [-e] name

Description
-----------

The editenv command changes the value of a single environment variable,
prompting for the new value with 'edit: '. The current value of the variable
is presented for editing, so the usual command-line editing keys can be used
to adjust it instead of typing it out again. An unset variable starts out
empty.

Pressing Enter stores what is shown. If nothing is left on the line the
variable is deleted, just as 'setenv name' with no value does. Ctrl-C
abandons the edit and leaves the variable as it was.

It is an alias for *env edit*, described in :doc:`env`.

name
    name of the variable to edit

\-e
    edit the variable with the expo graphical editor rather than on the
    command line. This needs a video console and is available only when
    CONFIG_CMD_EDITENV_EXPO is enabled.

Example
-------

Give a new value to a variable which is not set. The text after 'edit: ' is
typed by the user::

    => setenv fred
    => editenv fred
    edit: hello
    => printenv fred
    fred=hello

Edit it again. This time the old value appears after the prompt with the
cursor at the end of it, so typing adds to what is already there::

    => editenv fred
    edit: hello world
    => printenv fred
    fred=hello world

Configuration
-------------

The command is only available if CONFIG_CMD_EDITENV=y.

Return value
------------

The return value $? is 0 (true) if the variable is updated or deleted. It is
1 (false) if no name is given, if the environment is not ready yet, or if the
edit is abandoned with Ctrl-C.
