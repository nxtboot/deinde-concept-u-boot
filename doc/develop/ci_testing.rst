.. SPDX-License-Identifier: GPL-2.0+

Continuous Integration testing
==============================

All changes require passing our continuous integration tests prior to being
merged in to mainline.  To help facilitate merges being accepted quickly,
custodians are encouraged but not required to run a pipeline prior to sending a
pull request.  Individual developers submitting significant or widespread
changes are encouraged to run a pipeline themselves prior to posting.

In order to make this process as easy as possible, the ability to run a CI
pipeline is provided in both Azure and GitLab.  Both of these pipelines perform
their Linux build jobs on the same Docker container image and to cover the same
platforms.  In addition, Azure is also used to confirm that our host tools can
be built with mingw to run on Windows.

Each of the pipelines is written in such as way as to be a "world build" style
test and as such we try and build all possible platforms.  In addition, for all
platforms that support being run in QEMU we run them in QEMU and use our pytest
suite.  See :doc:`pytest/usage` for more information about those tests.

Azure Pipelines
---------------

This pipeline is defined in the top-level ``.azure-pipelines.yml`` file.
Currently there are two ways to run a Microsoft Azure Pipeline test for U-Boot.

The first way is to create an account with Microsoft at
https://azure.microsoft.com/en-us/services/devops/ and then use the
``.azure-pipelines.yml`` file in the U-Boot repository as the pipeline
description.

The second way is to use GitHub.  This requires a GitHub account
and to fork the repository at https://github.com/u-boot/u-boot and to then
submit a pull request as this will trigger an Azure pipeline run.  Clicking on
your pull request on the list at https://github.com/u-boot/u-boot/pulls and
then the "Checks" tab will show the results.

GitLab CI Pipelines
-------------------

`Gitlab <https://gitlab.com>`_ is used for building and testing U-Boot. There
are two separate instances:

  - `Denx <https://source.denx.de>`_ - main tree, including custodian trees
  - `Deinde Concept <https://concept.deinde.dev/u-boot/u-boot>`__ - the Deinde Concept Tree

This pipeline is defined in the top-level ``.gitlab-ci.yml`` file.

To push to Gitlab without triggering a pipeline use:

.. code-block:: bash

    git push -o ci.skip

Denx
~~~~

Currently with the main tree,
we support running GitLab CI pipelines only for custodians, due to the
resources the project has available.  For Custodians, it is a matter of
enabling the pipeline feature in your project repository following the standard
GitLab documentation.  For non-custodians, the pipeline itself is part of the
tree and should be able to be used on any GitLab instance, with whatever
runners you are able to provide.  While it is intended to be able to run this
pipeline on the free public instances provided at https://gitlab.com/ a problem
with our squashfs tests currently prevents this.

Deinde Concept
~~~~~~~~~~~~~~

Contributors can request a login at `Deinde Concept <concept.deinde.dev>`__
(after sending an email to Simon Glass <sjg@chromium.org> as shown there).
Contributors are then able to push branches and trigger a CI run. The
``.gitlab-ci.yml`` is slightly different from the main tree and CI runs
are generally twice as fast to complete.

Docker container
----------------

As previously stated, both of the above pipelines build using the same Docker
container image.  This is maintained in the U-Boot source tree at
``tools/docker/Dockerfile`` and new images are made as needed to support new
tests or features.  This file needs to be updated whenever adding new external
tool requirements to tests.

Customizing CI
--------------

As noted above, the CI pipelines perform a world build.  While this is good for
overall project testing, it can be less useful for testing specific cases or
developing features.  In that case, it can be useful as part of your own
testing cycle to edit these pipelines in separate local commits to pair them
down to just the jobs you're interested in.  These changes must be removed
prior to submission.

Available variables are:

TEST_SUITES
    Set to 0 to disable the test_suites stage::

        git push ci -o ci.variable=TEST_SUITES=0

TEST_PY
    Set to 0 to disable the test.py stage, or set to the job name to run just
    that job, or set to the board name to run all tests for that board:

    For example::

        git push ci -o ci.variable=TEST_PY=0
        git push ci -o ci.variable=TEST_PY=sandbox
        git push ci -o ci.variable=TEST_PY='sandbox with clang test.py'

WORLD_BUILD
    Set to 0 to disable the world_build stage::

        git push ci -o ci.variable=WORLD_BUILD=0

TEST_SPEC
    Set to the test spec to use when running a test.py tests. This overrides
    the spec provided by the job. For example::

        git push ci -o ci.variable=TEST_SPEC=bootstd

Using the lab
-------------

There is an 'sjg' lab connected to gitlab. By default, this is set to run
manually, so you must click a button in the gitlab interface to kick off a run.

You can control this behaviour by passing variables with your `git push`
command.

To skip CI altogether (assuming your remote is called `ci` and the branch is
`feature`)::

    git push ci -o ci.skip <branch>

To request that the lab runs::

    git push ci -o ci.variable=SJG_LAB=1 <branch>

To request that *only* the lab runs, you can set TEST_PY=0 and TEST_SUITES=0
and WORD_BUILD=0 as above. But as a shortcut you can use the LAB_ONLY
variables::

    git push ci -o ci.variable=SJG_LAB=1 ci.variable=LAB_ONLY=1 <branch>

To request running on just a particular board in the lab, e.g. `snow`::

    git push ci -o ci.variable=SJG_LAB=snow <branch>

The `LAB_ONLY` and `SJG_ONLY` options can be specified together.
