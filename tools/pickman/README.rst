.. SPDX-License-Identifier: GPL-2.0+
..
.. Copyright 2025 Canonical Ltd.
.. Written by Simon Glass <simon.glass@canonical.com>

Pickman - Cherry-pick Manager
=============================

Pickman is a tool to help manage cherry-picking commits between branches.

Workflow
--------

The typical workflow for using pickman is:

1. **Setup**: Add a source branch to track with ``add-source``. This records the
   starting point (merge-base) for cherry-picking.

2. **Cherry-pick**: Run ``apply -p`` to cherry-pick the next set of commits (up
   to the next merge commit) and create a GitLab MR. A Claude agent handles the
   cherry-picks automatically, resolving simple conflicts.

3. **Review**: Once the MR is reviewed and merged, run ``commit-source`` to
   update the database with the last processed commit.

4. **Repeat**: Go back to step 2 until all commits are cherry-picked.

For fully automated workflows, use ``poll`` which runs ``step`` in a loop. The
``step`` command handles the complete cycle automatically:

- Detects merged MRs and updates the database (no manual ``commit-source``)
- Processes review comments on open MRs using Claude agent
- Creates new MRs when none are pending

This allows hands-off operation: just run ``poll`` and approve/merge MRs in
GitLab as they come in.

For ad-hoc cherry-picks without tracking, use the ``pick`` command. This
supports commit ranges (``hash1..hash2``) or merge commits, and doesn't require
a registered source branch. See `Ad-hoc Cherry-picking`_ for details.

Commit Selection
----------------

When pickman creates an MR, it groups commits into logical sets based on merge
commits in the source branch. Understanding this helps predict what will be
included in each MR.

**Algorithm**

1. **Start from the last processed commit**: Pickman reads from its database the
   hash of the last commit that was successfully cherry-picked from the source
   branch.

2. **Find the next merge on first-parent chain**: Walking forward from the last
   processed commit along the first-parent chain (``git log --first-parent``),
   pickman finds the next merge commit. The first-parent chain represents the
   mainline history of the branch.

3. **Include all commits up to that merge**: Using ``git log`` (without
   ``--first-parent``), pickman collects ALL commits between the last processed
   commit and the merge commit. This includes:

   - Commits on the mainline leading up to the merge
   - Commits brought in by the merge (from the merged branch)
   - The merge commit itself

**Example**

Consider this history on the source branch::

    * 5c8ef70 Merge tag 'xilinx-for-v2025.01-rc5-v2'
    |\
    | * 1b70b6c common: memtop: Fix the return type
    |/
    * c06705a Makefile: Match the full path to ccache
    * 0b7f4c7 imx: Fix usable memory ranges
    * ff1d5d8 Revert "configs: JH7110: enable EFI_LOADER"
    * d701c6a net: lwip: check if network device is available
    * b6691d0 net: lwip: do not return CMD_RET_USAGE
    * 9378307 binman: Regenerate tools/binman/entries.rst  <-- last processed

If the database shows ``9378307`` as the last processed commit, pickman will:

1. Walk first-parent from ``9378307`` and find merge ``5c8ef70``
2. Collect all commits in ``9378307..5c8ef70``:

   - ``b6691d0`` net: lwip: do not return CMD_RET_USAGE
   - ``d701c6a`` net: lwip: check if network device is available
   - ``ff1d5d8`` Revert "configs: JH7110..."
   - ``0b7f4c7`` imx: Fix usable memory ranges
   - ``c06705a`` Makefile: Match the full path to ccache
   - ``1b70b6c`` common: memtop: Fix the return type (from xilinx branch)
   - ``5c8ef70`` Merge tag 'xilinx-for-v2025.01-rc5-v2'

The resulting MR contains 7 commits. The branch name is derived from the first
commit's short hash: ``cherry-b6691d0``.

**Why merge-based grouping?**

Merge commits typically represent logical units of work (e.g., a pull request
or a subsystem update). By stopping at each merge, pickman:

