.. SPDX-License-Identifier: GPL-2.0+

.. _u-boot_doc:

The Deinde Concept U-Boot Documentation
=======================================

.. note::

   This documents the **Deinde Concept Tree**, an independent, experimental
   downstream tree for U-Boot maintained by an independent maintainer. It is
   not affiliated with, sponsored by, or endorsed by the Software Freedom
   Conservancy (SFC) or the official Das U-Boot project.

   The tree is a proving ground for large-scale features and refactoring, run
   in the open with the aim of feeding proven work back to mainline U-Boot.
   Pages here may therefore describe behaviour which does not exist in
   mainline, or which differs from it.

   * Mainline U-Boot: https://u-boot-project.org
   * About this tree: https://deinde.dev/u-boot/

This is the top level of the U-Boot's documentation tree.  U-Boot
documentation, like the U-Boot itself, is very much a work in progress;
that is especially true as we work to integrate our many scattered
documents into a coherent whole.  Please note that improvements to the
documentation are welcome; join the U-Boot list at http://lists.u-boot-project.org
if you want to help out.

.. toctree::
   :maxdepth: 2

Contributing Guidelines
-----------------------

.. note::

   To contribute **to this tree**, open a merge request against
   `concept.deinde.dev/u-boot/u-boot
   <https://concept.deinde.dev/u-boot/u-boot>`_. Patches are not accepted by
   email here; the tree's `mailing list
   <https://lists.deinde.dev/mailman3/lists/concept.u-boot.org/>`_ is for
   discussion.

   The guidelines below describe contributing to **mainline U-Boot**, which
   is done by sending patches to the U-Boot mailing list. Since the aim of
   this tree is to feed proven work back upstream, anything intended for
   mainline should follow them as well.

General guidelines for contributing to the U-Boot project.

.. toctree::
   :maxdepth: 2

   CONTRIBUTE

User-oriented documentation
---------------------------

The following manuals are written for *users* of the U-Boot - those who are
trying to get it to work optimally on a given system.

.. toctree::
   :maxdepth: 2

   build/index
   learn/index
   usage/index

Developer-oriented documentation
--------------------------------

The following manuals are written for *developers* of the U-Boot - those who
want to contribute to U-Boot.

.. toctree::
   :maxdepth: 2

   develop/index


U-Boot API documentation
------------------------

These books get into the details of how specific U-Boot subsystems work
from the point of view of a U-Boot developer.  Much of the information here
is taken directly from the U-Boot source, with supplemental material added
as needed (or at least as we managed to add it - probably *not* all that is
needed).

.. toctree::
   :maxdepth: 2

   api/index

Architecture-specific doc
-------------------------

These books provide programming details about architecture-specific
implementation.

.. toctree::
   :maxdepth: 2

   arch/index

Board-specific doc
------------------

These books provide details about board-specific information. They are
organized in a vendor subdirectory.

.. toctree::
   :maxdepth: 2

   board/index

Android-specific doc
--------------------

These books provide information about booting the Android OS from U-Boot,
manipulating Android images from U-Boot shell and discusses other
Android-specific features available in U-Boot.

.. toctree::
   :maxdepth: 2

   android/index

Chromium OS-specific doc
------------------------

.. toctree::
   :maxdepth: 2

   chromium/index

Indices and tables
==================

.. toctree::
   :maxdepth: 1

   genindex
