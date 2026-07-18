.. SPDX-License-Identifier: GPL-2.0+
.. sectionauthor:: Paolo Scaffardi, AIRVENT SAM s.p.a - RIMINI(ITALY), arsenio@tin.it
..  (C) Copyright 2000
.. sectionauthor:: Simon Glass <sjg@chromium.org>

=======================
U-Boot console handling
=======================

Introduction
------------

At system-startup U-Boot initializes a serial console. When U-Boot
relocates itself to RAM, all console drivers are initialized (they
will register all detected console devices to the system for further
use).

If not defined in the environment, the first input device is assigned
to the 'stdin' file, the first output one to 'stdout' and 'stderr'.

You can use the command `coninfo` to see all registered console
devices and their flags. You can assign a standard file (stdin,
stdout or stderr) to any device you see in that list simply by
assigning its name to the corresponding environment variable. For
example::

    # Use the serial input
    env set stdin serial

    # Use the video console
    env set stdout vidconsole

Do a simple `saveenv` to save the console settings in the environment
and get them working on the next startup, too.

How to output text to the console
---------------------------------

You can use the following functions to access the console:

stdout
    - putc() - write a char to stdout
    - puts() - write a string to stdout
    - printf() - format and write a string to stdout

stdin
    - tstc() - test for the presence of a char in stdin
    - getchar() - get a char from stdin

stderr
    - eputc() - write a char to stderr
    - eputs() - write a string to stderr
    - eprintf() - format and write a string to stderr

file ('stdin', 'stdout' or 'stderr')
    - fputc() - write a char to a file
    - fputs() - write a string to a file
    - fprintf() - format and write a string to a file
    - ftstc() - test for the presence of a char in file
    - fgetc() - get a char from a file

Remember that FILE-related functions CANNOT be used before U-Boot relocation,
which is done in `board_init_r()`.

Pager
-----

U-Boot has a simple pager feature, enabled with `CONFIG_CONSOLE_PAGER`. It is
only available if `CONFIG_CONSOLE_MUX` is also enabled.

When activated, the pager pauses at the end of each 'page' (screenful) of
output, shows a prompt ": Press SPACE to continue" and lets the user read the
output. To continue to the next page, press the SPACE key. To quit paging
without seeing further output from the current command, press 'q'.

The pager can be bypassed by pressing 'Q' at the prompt. This disables the pager
until the 'pager' environment variable is given a new value.

Page Size Configuration
~~~~~~~~~~~~~~~~~~~~~~~

The number of lines per page is determined in the following order of priority:

1. **Environment variable**: The `pager` environment variable (hex value)
   takes highest priority. Set to 0 to disable paging.

2. **Video console detection**: If no environment variable is set and a video
   console is active, the pager uses the number of rows from the video console.

3. **Serial TTY detection**: For serial consoles, the pager checks if the
   output is connected to a terminal (TTY). If not connected to a TTY, paging
   is disabled. This check works by sending a few special characters to the
   terminal and (hopefully) receiving a reply. If you are logging the output of
   U-Boot, you may see these characters in the log. Disable
   `CONFIG_SERIAL_TERM_PRESENT` is this is unwanted.

4. **Configuration default**: If none of the above apply, falls back to
   `CONFIG_CONSOLE_PAGER_LINES`.

Examples::

    # Set page size to 30 lines (hex value 1e)
    env set pager 1e

    # Set page size to 24 lines (hex value 18)  
    env set pager 18

    # Disable paging
    env set pager 0

For developer documentation, please see :doc:`/develop/console`.