- Keeps MRs focused and reviewable
- Preserves the original grouping from upstream
- Makes it easier to identify and skip problematic sets

**No merge found**

If there are no merge commits between the last processed commit and the branch
tip, pickman includes all remaining commits in a single set. This is noted in
the output as "no merge found".

Subtree Merge Handling
----------------------

The source branch may contain subtree merges that update vendored trees such as
``dts/upstream``, ``lib/mbedtls/external/mbedtls`` or ``lib/lwip/lwip``. These
appear as a pair of commits on the first-parent chain:

1. ``Squashed 'dts/upstream/' changes from <old>..<new>`` (the actual file
   changes)
2. ``Subtree merge tag '<tag>' of <repo> into dts/upstream`` (the merge commit
   joining histories)

These commits cannot be cherry-picked. Pickman detects them automatically by
matching the merge subject against the pattern
``Subtree merge tag '<tag>' of ... into <path>``. When a subtree merge is found,
pickman:

1. Checks out the target branch (e.g. ``ci/master``)
2. Runs ``./tools/update-subtree.sh pull <name> <tag>`` to apply the update
3. Pushes the target branch (if ``--push`` is active)
4. Records both the squash and merge commits as 'applied' in the database
5. Advances the source position past the merge and continues with the next batch

This is works without manual intervention. The currently supported subtrees are:

=================================  ===========
Path                               Name
=================================  ===========
``dts/upstream``                   ``dts``
``lib/mbedtls/external/mbedtls``   ``mbedtls``
``lib/lwip/lwip``                  ``lwip``
=================================  ===========

Skipping MRs
------------

During review, if a set of commits should be skipped (e.g., not applicable to
the target branch), a reviewer can comment:

- ``pickman skip``
- ``pickman: skip``
- ``@pickman skip``
- ``@pickman: skip``

Pickman will add ``[skipped]`` to the MR title. Skipped MRs:

- Are ignored when deciding whether to create new MRs
- Don't block the ``step`` or ``poll`` commands from proceeding
- Can be unskipped by commenting ``pickman unskip``

Already-Applied Detection
-------------------------

Sometimes commits have already been applied to the target branch through a
different path (e.g., directly merged or cherry-picked with different hashes).
Pickman detects this situation automatically in two ways.

**Pre-Cherry-Pick Detection**

Before starting cherry-picks, pickman checks for potentially already-applied
commits by searching for matching commit subjects in the target branch::

    git log --oneline ci/master --grep="<subject>" -1

Commits that match are marked as "maybe already applied" and passed to the
Claude agent with the hash of the potentially matching commit. The agent then:

1. Compares the actual patch content between the original and found commits
2. Uses ``git show`` and ``diff`` to analyze the changes
3. Skips commits that are similar with only minor differences (line numbers,
   context, conflict resolutions)
4. Proceeds with commits that differ significantly in actual changes

**Fallback Detection**

If pre-detection missed something and the first cherry-pick fails with
conflicts, the agent performs the same subject search and patch comparison
process. If all commits in the set are verified as already applied, the agent:

1. Aborts the cherry-pick
2. Writes a signal file (``.pickman-signal``) with status ``already_applied``
3. Reports the situation

**What pickman does**

When pickman detects the ``already_applied`` signal or when the agent reports
pre-detected applied commits:

1. Marks all commits as 'skipped' in the database
2. Updates the source position to advance past these commits
3. Creates an MR with ``[skipped]`` prefix to record the attempt
4. The MR description explains that commits were already applied

This ensures:

- There's a record of what was attempted
- The source position advances so the next ``poll`` iteration processes new
  commits
- No manual intervention is required to continue
- False positives are minimized by comparing actual patch content

Drift from Upstream
-------------------

The downstream tree should differ from upstream only where a downstream commit
deliberately makes it so. Everything else which accumulates - a hunk mangled
while resolving a conflict, a hunk dropped from a cherry-pick, a hand-edit
which never made it into a commit of its own - is drift, and pickman can find
it and put it back.

