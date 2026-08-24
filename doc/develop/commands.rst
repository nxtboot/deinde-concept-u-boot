.. SPDX-License-Identifier: GPL-2.0+

Implementing shell commands
===========================

Command definition
------------------

Commands are added to U-Boot by creating a new command structure.
This is done by first including command.h, then using one of the U_BOOT_CMD
macros to fill in a struct cmd_tbl structure.

**Use U_BOOT_CMD_GETOPT() for a new command.** It gives the command function
the getopt signature, described below, which parses options for it:

.. code-block:: c

    U_BOOT_CMD_GETOPT(name, maxargs, repeatable, command, "usage", "help")
    U_BOOT_CMD_GETOPT_COMPLETE(name, maxargs, repeatable, command, "usage",
                               "help", comp)

Many existing commands use the older macros, which give the command function
the classic signature instead. Do not convert one without a test to show that
its behaviour has not changed:

.. code-block:: c

    U_BOOT_CMD(name, maxargs, repeatable, command, "usage", "help")
    U_BOOT_CMD_COMPLETE(name, maxargs, repeatable, command, "usage", "help", comp)

The arguments are the same either way:

name
    The name of the command. This is **not** a string.

maxargs
    The maximum number of arguments this function takes including
    the command itself.

repeatable
    Either 0 or 1 to indicate if autorepeat is allowed.

command
    Pointer to the command function. This is the function that is
    called when the command is issued.

usage
    Short description. This is a string.

help
    Long description. This is a string. The long description is
    only available if CONFIG_SYS_LONGHELP is defined.

comp
    Pointer to the completion function. May be NULL.
    This function is called if the user hits the TAB key while
    entering the command arguments to complete the entry. Command
    completion is only available if CONFIG_AUTO_COMPLETE is defined.

Sub-command definition
----------------------

A command with sub-commands is best declared with U_BOOT_CMD_WITH_SUBCMDS(),
which writes the table and the code to search it for you:

.. code-block:: c

    U_BOOT_CMD_WITH_SUBCMDS(foo, "do foo things", foo_help_text,
        U_BOOT_SUBCMD_MKENT(bar, 2, 1, do_foo_bar),
        U_BOOT_SUBCMD_MKENT(baz, 3, 0, do_foo_baz));

The dispatcher it generates finds the sub-command, refuses to repeat one which
is not repeatable, and calls it. Each sub-command has its own maxargs and its
own repeatable flag, so 'foo bar' can repeat when Enter is pressed while
'foo baz' does not.

Where a command searches its own table instead, the entries are made with:

.. code-block:: c

    U_BOOT_CMD_MKENT(name, maxargs, repeatable, command, "usage", "help")
    U_BOOT_CMD_MKENT_COMPLETE(name, maxargs, repeatable, command, "usage",
                              "help", comp)
    U_BOOT_CMD_MKENT_GETOPT(name, maxargs, repeatable, command, "usage", "help")

and the sub-command is reached with cmd_invoke(), which passes it its own table
entry and copes with either signature:

.. code-block:: c

    static struct cmd_tbl cmd_sub[] = {
        U_BOOT_CMD_MKENT(foo, CONFIG_SYS_MAXARGS, 1, do_foo, "", ""),
        U_BOOT_CMD_MKENT(bar, CONFIG_SYS_MAXARGS, 1, do_bar, "", ""),
    };

    static int do_cmd(struct getopt_state *gs)
    {
        struct cmd_tbl *cp;
        int argc = gs->argc;
        char *const *argv = gs->argv;

        if (argc < 2)
                return CMD_RET_USAGE;

        /* drop sub-command argument */
        argc--;
        argv++;

        cp = find_cmd_tbl(argv[0], cmd_sub, ARRAY_SIZE(cmd_sub));

        if (cp)
            return cmd_invoke(cp, gs->cmd_flag, argc, argv);

        return CMD_RET_USAGE;
    }

Do not call ``cp->cmd()`` directly. That passes the outer command's table entry
to the sub-command, so a sub-command reading cmdtp->name sees the wrong name,
and it cannot call a sub-command which uses the getopt signature.

Command function
----------------

There are two shapes of command function. A new command should use the getopt
one:

.. code-block:: c

    int (*cmd)(struct getopt_state *gs);

gs
    Parser state for this invocation. It holds the arguments and the command
    flags, and getopt() reads options out of it. The fields a command uses
    are:

    * gs->argc - number of arguments including the command
    * gs->argv - the arguments
    * gs->cmd_flag - the flags described below

Options are read with getopt(), which returns each option letter in turn and
-1 when there are none left:

