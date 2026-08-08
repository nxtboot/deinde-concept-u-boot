.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: cedit (command)

cedit command
=============

Synopsis
--------

::

    cedit load <interface> <dev[:part]> <filename>
    cedit run
    cedit write_fdt <dev[:part]> <filename>
    cedit read_fdt <dev[:part]> <filename>
    cedit write_env [-v]
    cedit read_env [-v]
    cedit write_cmos [-v] [dev]
    cedit cb_load
    cedit dump

Description
-----------

The *cedit* command is used to load a configuration-editor description and allow
the user to interact with it.

It makes use of the expo subsystem.

The description is in the form of a devicetree file, as documented at
:ref:`expo_format`.

See :doc:`../../develop/cedit` for information about the configuration editor.

cedit load
~~~~~~~~~~

Loads a configuration-editor description from a file. It creates a new cedit
structure ready for use. Initially no settings are read, so default values are
used for each object.

cedit run
~~~~~~~~~

Runs the default configuration-editor event loop. This is very simple, just
accepting character input and moving through the objects under user control.
The implementation is at `cedit_run()`.

cedit write_fdt
~~~~~~~~~~~~~~~

Writes the current user settings to a devicetree file. For each menu item the
selected ID and its text string are written.

cedit read_fdt
~~~~~~~~~~~~~~

Reads the user settings from a devicetree file and updates the cedit with those
settings.

cedit read_env
~~~~~~~~~~~~~~

Reads the settings from the environment variables. For each menu item `<name>`,
cedit looks for a variable called `c.<name>` with the ID of the selected menu
item.

The `-v` flag enables verbose mode, where each variable is printed after it is
read.

cedit write_env
~~~~~~~~~~~~~~~

Writes the settings to environment variables. For each menu item the selected
ID and its text string are written, similar to:

   env set c.<name> <selected_id>
   env set c.<name>-str <selected_id's text string>

The `-v` flag enables verbose mode, where each variable is printed before it is
set.

cedit write_cmos
~~~~~~~~~~~~~~~~

Writes the settings to locations in the CMOS RAM. The locations used are
specified by the schema. See `expo_format_`.

The `-v` flag enables verbose mode, which shows which CMOS locations were
updated.

Normally the first RTC device is used to hold the data. You can specify a
different device by name using the `dev` parameter.

.. _cedit_cb_load:

cedit cb_load
~~~~~~~~~~~~~

This is supported only on x86 devices booted from coreboot. It creates a new
configuration editor which can be used to edit CMOS settings.

cedit dump
~~~~~~~~~~

Dumps the current expo structure to the console. This shows the expo, its
scenes, objects, and their properties in a human-readable format. This is useful
for debugging and understanding the structure of the configuration editor.

Configuration
-------------

The cedit command is only available if CONFIG_CMD_CEDIT=y.

The 'cedit dump' subcommand is only available if CONFIG_CMD_CEDIT_DUMP=y.

Example
-------

::

    => cedit load hostfs - fred.dtb
    => cedit run
    => cedit write_fdt hostfs - settings.dtb

That results in::

    / {
        cedit-values {
            cpu-speed = <0x00000006>;
            cpu-speed-value = <0x00000003>;
            cpu-speed-str = "2 GHz";
            power-loss = <0x0000000a>;
            power-loss-value = <0x00000000>;
            power-loss-str = "Always Off";
        };
    }

    => cedit read_fdt hostfs - settings.dtb

This shows settings being stored in the environment::

    => cedit write_env -v
    c.cpu-speed=11
    c.cpu-speed-str=2.5 GHz
    c.cpu-speed-value=3
    c.power-loss=14
    c.power-loss-str=Always Off
    c.power-loss-value=0
    c.machine-name=my-machine
    c.cpu-speed=11
    c.power-loss=14
    c.machine-name=my-machine
    => print
    ...
    c.cpu-speed=6
    c.cpu-speed-str=2 GHz
    c.power-loss=10
    c.power-loss-str=Always Off
    c.machine-name=my-machine
    ...

    => cedit read_env -v
    c.cpu-speed=7
    c.power-loss=12

