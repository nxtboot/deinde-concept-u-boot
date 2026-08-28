.. SPDX-License-Identifier: GPL-2.0-or-later

.. index::
   single: video (command)

video command
=============

Synopsis
--------

::

    video setcursor <col> <row>
    video puts <string>
    video write [-p] [<col>:<row> <string>]...
    video images

Description
-----------

The video command provides access to the video-console subsystem. Common
arguments are as follows:

col
    Column position in hex, with 0 being the left side. Note that this is the
    text-column position, so the number of pixels per position depends on the
    font size.

row
    Row position in hex, with 0 being the top edge. Note that this is the
    text-row position, so the number of pixels per position depends on the
    font size.

video setcursor
~~~~~~~~~~~~~~~

    video setcursor <col> <row>

Set the cursor position on the video console.

video puts
~~~~~~~~~~

    video puts <string>

Write a string to the video console at the current cursor position.

string
    Text string to display

video write
~~~~~~~~~~~

    video write [-p] [<col>:<row> <string>]...

Write one or more strings to the video console at specified positions. Each
position/string pair sets the cursor to the specified location and writes the
string. Multiple position/string pairs can be provided to write to multiple
locations in a single command.

-p
    Use pixel coordinates instead of character positions. When specified, the
    col and row values are interpreted as pixel offsets and converted to
    character positions based on the current font size.

string
    Text string to display at the specified position

video images
~~~~~~~~~~~~

    video images

List all images that are compiled into U-Boot. This shows the name and size
of each image that was built from .bmp files in the drivers/video/images
directory.

Examples
--------

Set cursor and print text::

    => video setcursor 10 5
    => video puts "Hello World"

Print at different positions::

    => video setcursor 0 0
    => video puts "Top left"
    => video setcursor 0 10
    => video puts "Line 16"

Write text at multiple positions::

    => video write 0:0 "Top left" 0:a "Line 10"
    => video write 0:a "First column in line10" 2a:0 "Text column 42"

Write text using pixel coordinates::

    => video write -p 0:0 "Top left corner" a0:80 "Pixel position"

List compiled-in images::

    => video images
    Name                       Size
    -------------------- ----------
    u_boot                     6932

    Total images: 1

Configuration
-------------

The video command is available if CONFIG_CMD_VIDEO=y.

Return value
------------

The return value $? is 0 (true) on success, 1 (false) on failure.

See also
--------

* :doc:`osd<osd>` for the text overlay drawn on top of the display
