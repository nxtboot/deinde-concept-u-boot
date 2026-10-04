.. SPDX-License-Identifier: GPL-2.0+

Package U-Boot
==============

U-Boot uses Flat Image Tree (FIT) as a standard file format for packaging
images that it reads and boots. Documentation about FIT is available at
doc/usage/fit

U-Boot also uses binman for cases not covered by FIT. Examples include
initial execution (since FIT itself does not have an executable header) and
dealing with device boundaries, such as the read-only/read-write separation in
SPI flash.

Binman is maintained as a separate project, at
https://github.com/nxtboot/binman, and is installed as the binary-manager
package::

    pip install binary-manager

The U-Boot build runs the ``binman`` on the PATH, or another copy if set with
``make BINMAN=/path/to/binman``. Its documentation is at
https://binman.readthedocs.io/
