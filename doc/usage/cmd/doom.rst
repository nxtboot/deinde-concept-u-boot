.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: doom (command)

doom command
============

Synopsis
--------

::

    doom [wad-file] [frame-dir]     # sandbox
    doom <addr> <size>              # any other board

Description
-----------

The doom command runs the 1993 first-person shooter DOOM, using the
doomgeneric port of the id Software engine. It draws into the framebuffer of
the first video device and reads the keyboard, so any board with a display can
run it. There is no sound.

The game needs a WAD file, which holds its levels and artwork and is not part
of U-Boot. Where it comes from depends on the board.

Sandbox reads it from the host filesystem, named by the first argument, or
'doom1.wad' in the current directory if none is given. Note that this is the
directory U-Boot was started from, not anything inside U-Boot.

Any other board has no filesystem at the point the command runs, so the WAD is
placed in memory beforehand and named by address and size instead. Anything
the game writes afterwards - its configuration file and save games - is kept
in memory and lasts until the command exits.

Leaving the game through its menu returns to the U-Boot prompt.

Keys
----

Sandbox reads the keyboard through SDL and so knows when a key is released as
well as pressed. The controls are the ones DOOM shipped with:

=============== ===============================================
Key             Action
=============== ===============================================
Arrows          move and turn
Ctrl            fire
Space           use: open doors, press switches, call lifts
Alt + arrows    strafe
Shift           run
1 to 9          select a weapon
Tab             map
Esc             menu
=============== ===============================================

Other boards read the console, which reports a key going down but never coming
up. A press is therefore released again after a short time, and holding a key
works because its auto-repeat arrives before the release does. Movement is
less smooth as a result, and since no modifier key can be reported, fire and
strafe are given keys of their own:

=============== ===============================================
Key             Action
=============== ===============================================
Arrows or WASD  move and turn
F               fire
Z and X         strafe left and right
Space           use
Esc             menu
=============== ===============================================

The pause key is not mapped on either backend.

Examples
--------

On sandbox, with a WAD installed by the freedoom package::

    u-boot -D -l -c "doom /usr/share/games/doom/freedoom1.wad"

Frames can be recorded instead of displayed, which is useful on a machine with
no display. Each thirtieth frame is written to the named directory as a PPM
image, up to thirty of them::

    u-boot -D -c "doom doom1.wad /tmp/frames"

On qemu-x86_64, with the WAD placed in memory by QEMU::

    qemu-system-x86_64 -bios u-boot.rom -m 1024 \
        -device loader,file=doom1.wad,addr=0x10000000
    => doom 10000000 4006b4

The second argument is the length of the WAD in bytes, which the command has
no way of working out for itself. Get it from the file before starting QEMU::

    $ printf '%x\n' $(stat -c%s doom1.wad)
    4006b4

Give it the wrong length and the engine reads past the end of the WAD, or
stops short of it and fails to find a lump it needs.

On a board which can load files itself, load the WAD and pass where it went::

    => load mmc 0:1 10000000 freedoom1.wad
    => doom 10000000 ${filesize}

Configuration
-------------

The command is only available if CONFIG_CMD_DOOM=y, which needs CONFIG_VIDEO
and CONFIG_HAVE_SETJMP. It is enabled for sandbox, and
qemu-x86_64_doom_defconfig builds it for QEMU.

The engine asks malloc() for a 6MB zone heap and needs a framebuffer of
960x600, so CONFIG_SYS_MALLOC_LEN has to be large enough for both. It also
adds around 700KB of code, which on x86 does not fit in the 2MB ROM that
qemu-x86_64 uses by default.

See also
--------

* :doc:`bmp<bmp>` for showing a picture rather than a game
* :doc:`video<video>` for what the display is doing
* :doc:`2048<2048>` for a smaller game, which needs only an ANSI terminal