Provenance provides the ground truth, so there is no list to maintain by hand.
Every commit added downstream since the trees diverged is one of two things:

- a cherry-pick, carrying a ``(cherry picked from commit ...)`` line, which
  should leave the tree matching upstream
- a downstream-original commit, which is the whole reason the tree is allowed
  to differ

A delta which no downstream-original commit accounts for is therefore drift.
The comparison is made against the upstream commit which the source is sitting
at, not the tip of upstream, since commits which have not been cherry-picked
yet are not drift.

To see what has crept in::

    ./tools/pickman/pickman drift us/master

    Comparing with upstream 5c7ff1b54071
      3162 file(s) differ from upstream
      6629 hunk(s) wanted, explained by downstream commits
      0 hunk(s) accepted by .pickman-diverge
      665 hunk(s) of drift in 466 file(s), 9% of divergence

The percentage is the drift as a share of every hunk which differs from
upstream: how much of the divergence is spurious rather than intended.

By default pickman blames the files which a downstream commit has touched, so
that it can tell drift inside them from the genuine downstream changes.  This
is accurate but takes a few minutes on a large tree; pass ``-s`` / ``--shallow``
for a quick estimate which instead takes any touched file as wanted in full.

A hunk which only removes lines is never called drift in a deep run. Blame can
say who wrote a line but not who deleted one, so reverting such a hunk might
resurrect code which a downstream commit meant to remove. Nothing is missed:
no downstream commit has touched a file classified without blame, so a deletion
there cannot be deliberate and is still caught.

The changes a merge commit makes belong to the commits it merges, so merges are
not counted as downstream-original. A conflict resolved in a merge rather than
in a commit therefore shows up as drift, which is usually the right answer.
Where it is not, record it in the accept file.

Accepting a Delta
~~~~~~~~~~~~~~~~~

Some deltas are intentional but have no commit to point to: an adaptation made
while resolving a conflict, say. These go in ``.pickman-diverge``, which is a
list of exceptions rather than an inventory of the whole divergence, so it
stays short::

    ./tools/pickman/pickman drift-accept README -m 'Keep our fix for line 1'

    README                            *             Keep our fix for line 1