This shows writing to CMOS RAM. Notice that the bytes at 80 and 84 change::

    => rtc read 80 8
    00000080: 00 00 00 00 00 2f 2a 08                          ...../*.
    =>  cedit write_cmos -v
    Write 2 bytes from offset 80 to 84
    => rtc read 80 8
    00000080: 01 00 00 00 08 2f 2a 08                          ...../*.
    => cedit read_cmos -v
    Read 2 bytes from offset 80 to 84

Here is an example with the device specified::

    => cedit write_cmos rtc@43
    =>

This example shows editing coreboot CMOS-RAM settings. A script could be used
to automate this::

    => cbsysinfo
    Coreboot table at 500, size 5c4, records 1d (dec 29), decoded to 000000007dce3f40, forwarded to 000000007ff9a000

    CPU KHz     : 0
    Serial I/O port: 00000000
       base        : 00000000
       pointer     : 000000007ff9a370
       type        : 1
       base        : 000003f8
       baud        : 0d115200
       regwidth    : 1
       input_hz    : 0d1843200
       PCI addr    : 00000010
    Mem ranges  : 7
              id: type               ||   base        ||   size
               0: 10:table    0000000000000000 0000000000001000
               1: 01:ram      0000000000001000 000000000009f000
               2: 02:reserved 00000000000a0000 0000000000060000
               3: 01:ram      0000000000100000 000000007fe6d000
               4: 10:table    000000007ff6d000 0000000000093000
               5: 02:reserved 00000000fec00000 0000000000001000
               6: 02:reserved 00000000ff800000 0000000000800000
    option_table: 000000007ff9a018
     Bit  Len  Cfg  ID  Name
       0  180    r   0  reserved_memory
     180    1    e   4  boot_option            0:Fallback 1:Normal
     184    4    h   0  reboot_counter
     190    8    r   0  reserved_century
     1b8    8    r   0  reserved_ibm_ps2_century
     1c0    1    e   1  power_on_after_fail    0:Disable 1:Enable
     1c4    4    e   6  debug_level            5:Notice 6:Info 7:Debug 8:Spew
     1d0   80    r   0  vbnv
     3f0   10    h   0  check_sum
    CMOS start  : 1c0
       CMOS end    : 1cf
       CMOS csum loc: 3f0
    VBNV start  : ffffffff
    VBNV size   : ffffffff
    ...
    Unimpl.     : 10 37 40

Check that the CMOS RAM checksum is correct, then create a configuration editor
and load the settings from CMOS RAM::

    => cbcmos check
    => cedit cb
    => cedit read_cmos

Now run the cedit. In this case the user selected 'save' so `cedit run` returns
success::

    => if cedit run; then cedit write_cmos -v; fi
    Write 2 bytes from offset 30 to 38
    => echo $?
    0

Update the checksum in CMOS RAM::

    => cbcmos check
    Checksum 6100 error: calculated 7100
    => cbcmos update
    Checksum 7100 written
    => cbcmos check
    =>

This shows dumping the cedit::

    Expo: name 'name'
    display (null)
    cons (none)
    mouse (none)
    scene_id 0
    next_id 42
    req_width 0
    req_height 0
    text_mode 0
    popup 0
    show_highlight 0
    mouse_enabled 0
    mouse_ptr 0000000000000000
    mouse_size 0x0
    mouse_pos (0,0)
    damage (0,0)-(0,0)
    done 0
    save 0
    last_key_ms 271450936
    Theme:
        font_size 0
        white_on_black 0
        menu_inset 0
        menuitem_gap_y 0

    Scenes:
    Scene 6: name 'main'
        title_id 19 (title)
        highlight_id 0 ((none))
        Object 19 (title): type text
        flags
        bbox: (0,0)-(0,0)
        dims: 0x0
        Text: str_id 20 font_name '(default)' font_size 0
            str 'Test Configuration'
        Object 21 (prompt): type text
        flags
        bbox: (0,0)-(0,0)
        dims: 0x0
        Text: str_id 22 font_name '(default)' font_size 0
            str 'UP and DOWN to choose, ENTER to select'
        Object 8 (cpu-speed): type menu
        flags
        bbox: (0,0)-(0,0)
        dims: 0x0
        Menu: pointer_id 0 title_id 23 manual 0
            Item 10: name '00' label_id 25 desc_id 0
            Item 11: name '01' label_id 27 desc_id 0
            Item 12: name '02' label_id 29 desc_id 0
        Object 23 (title): type text
        flags
        bbox: (0,0)-(0,0)
        dims: 0x0
        Text: str_id 24 font_name '(default)' font_size 0
            str 'CPU speed'
        Object 25 (item-label): type text
        flags
        bbox: (0,0)-(0,0)
        dims: 0x0
        Text: str_id 26 font_name '(default)' font_size 0
            str '2 GHz'
        Object 27 (item-label): type text
        flags
        bbox: (0,0)-(0,0)
        dims: 0x0
        Text: str_id 28 font_name '(default)' font_size 0
            str '2.5 GHz'
        Object 29 (item-label): type text
        flags
        bbox: (0,0)-(0,0)
        dims: 0x0
        Text: str_id 30 font_name '(default)' font_size 0
            str '3 GHz'
        Object 13 (power-loss): type menu
        flags
        bbox: (0,0)-(0,0)
        dims: 0x0
        Menu: pointer_id 0 title_id 31 manual 0
            Item 14: name '00' label_id 33 desc_id 0
            Item 15: name '01' label_id 35 desc_id 0
            Item 16: name '02' label_id 37 desc_id 0
        Object 31 (title): type text
        flags
        bbox: (0,0)-(0,0)
        dims: 0x0
        Text: str_id 32 font_name '(default)' font_size 0
            str 'AC Power'
        Object 33 (item-label): type text
        flags
        bbox: (0,0)-(0,0)
        dims: 0x0
        Text: str_id 34 font_name '(default)' font_size 0
            str 'Always Off'
        Object 35 (item-label): type text
        flags
        bbox: (0,0)-(0,0)
        dims: 0x0
        Text: str_id 36 font_name '(default)' font_size 0
            str 'Always On'
        Object 37 (item-label): type text
        flags
        bbox: (0,0)-(0,0)
        dims: 0x0
        Text: str_id 38 font_name '(default)' font_size 0
            str 'Memory'
        Object 17 (machine-name): type textline
        flags
        bbox: (0,0)-(0,0)
        dims: 0x0
        Textline: label_id 39 edit_id 18
            line_chars 14 pos 14
        Object 39 (title): type text
        flags
        bbox: (0,0)-(0,0)
        dims: 0x0
        Text: str_id 40 font_name '(default)' font_size 0
            str 'Machine name'
        Object 18 (edit): type text
        flags
        bbox: (0,0)-(0,0)
        dims: 0x0
        Text: str_id 41 font_name '(default)' font_size 0
            str ''