.. code-block:: c

    static int do_foo(struct getopt_state *gs)
    {
        bool verbose = false;
        const char *arg;
        int opt;

        while ((opt = getopt(gs, "+v")) > 0) {
            switch (opt) {
            case 'v':
                verbose = true;
                break;
            default:
                return CMD_RET_USAGE;
            }
        }

        while ((arg = getopt_pop(gs)))
            printf("%s\n", arg);

        return 0;
    }

Start the optstring with ``+``. That makes getopt() stop at the first argument
which is not an option, which is how U-Boot commands have always behaved, and
avoids needing CONFIG_GETOPT_PERMUTE, which would make every command carry a
writable copy of its arguments.

A command with no options at all still gains something from the signature: an
empty optstring refuses any option rather than treating it as data:

.. code-block:: c

    if (getopt(gs, "+") > 0)
        return CMD_RET_USAGE;

The remaining arguments are taken one at a time with getopt_pop(), or read from
``gs->argv[gs->index]`` onwards.

The older signature, used by commands which have not been converted, is:

.. code-block:: c

    int (*cmd)(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[]);

cmdtp
    Table entry describing the command (see above).

flag
    The command flags, as gs->cmd_flag above.

argc
    Number of arguments including the command.

argv
    Arguments.

The command flags are a bitmap which may contain the following bits:

* CMD_FLAG_REPEAT - The last command is repeated.
* CMD_FLAG_BOOTD  - The command is called by the bootd command.
* CMD_FLAG_ENV    - The command is called by the run command.

Allowable return value are:

CMD_RET_SUCCESS
    The command was successfully executed.

CMD_RET_FAILURE
    The command failed.

CMD_RET_USAGE
    The command was called with invalid parameters. This value
    leads to the display of the usage string.

Completion function
-------------------

The completion function pointer has to be of type

.. code-block:: c

    int (*complete)(int argc, char *const argv[], char last_char,
                    int maxv, char *cmdv[]);

argc
    Number of arguments including the command.

argv
    Arguments.

last_char
    The last character in the command line buffer.

maxv
    Maximum number of possible completions that may be returned by
    the function.

cmdv
    Used to return possible values for the last argument. The last
    possible completion must be followed by NULL.

The function returns the number of possible completions (without the terminating
NULL value).

Behind the scene
----------------

The structure created is named with a special prefix and placed by
the linker in a special section using the linker lists mechanism
(see include/linker_lists.h)

This makes it possible for the final link to extract all commands
compiled into any object code and construct a static array so the
command array can be iterated over using the linker lists macros.

The linker lists feature ensures that the linker does not discard
these symbols when linking full U-Boot even though they are not
referenced in the source code as such.

If a new board is defined do not forget to define the command section
by writing in u-boot.lds ($(srctree)/board/boardname/u-boot.lds) these
3 lines:

.. code-block:: c

    __u_boot_list : {
        KEEP(*(SORT(__u_boot_list*)));
    }

Writing tests
-------------

All new commands should have tests. Tests for existing commands are very
welcome.

It is fairly easy to write a test for a command. Enable it in sandbox, and
then add code that runs the command and checks the output.

Here is an example:

.. code-block:: c

    /* Test 'acpi items' command */
    static int dm_test_acpi_cmd_items(struct unit_test_state *uts)
    {
        struct acpi_ctx ctx;
        void *buf;

        buf = malloc(BUF_SIZE);
        ut_assertnonnull(buf);

        ctx.current = buf;
        ut_assertok(acpi_fill_ssdt(&ctx));
        run_command("acpi items", 0);
        ut_assert_nextline("dev 'acpi-test', type 1, size 2");
        ut_assert_nextline("dev 'acpi-test2', type 1, size 2");
        ut_assert_console_end();

        ctx.current = buf;
        ut_assertok(acpi_inject_dsdt(&ctx));
        run_command("acpi items", 0);
        ut_assert_nextline("dev 'acpi-test', type 2, size 2");
        ut_assert_nextline("dev 'acpi-test2', type 2, size 2");
        ut_assert_console_end();

        run_command("acpi items -d", 0);
        ut_assert_nextline("dev 'acpi-test', type 2, size 2");
        ut_assert_nextlines_are_dump(2);
        ut_assert_nextline("%s", "");
        ut_assert_nextline("dev 'acpi-test2', type 2, size 2");
        ut_assert_nextlines_are_dump(2);
        ut_assert_nextline("%s", "");
        ut_assert_console_end();

        return 0;
    }
    DM_TEST(dm_test_acpi_cmd_items, UTF_SCAN_PDATA | UTF_SCAN_FDT | UTF_CONSOLE);

Note that it is not necessary to call console_record_reset() unless you are
trying to drop some unchecked output. Consider using ut_check_skip_to_line()
instead.