The file is committed to the downstream tree, so the record travels with it and
a reviewer can see it. A delta may be accepted a whole file at a time (``*``),
a glob at a time (``arch/arm/dts/*.dtsi``), a directory at a time
(``test/hooks/``) or one hunk at a time, by passing the hunk's fingerprint to
``-u``. A fingerprint covers the content of a hunk and not its position, so it
survives the churn upstream causes; if the delta itself is later reworked, the
fingerprint changes and the hunk comes back for review, which is intended.

Reverting Drift
~~~~~~~~~~~~~~~

To put drift back to what upstream has::

    ./tools/pickman/pickman drift-fix us/master -c 3 -p

Pickman groups the drifted files by area of the tree, reverts the drift hunks
in each area (leaving alone any hunk in the same file which is wanted), commits
the result on a branch of its own and, with ``-p``, opens an MR for it. Working
an area at a time keeps each MR small enough to review and stops a CI failure
in one area from holding up the rest; ``-c`` says how many areas to do at once,
and running it again picks up where it left off.

Pipeline Fix
------------

When a CI pipeline fails on a pickman MR, the ``step`` and ``poll`` commands
can automatically diagnose and fix the failure using a Claude agent. This is
useful when cherry-picks introduce build or test failures that need minor
adjustments.

**How it works**

During each step, after processing review comments, pickman checks active MRs
for failed pipelines. For each failed pipeline:

1. Pickman fetches the failed job logs from GitLab
2. A Claude agent analyses the logs, diagnoses the root cause, and makes
   targeted fixes
3. The fix is pushed to the MR branch, triggering a new pipeline
4. The attempt is recorded in the database to avoid reprocessing

**Retry behaviour**

Each MR gets up to ``--fix-retries`` attempts (default: 3). If the limit is
reached, pickman posts a comment on the MR indicating that manual intervention
is required. Set ``--fix-retries 0`` to disable automatic pipeline fixing.

Each attempt is tracked per pipeline ID, so a new pipeline triggered by a rebase
or comment fix is treated independently.

**Options**

- ``-F, --fix-retries``: Maximum pipeline-fix attempts per MR (default: 3, 0 to
  disable). Available on both ``step`` and ``poll`` commands.

CI Pipelines
------------

Pickman manages CI pipelines to avoid unnecessary duplicate runs. GitLab
automatically triggers an MR pipeline whenever the source branch is updated,
so pickman skips the push pipeline to avoid running two pipelines.

**How it works**

When pushing a branch (for new MRs or updates), pickman uses ``-o ci.skip``
to skip the push pipeline. GitLab then triggers an MR pipeline when it
detects the branch update on the merge request. This ensures exactly one
pipeline runs for each push.

**Summary**

===============================  ================  ==============================
Action                           Pipeline Skipped  Reason
===============================  ================  ==============================
Initial branch push for new MR   Yes               MR creation triggers pipeline
Push after rebase/review         Yes               MR update triggers pipeline
===============================  ================  ==============================

Usage
-----

To add a source branch to track::

    ./tools/pickman/pickman add-source us/next

This finds the merge-base commit between the master branch (ci/master) and the
source branch, and stores it in the database as the starting point for
cherry-picking.

To list all tracked source branches::

    ./tools/pickman/pickman list-sources

To compare branches and show commits that need to be cherry-picked::

    ./tools/pickman/pickman compare

This shows:

- The number of commits in the source branch (us/next) that are not in the
  master branch (ci/master)
- The last common commit between the two branches

To check current branch for problematic cherry-picks::

    ./tools/pickman/pickman check

This analyzes commits on the current branch and identifies cherry-picks with
large deltas compared to their original commits. By default, it:

- Shows only problematic commits (above 20% delta threshold)
- Ignores small commits (less than 10 lines changed)
- Skips merge commits (which have different delta characteristics)
- Uses color coding: red for ≥80% delta, yellow for ≥50% delta

Options:

- ``-t, --threshold``: Delta threshold as fraction (default: 0.2 = 20%)
- ``-m, --min-lines``: Minimum lines changed to check (default: 10)
- ``-v, --verbose``: Show detailed analysis for all commits
- ``--diff``: Show patch differences for problem commits
- ``--no-colour``: Disable color output in patch differences

Example output::

    Cherry-pick Delta% Original   Subject
    ----------- ------ ---------- -------
    aaea489b2a    100 9bab7d2a7c net: wget: let wget_with_dns work with dns disabled
    e557daec17     89 f0315babfb hash: Plumb crc8 into the hash functions
    
    2 problem commit(s) found

This helps identify cherry-picks that may have been applied incorrectly or
need manual review due to significant differences from the original commits.

To check GitLab permissions for the configured token::

    ./tools/pickman/pickman check-gitlab

This verifies that the GitLab token has the required permissions to push
branches and create merge requests. Use ``-r`` to specify a different remote.

To show the next set of commits to cherry-pick from a source branch::

    ./tools/pickman/pickman next-set us/next

This finds commits between the last cherry-picked commit and the next merge
commit in the source branch. It stops at the merge commit since that typically
represents a logical grouping of commits (e.g., a pull request).

To count the total remaining merges to process::

    ./tools/pickman/pickman count-merges us/next

This shows how many merge commits remain on the first-parent chain between the
last cherry-picked commit and the source branch tip.

To show the next N merges that will be applied::

    ./tools/pickman/pickman next-merges us/next

This shows the upcoming merge commits on the first-parent chain, useful for
seeing what's coming up. Use ``-c`` to specify the count (default 10).

To apply the next set of commits using a Claude agent::

    ./tools/pickman/pickman apply us/next

This uses the Claude Agent SDK to automate the cherry-pick process. The agent
will:

- Run git status to check the repository state
- Cherry-pick each commit in order
- Handle simple conflicts automatically
- Report status after completion

To push the branch and create a GitLab merge request::

    ./tools/pickman/pickman apply us/next -p

Options for the apply command:

- ``-b, --branch``: Branch name to create (default: cherry-<hash>)
- ``-p, --push``: Push branch and create GitLab MR
- ``-r, --remote``: Git remote for push (default: ci)
- ``-t, --target``: Target branch for MR (default: master)

On successful cherry-pick, an entry is appended to ``.pickman-history`` with:

- Date and source branch
- Branch name and list of commits
- The agent's conversation log

This file is committed automatically and included in the MR description when
using ``-p``.

Ad-hoc Cherry-picking
~~~~~~~~~~~~~~~~~~~~~

To cherry-pick commits without using a registered source branch::

    ./tools/pickman/pickman pick <commit-spec>

The ``pick`` command supports three input formats:

1. **Commit range**: Cherry-pick all commits in a range::

       ./tools/pickman/pickman pick abc123..def456

2. **Merge commit**: Cherry-pick all commits that were part of a merge::

       ./tools/pickman/pickman pick <merge-hash>

   This extracts all commits from the merged branch (excluding the merge
   commit itself) and cherry-picks them.

3. **Single commit**: Cherry-pick a single non-merge commit::

       ./tools/pickman/pickman pick <commit-hash>

Like ``apply``, this uses a Claude agent to handle the cherry-picks and resolve
simple conflicts. However, unlike ``apply``:

- No source branch registration is required
- Commits are not tracked in the database
- No automatic position updates after completion

This is useful for one-off cherry-picks or when you need to quickly grab
specific commits without setting up full tracking.

To push the branch and create a GitLab merge request::

    ./tools/pickman/pickman pick abc123..def456 -p

Options for the pick command:

- ``-b, --branch``: Branch name to create (default: pick-<hash>)
- ``-p, --push``: Push branch and create GitLab MR
- ``-r, --remote``: Git remote for push (default: ci)
- ``-t, --target``: Target branch for MR (default: master)

After successfully applying commits, update the database to record progress::

    ./tools/pickman/pickman commit-source us/next <commit-hash>

This updates the last cherry-picked commit for the source branch, so subsequent
``next-set`` and ``apply`` commands will start from the new position.

To pivot to a renamed or post-release upstream branch (for example, when
``us/next`` is exhausted and integration moves to ``us/master``)::

    ./tools/pickman/pickman switch-source us/next us/master

This seeds a new ``us/master`` source entry with the last commit cherry-picked
from ``us/next``, so subsequent ``next-set``/``apply``/``step`` commands resume
from the same point on the new branch. Pickman verifies the seed commit is
reachable from the new source so it does not re-apply already-picked commits.
The old source entry is kept in the database for reference; remove it manually
if no longer needed.

Options for the switch-source command:

- ``--at <commit>``: Override the start commit (default: inherit from the old
  source's last cherry-picked commit)
- ``-f, --force``: Overwrite an existing entry for the new source

To check open MRs for comments and address them::

    ./tools/pickman/pickman review

This lists open pickman MRs (those with ``[pickman]`` in the title), checks each
for unresolved comments, and uses a Claude agent to address them. The agent will:

- Make code changes based on the feedback
- Create a local branch with version suffix (e.g., ``cherry-abc123-v2``)
- Force push to the original remote branch to update the existing MR
- Use ``--keep-empty`` when rebasing to preserve empty merge commits

After processing, pickman:

- Marks comments as processed in the database (to avoid reprocessing)
- Updates the MR description with the agent's conversation log
- Appends the review handling to ``.pickman-history``

Options for the review command:

- ``-r, --remote``: Git remote (default: ci)

To automatically create an MR if none is pending::

    ./tools/pickman/pickman step us/next

This command performs the following:

1. Checks for merged pickman MRs and updates the database with the last
   cherry-picked commit from each merged MR
2. Checks for open pickman MRs (those with ``[pickman]`` in the title)
3. If open MRs exist, processes any review comments using Claude agent
4. If open MRs are below ``--max-mrs`` limit, runs ``apply`` with ``--push``
   to create a new one

This is useful for automated workflows. The ``--max-mrs`` option controls how
many MRs can be open simultaneously (default: 5), allowing parallel review of
multiple cherry-pick sets. The automatic database update on merge means you
don't need to manually run ``commit-source`` after each MR is merged, and
review comments are handled automatically.

Options for the step command:

- ``-F, --fix-retries``: Max pipeline-fix attempts per MR (default: 3, 0 to disable)
- ``-m, --max-mrs``: Maximum open MRs allowed (default: 5)
- ``-r, --remote``: Git remote for push (default: ci)
- ``-t, --target``: Target branch for MR (default: master)

To run step continuously in a polling loop::

    ./tools/pickman/pickman poll us/next

This runs the ``step`` command repeatedly with a configurable interval,
creating new MRs as previous ones are merged. Press Ctrl+C to stop.

Options for the poll command:

- ``-F, --fix-retries``: Max pipeline-fix attempts per MR (default: 3, 0 to disable)
- ``-i, --interval``: Interval between steps in seconds (default: 300)
- ``-m, --max-mrs``: Maximum open MRs allowed (default: 5)
- ``-r, --remote``: Git remote for push (default: ci)
- ``-t, --target``: Target branch for MR (default: master)

To push a branch using the GitLab API token for authentication::

    ./tools/pickman/pickman push-branch <branch-name>

This is useful when you want commits to appear as coming from the token owner
(e.g., a pickman bot account) rather than the user's configured git credentials.
The agent uses this command automatically when pushing review changes.

Options for the push-branch command:

- ``-r, --remote``: Git remote (default: ci)
- ``-f, --force``: Force push (overwrite remote branch)

Checking Status
~~~~~~~~~~~~~~~

To see, at a glance, how far behind upstream the downstream branch is and how
much drift it carries::

    ./tools/pickman/pickman status us/master

This reports two backlogs: the upstream series (first-parent merges) not yet
cherry-picked, and the drift which should be resynced to upstream, including
what fraction of the divergence from upstream that drift represents.  It reads
the refs as they are, so fetch first for an up-to-date picture.  The drift
count is the accurate, slower one by default (see below); pass ``-s`` for a
quick estimate, or ``-b`` to name a different downstream branch.

Checking Drift from Upstream
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To report deltas from upstream which no downstream commit accounts for::

    ./tools/pickman/pickman drift us/master

Options:

- ``-l`` list each file which has drift, worst first
- ``-d`` show the drift as a patch
- ``-s`` skip blaming the files which downstream commits have touched; faster
  but misses drift inside them
- ``-b`` downstream branch to examine (default: ci/master)

By default pickman blames those touched files, which finds drift inside them
but takes a few minutes on a large tree.  The exit code is 1 if there is any
drift, so this can be used as a check.

To record a delta as intentional, exempting it from the above::

    ./tools/pickman/pickman drift-accept .gitlab-ci.yml -m 'Downstream CI'
    ./tools/pickman/pickman drift-accept 'test/hooks/' -m 'Downstream hooks'
    ./tools/pickman/pickman drift-accept lib/efi.c -u a3f19c2b8d41 -m 'Ours'

This writes ``.pickman-diverge``, which should be committed. Without ``-u`` the
whole file is accepted; with it, only the hunk with that fingerprint.

To revert drift back to upstream::

    ./tools/pickman/pickman drift-fix us/master

Options:

- ``-c`` number of areas of the tree to fix at once (default: 1)
- ``-p`` push each branch and open an MR for it
- ``-s`` skip blaming touched files, matching a shallow ``drift`` run
- ``-r`` git remote for the push (default: ci)
- ``-t`` target branch for the MR (default: master)

Each area gets its own branch and MR, so run it again to work through the rest.

See `Drift from Upstream`_ for what counts as drift and why.

Requirements
------------

To use the ``apply`` command, install the Claude Agent SDK::

    pip install claude-agent-sdk

You will also need an Anthropic API key set in the ``ANTHROPIC_API_KEY``
environment variable.

To use the ``-p`` (push) option for GitLab integration, install python-gitlab::

    pip install python-gitlab

You will also need a GitLab API token. The token can be configured in a config
file or environment variable. Pickman checks in this order:

1. Config file ``~/.config/pickman.conf``::

       [gitlab]
       token = glpat-xxxxxxxxxxxxxxxxxxxx

2. ``GITLAB_TOKEN`` environment variable
3. ``GITLAB_API_TOKEN`` environment variable

See `GitLab Personal Access Tokens`_ for instructions on creating a token.
The token needs ``api`` and ``write_repository`` scopes. Using a dedicated bot
account for pickman is recommended - this ensures all commits pushed by pickman
appear as coming from the bot account rather than individual users.

.. _GitLab Personal Access Tokens:
   https://docs.gitlab.com/ee/user/profile/personal_access_tokens.html

Database
--------

Pickman uses a sqlite3 database (``.pickman.db``) to track state. The schema
version is stored in the ``schema_version`` table and migrations are applied
automatically when the database is opened.

Tables
~~~~~~

**source**
    Tracks source branches and their cherry-pick progress.

    - ``id``: Primary key
    - ``name``: Branch name (e.g., 'us/next')
    - ``last_commit``: Hash of the last commit cherry-picked from this branch

**pcommit**
    Tracks individual commits being cherry-picked.

    - ``id``: Primary key
    - ``chash``: Original commit hash
    - ``source_id``: Foreign key to source table
    - ``mergereq_id``: Foreign key to mergereq table (optional)
    - ``subject``: Commit subject line
    - ``author``: Commit author
    - ``status``: One of 'pending', 'applied', 'skipped', 'conflict'
    - ``cherry_hash``: Hash of the cherry-picked commit (if applied)

**mergereq**
    Tracks merge requests created for cherry-picked commits.

    - ``id``: Primary key
    - ``source_id``: Foreign key to source table
    - ``branch_name``: Git branch name for this MR
    - ``mr_id``: GitLab merge request ID
    - ``status``: One of 'open', 'merged', 'closed'
    - ``url``: URL to the merge request
    - ``created_at``: Timestamp when the MR was created

**comment**
    Tracks MR comments that have been processed by the review agent.

    - ``id``: Primary key
    - ``mr_iid``: GitLab merge request IID
    - ``comment_id``: GitLab comment/note ID
    - ``processed_at``: Timestamp when the comment was processed

    This table prevents the same comment from being addressed multiple times
    when running ``review`` or ``poll`` commands.

**pipeline_fix**
    Tracks pipeline fix attempts per MR to avoid reprocessing.

    - ``id``: Primary key
    - ``mr_iid``: GitLab merge request IID
    - ``pipeline_id``: GitLab pipeline ID
    - ``attempt``: Attempt number
    - ``status``: Result ('success', 'failure', 'skipped', 'no_jobs')
    - ``created_at``: Timestamp when the attempt was made

    The ``(mr_iid, pipeline_id)`` pair is unique, so each pipeline is only
    processed once.

Configuration
-------------

The branches to compare are configured as constants in control.py:

- ``BRANCH_MASTER``: The main branch to compare against (default: ci/master)
- ``BRANCH_SOURCE``: The source branch with commits to cherry-pick
  (default: us/next)

Testing
-------

To run the functional tests::

    ./tools/pickman/pickman test

Or using pytest::

    python3 -m pytest tools/pickman/ftest.py -v
