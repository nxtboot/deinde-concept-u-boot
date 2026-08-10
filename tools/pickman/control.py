# SPDX-License-Identifier: GPL-2.0+
#
# Copyright 2025 Canonical Ltd.
# Written by Simon Glass <simon.glass@canonical.com>
#
# pylint: disable=too-many-lines
"""Control module for pickman - handles the main logic."""

from collections import namedtuple
from datetime import date
import fnmatch
import os
import re
import sys
import tempfile
import time
import unittest

import requests  # pylint: disable=import-error

# Allow 'from pickman import xxx' to work via symlink
our_path = os.path.dirname(os.path.realpath(__file__))
sys.path.insert(0, os.path.join(our_path, '..'))

# pylint: disable=wrong-import-position,import-error
from pickman import agent
from pickman import database
from pickman import drift
from pickman import ftest
from pickman import gitlab_api
from u_boot_pylib import command
from u_boot_pylib import gitutil
from u_boot_pylib import terminal
from u_boot_pylib import tools
from u_boot_pylib import tout

# Default database filename
DB_FNAME = '.pickman.db'

# Branch names to compare
BRANCH_MASTER = 'ci/master'
BRANCH_SOURCE = 'us/main'

# Git stat output parsing patterns
RE_GIT_STAT_SUMMARY = re.compile(
    r'(\d+)\s+files?\s+changed'
    r'(?:,\s*(\d+)\s+insertions?\([+]\))?'
    r'(?:,\s*(\d+)\s+deletions?\([-]\))?'
)
RE_GIT_STAT_FILE = re.compile(r'^([^|]+)\s*\|')

# Extract hash from line like "(cherry picked from commit abc123def)"
RE_CHERRY_PICK = re.compile(r'cherry picked from commit ([a-f0-9]+)')

# Detect subtree merge commits on the first-parent chain
RE_SUBTREE_MERGE = re.compile(
    r"Subtree merge tag '([^']+)' of .* into (.*)")

# Header of each line of 'git blame --porcelain' output:
#   <40-char hash> <line in original file> <line in final file> [<num lines>]
RE_BLAME_LINE = re.compile(r'^([0-9a-f]{40}) \d+ (\d+)')

# Map from subtree path to update-subtree.sh name
SUBTREE_NAMES = {
    'dts/upstream': 'dts',
    'lib/mbedtls/external/mbedtls': 'mbedtls',
    'lib/lwip/lwip': 'lwip',
}

# Return codes for _subtree_run_update()
SUBTREE_OK = 0
SUBTREE_FAIL = 1
SUBTREE_CONFLICT = 2

# Named tuple for commit info
Commit = namedtuple('Commit', ['hash', 'chash', 'subject', 'date'])

# Named tuple for git stat output
# files: Number of files changed
# inserted: Number of lines inserted
# deleted: Number of lines deleted
# file_set: Set of modified file paths
GitStat = namedtuple('GitStat', ['files', 'inserted', 'deleted', 'file_set'])

# Named tuple for check results
# chash: Cherry-pick commit hash (full)
# orig_hash: Original commit hash that was cherry-picked
# subject: Commit subject line
# delta_ratio: Ratio of differences between original and cherry-pick
#   (0.0=identical, 1.0=completely different)
# orig_stats: Stats from original commit (files, insertions, deletions,
#   file_set)
# cherry_stats: Stats from cherry-pick commit
# reason: Reason for skipping analysis (None if analyzed)
CheckResult = namedtuple('CheckResult', [
    'chash', 'orig_hash', 'subject', 'delta_ratio',
    'orig_stats', 'cherry_stats', 'reason'
])

# Named tuple for commit with author
# hash: Full SHA-1 commit hash (40 characters)
# chash: Abbreviated commit hash (typically 7-8 characters)
# subject: First line of commit message (commit subject)
# author: Commit author name and email in format "Name <email>"
CommitInfo = namedtuple('CommitInfo',
                        ['hash', 'chash', 'subject', 'author'])

# Named tuple for simplified commit data passed to agent
# hash: Full SHA-1 commit hash (40 characters)
# chash: Abbreviated commit hash (typically 7-8 characters)
# subject: First line of commit message (commit subject)
# applied_as: Short hash if potentially already applied, None otherwise
AgentCommit = namedtuple('AgentCommit',
                         ['hash', 'chash', 'subject', 'applied_as'])

# Named tuple for get_next_commits() result
#
# commits: list of CommitInfo to cherry-pick
# merge_found: True if these commits came from a merge on the source branch
# advance_to: hash to advance the source position to, or None to stay put
# subtree_update: (name, tag) tuple if a subtree update is needed, else None
NextCommitsInfo = namedtuple('NextCommitsInfo',
                             ['commits', 'merge_found', 'advance_to',
                              'subtree_update'],
                             defaults=[None])

# Named tuple for prepare_apply() result
#
# commits: list of AgentCommit to cherry-pick
# branch_name: name of the branch to create for the MR
# original_branch: branch name before any conflict suffix
# merge_found: True if these commits came from a merge on the source branch
# advance_to: hash to advance the source position to, or None to stay put
ApplyInfo = namedtuple('ApplyInfo',
                       ['commits', 'branch_name', 'original_branch',
                        'merge_found', 'advance_to'])

# Named tuple for drift_collect() result
#
# base: upstream commit the downstream tree is compared against, being the
#     last commit cherry-picked from the source
# fdiffs: list of drift.FileDiff, one per file which differs from upstream
# verdicts: dict mapping path to the list of drift.Verdict for its hunks
# binary: list of paths which differ from upstream in binary content, and
#     which no downstream commit accounts for
# orphans: set of commits picked from a series which upstream never took, and
#     so treated as downstream-original rather than as cherry-picks
# touched: set of paths which a downstream-original commit has touched, so
#     that drift in them might have a justification
# skipped: number of files taken as wanted in full without being looked
#     inside, which a shallow run does for every file it would have blamed
# deleted: paths which a downstream commit has removed, so that blame cannot
#     say who did it and their hunks are taken as wanted
# subtree: paths inside a vendored subtree, which update-subtree.sh manages
#     rather than pickman, so they are no part of the drift figure
DriftInfo = namedtuple('DriftInfo',
                       ['base', 'fdiffs', 'verdicts', 'binary', 'orphans',
                        'touched', 'skipped', 'deleted', 'subtree'])


def parse_log_output(log_output, has_parents=False):
    """Parse git log output to extract CommitInfo tuples

    Args:
        log_output (str): Output from git log with format '%H|%h|%an|%s'
            or '%H|%h|%an|%s|%P' if has_parents is True
        has_parents (bool): If True, expects parents field at end and
            excludes it from subject parsing

    Returns:
        list: List of CommitInfo tuples
    """
    commits = []
    for line in log_output.split('\n'):
        if not line:
            continue
        parts = line.split('|')
        commit_hash = parts[0]
        chash = parts[1]
        author = parts[2]
        if has_parents:
            subject = '|'.join(parts[3:-1])
        else:
            subject = '|'.join(parts[3:])
        commits.append(CommitInfo(commit_hash, chash, subject, author))
    return commits


def run_git(args):
    """Run a git command and return output."""
    return command.output('git', *args).strip()


def compare_branches(master, source):
    """Compare two branches and return commit difference info.

    Args:
        master (str): Main branch to compare against
        source (str): Source branch to check for unique commits

    Returns:
        tuple: (count, Commit) where count is number of commits and Commit
            is the last common commit
    """
    # Find commits in source that are not in master
    count = int(run_git(['rev-list', '--count', f'{master}..{source}']))

    # Find the merge base (last common commit)
    base = run_git(['merge-base', master, source])

    # Get details about the merge-base commit
    info = run_git(['log', '-1', '--format=%H%n%h%n%s%n%ci', base])
    full_hash, chash, subject, commit_date = info.split('\n')

    return count, Commit(full_hash, chash, subject, commit_date)


def do_add_source(args, dbs):
    """Add a source branch to the database

    Finds the merge-base commit between master and source and stores it.

    Args:
        args (Namespace): Parsed arguments with 'source' attribute
        dbs (Database): Database instance

    Returns:
        int: 0 on success
    """
    source = args.source

    # Find the merge base commit
    base_hash = run_git(['merge-base', BRANCH_MASTER, source])

    # Get commit details for display
    info = run_git(['log', '-1', '--format=%h%n%s', base_hash])
    chash, subject = info.split('\n')

    # Store in database
    dbs.source_set(source, base_hash)
    dbs.commit()

    tout.info(f"Added source '{source}' with base commit:")
    tout.info(f'  Hash:    {chash}')
    tout.info(f'  Subject: {subject}')

    return 0


def do_list_sources(args, dbs):  # pylint: disable=unused-argument
    """List all tracked source branches

    Args:
        args (Namespace): Parsed arguments
        dbs (Database): Database instance

    Returns:
        int: 0 on success
    """
    sources = dbs.source_get_all()

    if not sources:
        tout.info('No source branches tracked')
    else:
        tout.info('Tracked source branches:')
        for name, last_commit in sources:
            tout.info(f'  {name}: {last_commit[:12]}')

    return 0


def do_compare(args, dbs):  # pylint: disable=unused-argument
    """Compare branches and print results.

    Args:
        args (Namespace): Parsed arguments
        dbs (Database): Database instance
    """
    count, base = compare_branches(BRANCH_MASTER, BRANCH_SOURCE)

    tout.info(f'Commits in {BRANCH_SOURCE} not in {BRANCH_MASTER}: {count}')
    tout.info('')
    tout.info('Last common commit:')
    tout.info(f'  Hash:    {base.chash}')
    tout.info(f'  Subject: {base.subject}')
    tout.info(f'  Date:    {base.date}')

    return 0


def parse_git_stat_output(stat_output):
    """Parse git show --stat output to extract file change statistics

    Args:
        stat_output (str): Output from 'git show --stat <hash>'

    Returns:
        GitStat: Named tuple with files, insertions, deletions, file_set
    """
    lines = stat_output.strip().split('\n')
    files_changed = 0
    insertions = 0
    deletions = 0
    changed_files = set()

    # Parse summary line: "5 files changed, 42 insertions(+), 13 deletions(-)"
    for line in lines:
        match = RE_GIT_STAT_SUMMARY.search(line)
        if match:
            files_changed = int(match.group(1))
            insertions = int(match.group(2)) if match.group(2) else 0
            deletions = int(match.group(3)) if match.group(3) else 0
            break

    # Parse individual file lines: "path/to/file.ext | 42 ++++----"
    for line in lines:
        match = RE_GIT_STAT_FILE.match(line)
        if match:
            filename = match.group(1).strip()
            if filename:
                changed_files.add(filename)

    return GitStat(files_changed, insertions, deletions, changed_files)


def calc_ratio(orig, cherry):
    """Get the ratio of differences between original and cherry-picked commits

    Args:
        orig (GitStat): Stats for original commit
        cherry (GitStat): Stats for cherry-pick commit

    Returns:
        float: Delta ratio (0.0 = identical, 1.0 = completely
            different)
    """
    # If both commits have no changes, they're identical
    if not (orig.inserted + orig.deleted) and not (cherry.inserted +
                                                    cherry.deleted):
        return 0.0

    # Calculate file set difference
    if orig.file_set or cherry.file_set:
        union = orig.file_set | cherry.file_set
        intersection = orig.file_set & cherry.file_set
        similarity = (len(intersection) / len(union) if union else 1.0)
    else:
        similarity = 1.0

    # Calculate line change difference
    orig_lines = orig.inserted + orig.deleted
    cherry_lines = cherry.inserted + cherry.deleted

    if not orig_lines and not cherry_lines:
        line_similarity = 1.0
    elif not orig_lines or not cherry_lines:
        line_similarity = 0.0
    else:
        line_ratio = (min(orig_lines, cherry_lines) /
                      max(orig_lines, cherry_lines))
        line_similarity = line_ratio

    # Overall similarity is the minimum of file and line similarity
    overall_similarity = min(similarity, line_similarity)

    # Delta ratio is 1 - similarity
    return 1.0 - overall_similarity


def get_orig_commit(cherry_commit_hash):
    """Find the original commit hash from a cherry-pick commit

    Args:
        cherry_commit_hash (str): Hash of the cherry-picked commit

    Returns:
        str: Original commit hash, or None if not found
    """
    try:
        # Get the commit message
        commit_msg = run_git(['log', '-1', '--format=%B', cherry_commit_hash])

        # Look for "(cherry picked from commit <hash>)" line
        for line in commit_msg.split('\n'):
            if 'cherry picked from commit' in line:
                match = RE_CHERRY_PICK.search(line)
                if match:
                    return match.group(1)

        return None
    except Exception:  # pylint: disable=broad-except
        return None


def check_commits(commits, min_lines):
    """Yield CheckResult entries for commits with delta analysis

    Args:
        commits (list): List of (commit_hash, chash, subject) tuples
        min_lines (int): Minimum lines changed to analyze

    Yields:
        CheckResult: Analysis result for each commit
    """
    for chash, _, subject in commits:
        # Skip merge commits
        is_merge = False
        try:
            parents = run_git(['log', '-1', '--format=%P', chash]).split()
            if len(parents) > 1:
                is_merge = True
        except Exception:  # pylint: disable=broad-except
            pass

        # Also check subject for merge indicators
        if not is_merge and (subject.startswith('Merge ') or
                             'Merge branch' in subject or
                             'Merge tag' in subject):
            is_merge = True

        if is_merge:
            yield CheckResult(
                chash, None, subject, 0.0,
                None, None, 'merge_commit'
            )
            continue

        # Find original commit
        orig_hash = get_orig_commit(chash)
        if not orig_hash:
            yield CheckResult(
                chash, None, subject, 0.0,
                None, None, 'not_cherry_pick'
            )
            continue

        # Get stats for both commits
        orig_stat = run_git(['show', '--stat', orig_hash])
        cherry_stat = run_git(['show', '--stat', chash])

        # Parse statistics
        orig_stats = parse_git_stat_output(orig_stat)
        cherry_stats = parse_git_stat_output(cherry_stat)

        # Skip small commits
        orig_total_lines = orig_stats.inserted + orig_stats.deleted
        cherry_total_lines = cherry_stats.inserted + cherry_stats.deleted
        max_lines = max(orig_total_lines, cherry_total_lines)

        if max_lines < min_lines:
            yield CheckResult(
                chash, orig_hash, subject, 0.0,
                orig_stats, cherry_stats, f'small_commit_{max_lines}_lines'
            )
            continue

        # Calculate delta ratio
        delta_ratio = calc_ratio(orig_stats, cherry_stats)

        yield CheckResult(
            chash, orig_hash, subject, delta_ratio,
            orig_stats, cherry_stats, None
        )


def check_verbose(result, threshold):
    """Print verbose output for a single check result

    Args:
        result (CheckResult): The check result to print
        threshold (float): Delta threshold for highlighting problems
    """
    chash_short = result.chash[:10]

    if result.reason:
        if result.reason == 'merge_commit':
            tout.info(f'{chash_short}: {result.subject}')
            tout.info('  → Skipped (merge commit)')
            tout.info('')
        elif result.reason == 'not_cherry_pick':
            tout.info(f'{chash_short}: {result.subject}')
            tout.info('  → Not a cherry-pick (no original commit found)')
            tout.info('')
        elif result.reason.startswith('small_commit'):
            lines = result.reason.split('_')[2]
            tout.info(f'{chash_short}: {result.subject}')
            tout.info(f'  → Skipped (only {lines} lines changed)')
            tout.info('')
        elif result.reason.startswith('error'):
            error = result.reason[6:]  # Remove 'error_' prefix
            tout.info(f'{chash_short}: {result.subject}')
            tout.info(f'  → Error checking delta: {error}')
            tout.info('')
    else:
        # Valid result with analysis
        tout.info(f'{chash_short}: {result.subject}')
        tout.info(f'  → Original: {result.orig_hash[:12]} '
                  f'({result.orig_stats.files} files, '
                  f'{result.orig_stats.inserted}+/'
                  f'{result.orig_stats.deleted}- lines)')
        tout.info(f'  → Cherry-pick: {result.cherry_stats.files} files, '
                  f'{result.cherry_stats.inserted}+/'
                  f'{result.cherry_stats.deleted}- lines')
        if result.delta_ratio > threshold:
            tout.info(f'  → Delta ratio: {result.delta_ratio:.1%} '
                      f'⚠️  LARGE DELTA!')
        else:
            tout.info(f'  → Delta ratio: {result.delta_ratio:.1%} ✓')
        tout.info('')


def print_check_header():
    """Print the standard header for check output table"""
    header = (f'{"Cherry-pick":<11} {"Delta%":>6} '
              f'{"Original":<10} Subject')
    dashes = f'{"-" * 11} {"-" * 6} {"-" * 10} -------'
    tout.info(header)
    tout.info(dashes)


def format_problem_commit(result, threshold):
    """Format a problematic commit in the standard table format

    Args:
        result (CheckResult): The check result to format
        threshold (float): Delta threshold for coloring

    Returns:
        str: Formatted commit line
    """
    delta_pct_val = result.delta_ratio * 100
    delta_pct = f'{delta_pct_val:.0f}'
    pct_field = f'{delta_pct:>6}'

    # Apply color
    col = terminal.Color()
    threshold_pct = threshold * 100
    if delta_pct_val >= 50:
        pct_field = col.build(terminal.Color.RED, pct_field)
    elif delta_pct_val >= threshold_pct:
        pct_field = col.build(terminal.Color.YELLOW, pct_field)

    return (f'{result.chash[:10]}  {pct_field} '
            f'{result.orig_hash[:10]} {result.subject}')


def get_branch_commits():
    """Get commits on current branch that differ from ci/master

    Returns:
        tuple: (current_branch, commits) where commits is a list of
            (full_hash, short_hash, subject) tuples
    """
    current_branch = run_git(['rev-parse', '--abbrev-ref', 'HEAD'])

    # Get all commits on current branch that aren't in ci/master
    commit_list = run_git(['log', '--reverse', '--format=%H|%h|%s',
                          f'{BRANCH_MASTER}..HEAD'])

    if not commit_list:
        return current_branch, []

    # Parse commit_list format: "full_hash|short_hash|subject" per line
    commits = []
    for line in commit_list.split('\n'):
        if line:
            parts = line.split('|', 2)
            commits.append((parts[0], parts[1], parts[2]))

    return current_branch, commits


def check_already_applied(commits, target_branch='ci/master'):
    """Check which commits are already applied to the target branch

    Args:
        commits (list): List of CommitInfo tuples to check
        target_branch (str): Branch to check against (default: ci/master)

    Returns:
        tuple: (new_commits, applied) where:
            new_commits: list of CommitInfo for commits not yet applied
            applied: list of CommitInfo for commits already applied
    """
    new_commits = []
    applied = []

    for commit in commits:
        # Check if a commit with the same subject exists in target branch
        try:
            # Use git log with --grep to search for the subject
            # Escape any special characters in the subject for grep
            escaped_subject = commit.subject.replace('"', '\\"')
            result = run_git(['log', '--oneline', target_branch,
                             f'--grep={escaped_subject}', '-1'])
            if result.strip():
                # Found a commit with the same subject
                applied.append(commit)
                tout.info(f'Skipping {commit.chash} (already applied): '
                         f'{commit.subject}')
            else:
                new_commits.append(commit)
        except Exception:  # pylint: disable=broad-except
            # If grep fails, assume the commit is not applied
            new_commits.append(commit)

    return new_commits, applied


def build_applied_map(commits):
    """Build a mapping of commit hashes to their applied counterparts

    Checks which commits have already been applied to the target branch
    and returns a dict mapping original hashes to the applied hashes.

    Args:
        commits (list): List of CommitInfo tuples to check

    Returns:
        dict: Mapping of original commit hash to applied commit hash
    """
    _, applied = check_already_applied(commits)

    applied_map = {}
    if applied:
        for c in applied:
            escaped_subject = c.subject.replace('"', '\\"')
            result = run_git(['log', '--oneline', 'ci/master',
                             f'--grep={escaped_subject}', '-1'])
            if result.strip():
                applied_hash = result.split()[0]
                applied_map[c.hash] = applied_hash
        tout.info(f'Found {len(applied)} potentially already applied'
                  ' commit(s)')
    return applied_map


def show_commit_diff(res, no_colour=False):
    """Show the difference between original and cherry-picked commit patches

    Args:
        res (CheckResult): Check result with commit hashes
        no_colour (bool): Disable colour output
    """
    tout.info(f'\n--- Patch diff between original {res.orig_hash[:8]} and '
              f'cherry-picked {res.chash[:8]} ---')

    # Get the patch content of each commit
    orig_patch = run_git(['show', '--no-ext-diff', res.orig_hash])
    cherry_patch = run_git(['show', '--no-ext-diff', res.chash])

    # Create temporary files and diff them
    with tempfile.NamedTemporaryFile(mode='w', suffix='_orig.patch',
                                     delete=False) as orig_file:
        orig_file.write(orig_patch)
        orig_path = orig_file.name

    with tempfile.NamedTemporaryFile(mode='w', suffix='_cherry.patch',
                                     delete=False) as cherry_file:
        cherry_file.write(cherry_patch)
        cherry_path = cherry_file.name

    try:
        # Diff the two patch files using system diff
        diff_args = ['diff', '-u']
        if not no_colour:
            diff_args.append('--color=always')
        diff_args.extend([orig_path, cherry_path])

        patch_diff = command.output(*diff_args, raise_on_error=False)
        if patch_diff:
            print(patch_diff)
        else:
            tout.info('(Patches are identical)')
    finally:
        # Clean up temporary files
        os.unlink(orig_path)
        os.unlink(cherry_path)

    tout.info('--- End patch diff ---\n')


def show_check_summary(bad, verbose, threshold, show_diff, no_colour):
    """Show summary of check results

    Args:
        bad (list): List of CheckResult objects with problems
        verbose (bool): Whether to show verbose output
        threshold (float): Delta threshold for problems
        show_diff (bool): Whether to show diffs for problems
        no_colour (bool): Whether to disable colour in diffs

    Returns:
        int: 0 if no problems, 1 if problems found
    """
    if bad:
        if verbose:
            tout.info(f'Found {len(bad)} commit(s) with large deltas:')
            tout.info('')
            print_check_header()
            for res in bad:
                tout.info(format_problem_commit(res, threshold))
                if show_diff:
                    show_commit_diff(res, no_colour)
        else:
            tout.info(f'{len(bad)} problem commit(s) found')
        return 1
    if verbose:
        tout.info('All cherry-picks have acceptable deltas ✓')
    return 0


def do_check(args, dbs):  # pylint: disable=unused-argument
    """Check current branch for cherry-picks with large deltas

    Args:
        args (Namespace): Parsed arguments with 'threshold', 'min_lines',
            'verbose', and 'diff' attributes
        dbs (Database): Database instance

    Returns:
        int: 0 on success, 1 on failure
    """
    threshold = args.threshold
    min_lines = args.min_lines
    verbose = args.verbose
    show_diff = args.diff

    current_branch, commits = get_branch_commits()

    if verbose:
        tout.info(f'Checking branch: {current_branch}')
        tout.info(f'Delta threshold: {threshold:.1%}')
        tout.info(f'Minimum lines to check: {min_lines}')
        tout.info(f'Found {len(commits)} commits to check')
        tout.info('')

    bad = []
    header_printed = False

    # Process commits using the generator
    for res in check_commits(commits, min_lines):
        is_problem = not res.reason and res.delta_ratio > threshold

        if verbose:
            check_verbose(res, threshold)
        elif is_problem:
            # Non-verbose: only show problems on one line
            if not header_printed:
                print_check_header()
                header_printed = True
            tout.info(format_problem_commit(res, threshold))

        if is_problem:
            bad.append(res)
            if show_diff:
                show_commit_diff(res, args.no_colour)

    return show_check_summary(bad, verbose, threshold, show_diff,
                              args.no_colour)


def do_check_gitlab(args, dbs):  # pylint: disable=unused-argument
    """Check GitLab permissions for the configured token

    Args:
        args (Namespace): Parsed arguments with 'remote' attribute
        dbs (Database): Database instance (unused)

    Returns:
        int: 0 on success with sufficient permissions, 1 otherwise
    """
    remote = args.remote

    perms = gitlab_api.check_permissions(remote)
    if not perms:
        return 1

    tout.info(f"GitLab permission check for remote '{remote}':")
    tout.info(f"  Host:         {perms.host}")
    tout.info(f"  Project:      {perms.project}")
    tout.info(f"  User:         {perms.user}")
    tout.info(f"  Access level: {perms.access_name}")
    tout.info('')
    tout.info('Permissions:')
    tout.info(f"  Push branches:    {'Yes' if perms.can_push else 'No'}")
    tout.info(f"  Create MRs:       {'Yes' if perms.can_create_mr else 'No'}")
    tout.info(f"  Merge MRs:        {'Yes' if perms.can_merge else 'No'}")

    if not perms.can_create_mr:
        tout.warning('')
        tout.warning('Insufficient permissions to create merge requests!')
        tout.warning('The user needs at least Developer access level.')
        return 1

    tout.info('')
    tout.info('All required permissions are available.')
    return 0


def drift_read_accepts():
    """Read the accept file, which records intentional deltas from upstream

    Return:
        list of drift.Accept: Entries, empty if there is no accept file
    """
    if not os.path.exists(drift.ACCEPT_FILE):
        return []
    return drift.read_accepts(tools.read_file(drift.ACCEPT_FILE, binary=False))


def drift_write_accepts(accepts):
    """Write the accept file

    Args:
        accepts (list of drift.Accept): Entries to write
    """
    tools.write_file(drift.ACCEPT_FILE, drift.format_accepts(accepts),
                     binary=False)


def drift_cherry_picks(rng, sources):
    """Sort the cherry-picks in a range into genuine ones and orphans

    A commit which records '(cherry picked from commit X)' has only really
    come from upstream if X is reachable from a tracked source.  Some
    downstream commits are picked from a series which upstream never took, so
    X exists in no source; their change is downstream-only work which upstream
    does not have.  Treating those as cherry-picks would report the work as
    drift and offer to revert it, so they are separated out here.

    Args:
        rng (str): Commit range to examine, e.g. 'base..branch'
        sources (list of str): Tracked source refs to resolve against

    Return:
        tuple:
            set of str: Hashes of the genuine cherry-picks
            set of str: Hashes of the orphans, picked from an unmerged series
    """
    reachable = set()
    for ref in sources:
        if gitutil.ref_exists(ref):
            reachable.update(gitutil.rev_list(ref))

    # A trailer may abbreviate the hash, so index the reachable commits by
    # each prefix length which turns up
    by_len = {}

    genuine = set()
    orphan = set()
    for chash, body in gitutil.log_bodies(rng, no_merges=True):
        found = RE_CHERRY_PICK.findall(body)
        if not found:
            continue
        ref = found[-1]
        if len(ref) >= 40:
            known = ref in reachable
        else:
            prefixes = by_len.get(len(ref))
            if prefixes is None:
                prefixes = {full[:len(ref)] for full in reachable}
                by_len[len(ref)] = prefixes
            known = ref in prefixes
        if known:
            genuine.add(chash)
        else:
            orphan.add(chash)
    return genuine, orphan


def drift_downstream_commits(source, branch, sources=None):
    """Find the commits made downstream which did not come from upstream

    Commits added since the trees diverged which carry no cherry-pick line are
    downstream-original: they are the reason the trees are allowed to differ.
    Merges are ignored, since their content belongs to the commits they merge.

    A commit picked from a series which upstream never took counts as
    downstream-original too, since upstream has no such change to match.

    Args:
        source (str): Source branch name, e.g. 'us/master'
        branch (str): Downstream branch to examine, e.g. 'ci/master'
        sources (list of str): Tracked source refs to resolve cherry-picks
            against, or None to use just the source being compared

    Return:
        tuple:
            set of str: Hashes of the downstream-original commits
            set of str: Paths which those commits touch
            set of str: Hashes of the orphans, picked from an unmerged series
    """
    fork = gitutil.merge_base(branch, source)
    rng = f'{fork}..{branch}'
    genuine, orphan = drift_cherry_picks(rng, sources or [source])

    hashes = set()
    paths = set()
    for chash, files in gitutil.log_commits_with_files(rng, no_merges=True):
        if chash not in genuine:
            hashes.add(chash)
            paths.update(files)
    return hashes, paths, orphan


def drift_blame(branch, path, down_hashes):
    """Find which downstream lines of a file a downstream commit wrote

    Args:
        branch (str): Downstream branch to blame
        path (str): File to blame
        down_hashes (set of str): Hashes of the downstream-original commits

    Return:
        dict: Maps line number to the hash of the downstream-original commit
            which last touched it; lines left by a cherry-pick are absent
    """
    out = gitutil.blame(branch, path)
    blame = {}
    for line in out.splitlines():
        match = RE_BLAME_LINE.match(line)
        if match and match.group(1) in down_hashes:
            blame[int(match.group(2))] = match.group(1)
    return blame


def drift_collect(dbs, source, branch, deep=False, base=None):  # pylint: disable=too-many-locals
    """Compare the downstream tree with upstream and classify every delta

    Args:
        dbs (Database): Database instance
        source (str): Source branch name, e.g. 'us/master'
        branch (str): Downstream branch to examine, e.g. 'ci/master'
        deep (bool): Blame the files which downstream commits touch, to find
            drift inside them; otherwise assume such files are wholly intended
        base (str): Upstream commit to compare against, overriding the tracked
            source position; None to use the position from the database.  An
            override lets drift be computed for a past point, since it makes
            the result a pure function of (branch, base)

    Return:
        DriftInfo: Result of the comparison, or None if the source is unknown
    """
    if base is None:
        base = dbs.source_get(source)
        if not base:
            tout.error(f"Source '{source}' not found - use "
                       "'pickman add-source'")
            return None

    accepts = drift_read_accepts()
    # Resolve cherry-picks against every tracked source, so that a commit
    # picked from a series which upstream never took is not mistaken for one
    # whose change upstream already has
    tracked = [name for name, _ in dbs.source_get_all()] or [source]
    down_hashes, down_paths, orphans = drift_downstream_commits(
        source, branch, tracked)

    diff = gitutil.diff(base, branch)
    fdiffs = drift.parse_diff(diff)

    if deep:
        to_blame = sum(1 for fd in fdiffs if fd.path in down_paths
                       and not fd.binary and not fd.deleted)
        if to_blame:
            tout.info(f'Blaming {to_blame} file(s) touched downstream '
                      '(this may take a while)...')

    verdicts = {}
    binary = []
    deleted = []
    subtree = []
    skipped = 0
    subtree_paths = tuple(SUBTREE_NAMES)
    for fdiff in fdiffs:
        # A vendored subtree is managed by update-subtree.sh, not by picks.
        # Its contents differ from upstream because they track a different
        # project, so calling that drift would offer to revert work which is
        # not pickman's, and calling it absent would advise a cherry-pick
        # which could not bring it.  Note that provenance alone cannot tell:
        # a subtree squash commit carries no cherry-pick line, so it reads as
        # downstream-original and its files as wanted - which means the same
        # file can look wanted or drifted depending only on how recently the
        # subtree was pulled.  Hence the explicit test here
        if fdiff.path.startswith(subtree_paths):
            subtree.append(fdiff.path)
            continue
        touched = fdiff.path in down_paths
        if fdiff.binary:
            if not touched and not drift.match_accept(accepts, fdiff.path,
                                                      drift.ALL_HUNKS):
                binary.append(fdiff.path)
            continue
        blame = None
        if touched:
            # A file which is gone downstream cannot be blamed, and a shallow
            # run does not try to tell one hunk from another.  Either way the
            # downstream commits get the benefit of the doubt, but the two are
            # worth counting apart: only one of them can be acted on
            if fdiff.deleted or not deep:
                verdicts[fdiff.path] = [
                    drift.Verdict(hunk, drift.WANTED, 'downstream file')
                    for hunk in fdiff.hunks]
                if fdiff.deleted:
                    deleted.append(fdiff.path)
                else:
                    skipped += 1
                continue
            blame = drift_blame(branch, fdiff.path, down_hashes)
        verdicts[fdiff.path] = drift.classify(fdiff, accepts, blame)

    return DriftInfo(base, fdiffs, verdicts, binary, orphans, down_paths,
                     skipped, deleted, subtree)


def drift_absent_paths(info):
    """List the files which upstream has and this tree never received

    Args:
        info (DriftInfo): Result from drift_collect()

    Return:
        list of tuple: (path, number of lines the file has), largest first
    """
    out = []
    for path, verdicts in info.verdicts.items():
        hunks = [vdt.hunk for vdt in verdicts if vdt.state == drift.ABSENT]
        if hunks:
            lines = sum(1 for hunk in hunks for line in hunk.lines[1:]
                        if line[:1] in '+-')
            out.append((path, lines))
    return sorted(out, key=lambda item: (-item[1], item[0]))


def drift_paths(info):
    """List the files which have drift, worst first

    Args:
        info (DriftInfo): Result from drift_collect()

    Return:
        list of tuple: (path, number of drift hunks), most drift first
    """
    out = []
    for path, verdicts in info.verdicts.items():
        count = sum(1 for vdt in verdicts if vdt.state == drift.DRIFT)
        if count:
            out.append((path, count))
    out += [(path, 0) for path in info.binary]
    return sorted(out, key=lambda item: (-item[1], item[0]))


def drift_state_counts(info):
    """Count the hunks in each classification across all files

    Args:
        info (DriftInfo): Result from drift_collect()

    Return:
        dict: Maps state (WANTED, ACCEPTED, DRIFT) to the number of hunks
    """
    states = {drift.WANTED: 0, drift.ACCEPTED: 0, drift.REORDER: 0,
              drift.ABSENT: 0, drift.DRIFT: 0}
    for verdicts in info.verdicts.values():
        for vdt in verdicts:
            states[vdt.state] += 1
    return states


def drift_percent(states):
    """Work out what fraction of the divergence from upstream is drift

    This measures how much of what makes the downstream branch differ from
    upstream is spurious rather than intended: the drift hunks as a share of
    every hunk which differs.  Binary files are not counted, since they have
    no hunks.

    Args:
        states (dict): Hunk counts by state from drift_state_counts()

    Return:
        float: Drift as a percentage of all differing hunks, 0.0 if there are
            no hunks at all
    """
    total = sum(states.values())
    if not total:
        return 0.0
    return 100.0 * states[drift.DRIFT] / total


# Build used to check that a revert leaves a tree which still compiles.  It
# writes out of tree, so it does not dirty the working copy it is checking
DEFAULT_BUILD_CMD = 'um build sandbox'

# How many lines of a failed build to show; the first errors name the symbol
BUILD_ERROR_LINES = 8


def drift_build_cmd(args):
    """Work out the build command which checks a revert

    Args:
        args (Namespace): Parsed arguments, read for 'build_cmd' and
            'no_build'

    Return:
        str: Command to run, or None if building is turned off
    """
    if getattr(args, 'no_build', False):
        return None
    return (getattr(args, 'build_cmd', None) or
            gitlab_api.get_config_value('build', 'command') or
            DEFAULT_BUILD_CMD)


def drift_build_ok(build_cmd):
    """Run a check against the reverted working tree

    The identifier check cannot see a short name like a three-letter struct
    member, so building is the only answer to 'does this revert leave
    something which compiles'.  It runs against the reverted working tree,
    before anything is committed.

    A command which passes says only that this check passed.  It does not say
    the revert is right: a downstream test may depend on the very bytes being
    reverted, which compiles perfectly and fails when run.  The command is
    whatever is configured, so it can build and test both - the exit status is
    all that is looked at.

    Args:
        build_cmd (str): Command to run

    Return:
        tuple:
            bool: True if the build succeeded
            str: The tail of the output when it did not, else ''
    """
    tout.info(f'  building with: {build_cmd}')
    # Run through a shell, so that a command may chain with && or | as the
    # documentation says it can.  Splitting it into words instead would hand
    # '&&' to the first program as an argument, which fails in a way that
    # looks like the revert being at fault
    res = command.run_one('sh', '-c', build_cmd, capture=True,
                          capture_stderr=True, raise_on_error=False)
    if not res.return_code:
        return True, ''
    out = (res.combined or res.stderr or res.stdout or '')
    lines = [line for line in out.splitlines() if line.strip()]
    return False, chr(10).join(lines[:BUILD_ERROR_LINES])


# Names searched for in one go, to keep the command line sane
GREP_BATCH = 200

# How many declined files to name before summarising the rest
DECLINE_SHOWN = 5

# Files which describe rather than build, so nothing in them can break
DOC_SUFFIXES = ('.rst', '.txt', '.md', '.yaml', '.yml', '.json')


def drift_grep_names(names, branch):
    """Find every line in a branch which uses any of some names

    One search covers the whole area, since a search for each name in turn
    means thousands of git processes and takes minutes on a large one.

    Args:
        names (list of str): Names to look for, matched as whole words
        branch (str): Branch to search

    Return:
        list of tuple: (path, line number, text) for each matching line
    """
    hits = []
    names = sorted(names)
    for pos in range(0, len(names), GREP_BATCH):
        cmd = ['git', 'grep', '-n', '-w']
        for name in names[pos:pos + GREP_BATCH]:
            cmd += ['-e', name]
        cmd.append(branch)
        out = command.output(*cmd, raise_on_error=False)
        for line in out.splitlines():
            # 'git grep <rev>' prints 'rev:path:lineno:text'
            parts = line.split(':', 3)
            if len(parts) == 4 and parts[2].isdigit():
                hits.append((parts[1], int(parts[2]), parts[3]))
    return hits


# pylint: disable-next=too-many-locals,too-many-branches
def drift_load_bearing(info, paths, branch):
    """Find files whose revert would remove something still in use

    Reverting a hunk takes out the lines it adds.  Where those lines define
    something and other code still refers to it, the revert leaves the tree
    unable to build.  Nothing else catches this: the tree builds before, so
    the first sign of trouble is CI failing on a merge request already open.

    A use which the revert itself removes does not count, since it goes away
    with the definition.  That is judged line by line rather than by file: a
    file being reverted for one hunk may still use the name somewhere the
    revert does not touch, and taking the whole file as safe would miss it.

    Args:
        info (DriftInfo): Result from drift_collect()
        paths (list of str): Files about to be reverted
        branch (str): Branch to search for remaining users

    Return:
        dict: Maps path to a list of (name, file still using it, what the
            revert does to it), for the files which cannot safely be reverted
    """
    # The history file records every run, so it mentions names which nothing
    # uses; searching it would decline almost everything
    ignore = {HISTORY_FILE, drift.ACCEPT_FILE}

    names_for = {}
    dropped = {}
    hunks_for = {}
    for path in paths:
        hunks = [vdt.hunk for vdt in info.verdicts.get(path, [])
                 if vdt.state == drift.DRIFT]
        hunks_for[path] = hunks
        names = drift.removed_identifiers(hunks)
        if names:
            names_for[path] = names
        # Lines this revert takes away, so a use on one of them goes too
        dropped[path] = {num for hunk in hunks for num in hunk.added}

    every = set()
    for names in names_for.values():
        every.update(names)
    if not every:
        return {}

    hits = drift_grep_names(every, branch)

    held = {}
    for path, names in names_for.items():
        for name in sorted(names):
            for user, num, text in hits:
                if user == path or user in ignore:
                    continue
                # Cheap test first: the regex below is far more costly and
                # most lines in the batch matched some other name
                if name not in text:
                    continue
                if user.endswith(DOC_SUFFIXES):
                    # Documentation cannot fail to build
                    continue
                # A line which defines the name itself is not a user of
                # ours: every linker script declares its own __rel_dyn_end,
                # and two device trees may label unrelated nodes alike
                if name in drift.defined_names(text, user):
                    continue
                # A name inside a string or a comment is not a dependency:
                # reverting a definition cannot break a line which only
                # mentions the word
                if not drift.is_code_reference(text, name):
                    continue
                # A use which is itself being reverted disappears with the
                # definition, so it does not hold anything back
                if user in dropped and num in dropped[user]:
                    continue
                # Say which hazard it is: a name on both sides of the hunk
                # is not taken away, it goes back to an older form, and the
                # callers of the newer one are what break
                verb = ('changes' if drift.survives_revert(hunks_for[path],
                                                           name)
                        else 'removes')
                held.setdefault(path, []).append((name, user, verb))
                break
            if path in held:
                break
    return held


def drift_select(bad, info, patterns=None, unambiguous=False):
    """Narrow a list of drifted files down to those asked for

    Args:
        bad (list of tuple): (path, hunk count) from drift_paths()
        info (DriftInfo): Result from drift_collect()
        patterns (list of str): Globs a path must match, or None for any
        unambiguous (bool): True to keep only files which no downstream
            commit has touched, whose drift therefore cannot be justified

    Return:
        list of tuple: The entries which are wanted, in the order given
    """
    out = []
    for path, count in bad:
        if unambiguous and path in info.touched:
            continue
        if patterns and not any(fnmatch.fnmatch(path, pat) or
                                path.startswith(pat.rstrip('*'))
                                for pat in patterns):
            continue
        out.append((path, count))
    return out


def drift_show_orphans(info):
    """List the commits picked from a series no tracked source has

    Upstream may take such a series later, at which point the commit becomes
    an ordinary cherry-pick and its delta should match upstream again, so
    these are worth looking at again as upstream moves.

    Args:
        info (DriftInfo): Result from drift_collect()
    """
    tout.info(f'{len(info.orphans)} commit(s) picked from a series no '
              'tracked source has:')
    for line in gitutil.commit_summaries(sorted(info.orphans)):
        tout.info(f'  {line}')


def drift_show_fingerprints(info):
    """List each drift hunk with the fingerprint which identifies it

    The fingerprint is what 'drift-accept -u' takes, so without this there is
    no way to accept a single hunk rather than a whole file.

    Args:
        info (DriftInfo): Result from drift_collect()
    """
    for path, _ in drift_paths(info):
        hunks = [vdt.hunk for vdt in info.verdicts.get(path, [])
                 if vdt.state == drift.DRIFT]
        if not hunks:
            continue
        tout.info(path)
        for hunk in hunks:
            plus = sum(1 for line in hunk.lines[1:] if line.startswith('+'))
            minus = sum(1 for line in hunk.lines[1:] if line.startswith('-'))
            tout.info(f'  {hunk.fingerprint}  {hunk.lines[0][:46]:<46} '
                      f'+{plus} -{minus}')


def drift_show_report(info, show_list, show_diff):
    """Show what the comparison with upstream found

    Args:
        info (DriftInfo): Result from drift_collect()
        show_list (bool): List each file which has drift
        show_diff (bool): Show the drift hunks themselves

    Return:
        int: 0 if the tree has no drift, 1 if it has
    """
    states = drift_state_counts(info)

    bad = drift_paths(info)
    total = len(info.fdiffs)
    tout.info(f'Comparing with upstream {info.base[:12]}')
    tout.info(f'  {total} file(s) differ from upstream')
    tout.info(f'  {states[drift.WANTED]} hunk(s) wanted, explained by '
              'downstream commits')
    tout.info(f'  {states[drift.ACCEPTED]} hunk(s) accepted by '
              f'{drift.ACCEPT_FILE}')
    if states[drift.REORDER]:
        tout.info(f'  {states[drift.REORDER]} hunk(s) only reorder lines, '
                  'so they say the same as upstream')
    if info.orphans:
        tout.info(f'  {len(info.orphans)} commit(s) picked from a series no '
                  "tracked source has, treated as downstream ('-o' to list)")
    absent = drift_absent_paths(info)
    if absent:
        lines = sum(count for _, count in absent)
        tout.info(f'  {len(absent)} file(s) upstream has which this tree '
                  f'never received ({lines} lines), reported apart from '
                  "drift ('drift-fix --missing')")
    tout.info(f'  {states[drift.DRIFT]} hunk(s) of drift in {len(bad)} '
              f'file(s), {drift_percent(states):.0f}% of divergence')

    # Drift in a file no downstream commit has touched cannot have a
    # justification, so say how much of the total is certain
    sure = drift_select(bad, info, None, True)
    if sure and len(sure) != len(bad):
        hunks = sum(count for _, count in sure)
        tout.info(f'    of which {hunks} hunk(s) in {len(sure)} file(s) are '
                  "in files no downstream commit has touched ('drift-fix -u')")

    if info.skipped:
        tout.info(f'  {info.skipped} file(s) which downstream commits touch '
                  'were taken as wanted without being looked inside; drop '
                  "'-s' to blame them")
    if info.subtree:
        tout.info(f'  {len(info.subtree)} file(s) are in a vendored subtree; '
                  'update-subtree.sh owns these, but they still differ from '
                  'upstream and must survive each pull')
    if info.deleted:
        tout.info(f'  {len(info.deleted)} file(s) deleted downstream cannot '
                  "be blamed, so their hunks are taken as wanted ('-l' to "
                  'list)')

    if not bad:
        tout.info('')
        tout.info('No drift from upstream ✓')
        return 0

    if show_list:
        tout.info('')
        for path, count in bad:
            what = f'{count} hunk(s)' if count else 'binary'
            tout.info(f'  {what:>12}  {path}')
        for path in sorted(info.deleted):
            tout.info(f'  {"deleted":>12}  {path}')

    if show_diff:
        # Print the patch plainly, so that it can be piped to 'git apply -R'
        print(drift.build_patch(info.fdiffs, info.verdicts), end='')

    tout.info('')
    tout.info("Run 'pickman drift-fix' to revert these to upstream, or "
              "'pickman drift-accept' to record one as intentional")
    return 1


def resolve_compare(args):
    """Work out the downstream branch and upstream base for a comparison

    Applies the --upstream override, which is read-only and does not touch the
    database.  It lets drift be computed against a chosen upstream commit - for
    example a reconstructed historical position - rather than the tracked one.

    Args:
        args (Namespace): Parsed arguments, read for 'branch' and 'upstream'

    Return:
        tuple:
            str: Downstream branch to examine
            str: Upstream commit to compare against, or None to use the
                tracked source position

    Raises:
        ValueError: If an upstream commit is given which does not resolve
    """
    base = getattr(args, 'upstream', None)
    if base and not gitutil.ref_exists(f'{base}^{{commit}}'):
        raise ValueError(f"Upstream commit '{base}' not found")
    return args.branch, base


def do_drift(args, dbs):
    """Report deltas from upstream which no downstream commit accounts for

    Args:
        args (Namespace): Parsed arguments with 'source', 'branch', 'shallow',
            'list', 'diff', 'fingerprints', 'orphans' and 'upstream'
        dbs (Database): Database instance

    Return:
        int: 0 if the tree has no drift, 1 if it has or on error
    """
    try:
        branch, base = resolve_compare(args)
    except ValueError as exc:
        tout.error(str(exc))
        return 1
    info = drift_collect(dbs, args.source, branch, not args.shallow, base=base)
    if not info:
        return 1
    ret = drift_show_report(info, args.list, args.diff)
    if args.fingerprints:
        tout.info('')
        drift_show_fingerprints(info)
    if getattr(args, 'orphans', False) and info.orphans:
        tout.info('')
        drift_show_orphans(info)
    return ret


def drift_accept_paths(args):
    """Work out which paths a drift-accept call covers

    Args:
        args (Namespace): Parsed arguments, read for 'path' and 'from_file'

    Return:
        list of str: Paths to accept, in the order given

    Raises:
        ValueError: If neither or both of the two are given, or the file
            names nothing
    """
    from_file = getattr(args, 'from_file', None)
    if bool(args.path) == bool(from_file):
        raise ValueError("Give either a path or --from, not both")
    if not from_file:
        return [args.path]

    if from_file == '-':
        text = sys.stdin.read()
    else:
        text = tools.read_file(from_file, binary=False)
    paths = [line.strip() for line in text.splitlines()
             if line.strip() and not line.startswith('#')]
    if not paths:
        raise ValueError(f"No paths found in '{from_file}'")
    return paths


def do_drift_accept(args, dbs):  # pylint: disable=unused-argument
    """Record one or more deltas from upstream as intentional

    Args:
        args (Namespace): Parsed arguments with 'path', 'from_file', 'hunk',
            'message' and 'dry_run'
        dbs (Database): Database instance (unused)

    Return:
        int: 0 on success, 1 on failure
    """
    try:
        paths = drift_accept_paths(args)
    except (ValueError, IOError) as exc:
        tout.error(str(exc))
        return 1

    accepts = drift_read_accepts()
    have = {(ent.pattern, ent.fingerprint) for ent in accepts}

    added = []
    for path in paths:
        new = drift.Accept(path, args.hunk, args.message)
        if (new.pattern, new.fingerprint) in have:
            # With a list, an entry already recorded is not worth failing over
            if len(paths) == 1:
                tout.error(f"'{path}' is already accepted")
                return 1
            tout.info(f"  {path}: already accepted, skipped")
            continue
        have.add((new.pattern, new.fingerprint))
        added.append(new)

    what = ('every hunk' if args.hunk == drift.ALL_HUNKS
            else f'hunk {args.hunk}')
    if getattr(args, 'dry_run', False):
        tout.info(f'Would accept {what} in {len(added)} path(s):')
        for ent in added:
            tout.info(f'  {ent.pattern}')
        return 0

    if not added:
        tout.info('Nothing to accept')
        return 0

    drift_write_accepts(accepts + added)
    if len(added) == 1:
        tout.info(f"Accepted {what} in '{added[0].pattern}': "
                  f'{added[0].reason}')
    else:
        tout.info(f'Accepted {what} in {len(added)} path(s): '
                  f'{args.message}')
    tout.info(f'Updated {drift.ACCEPT_FILE} - commit this to record it')
    return 0


# Where the knowledge of which commit adds a file came from
ORIGIN_RECORDED = 'recorded'
ORIGIN_INFERRED = 'inferred'


def drift_absent_origin(dbs, source_id, source, paths):
    """Find the commit which adds each file this tree never received

    A parked conflict in the database is a record: pickman tried that commit
    and set it aside, so its files are missing for a known reason.  Where
    there is no such record the log is searched instead, which gives a good
    starting point but is a guess - the file may have gone missing during a
    pick which pickman believed had succeeded.

    Args:
        dbs (Database): Database instance
        source_id (int): Source branch id
        source (str): Source branch to search when nothing is recorded
        paths (list of str): Files which are absent downstream

    Return:
        dict: Maps path to (hash, subject, where it came from)
    """
    want = set(paths)
    found = {}

    # A parked commit is a record of why the file is missing
    for _, chash, subj in status_parked(dbs, source_id):
        if not gitutil.ref_exists(f'{chash}^{{commit}}'):
            continue
        for _, files in gitutil.log_commits_with_files(f'{chash}^!'):
            for path in files:
                if path in want and path not in found:
                    found[path] = (chash, subj, ORIGIN_RECORDED)

    # Anything left has to be looked up, which is a guess rather than a record
    for path in paths:
        if path in found:
            continue
        out = command.output('git', 'log', '--diff-filter=A', '-1',
                             '--format=%H%x00%s', source, '--', path,
                             raise_on_error=False).strip()
        if '\x00' in out:
            chash, subj = out.split('\x00', 1)
            found[path] = (chash, subj, ORIGIN_INFERRED)
    return found


def drift_absent_partial(origin, absent):
    """Find files whose restore would apply only part of a commit

    A file which arrives without the rest of its commit is half a change: the
    Makefile entry which builds it, or the devicetree which includes it, may
    still be missing.  Cherry-picking the commit brings the whole thing, so
    that is what should happen instead.

    Args:
        origin (dict): Result from drift_absent_origin()
        absent (set of str): Every file which is absent downstream

    Return:
        dict: Maps path to (hash, subject, where from, other files still
            absent, files the commit also changes)
    """
    partial = {}
    for path, (chash, subj, where) in origin.items():
        others = set()
        changed = set()
        for status, name in gitutil.commit_file_status(chash):
            if name == path:
                continue
            if status == 'A':
                if name in absent:
                    others.add(name)
            else:
                # A file the commit changes rather than adds is present here,
                # but its hunks came with the commit and are missing too
                changed.add(name)
        if others or changed:
            partial[path] = (chash, subj, where, sorted(others),
                             sorted(changed))
    return partial


def drift_absent_reason(dbs, source_id, paths):
    """Find parked commits which add the files a restore would bring back

    An absent file is often not cruft at all: the commit which adds it was
    parked as a conflict and never retried.  Saying so explains the change far
    better than guessing at a mangled conflict resolution.

    Args:
        dbs (Database): Database instance
        source_id (int): Source branch id
        paths (list of str): Files which are absent downstream

    Return:
        dict: Maps path to the (hash, subject) parked commit which adds it
    """
    parked = status_parked(dbs, source_id)
    if not parked:
        return {}
    want = set(paths)
    found = {}
    for _, chash, subj in parked:
        if not gitutil.ref_exists(f'{chash}^{{commit}}'):
            continue
        for _, files in gitutil.log_commits_with_files(f'{chash}^!'):
            for path in files:
                if path in want:
                    found.setdefault(path, (chash, subj))
    return found


def drift_absent_msg(area, paths, info, reasons):
    """Compose the commit message for restoring files upstream has

    Args:
        area (str): Area of the tree, e.g. 'arch/arm'
        paths (list of str): Files being restored
        info (DriftInfo): Result from drift_collect()
        reasons (dict): Parked commits by path, from drift_absent_reason()

    Return:
        str: Commit message
    """
    files = chr(10).join(f' - {path}' for path in paths)
    out = [f'{area}: Restore files which upstream has',
           '',
           'Upstream has these files and this tree never received them.',
           'Nothing was mangled on the way in: the change simply did not',
           'arrive, so this adds them back as upstream has them at',
           f'{info.base[:12]}',
           '']
    if reasons:
        named = sorted(set(reasons.values()))
        out += ['The commit which adds them is parked as a conflict, so this',
                'is work which was tried and set aside rather than lost:', '']
        out += [f' - {chash[:11]} {subj}' for chash, subj in named]
        out += ['']
    out += ['Note that a restored file may need a Makefile or Kconfig entry',
            'to be of any use, so this wants a build before it is merged.',
            '',
            f'This restores {len(paths)} file(s):',
            files, '']
    return chr(10).join(out)


def drift_commit_msg(area, paths, info):
    """Compose the commit message for a revert of drift in one area

    Args:
        area (str): Area of the tree the revert covers, e.g. 'drivers/video'
        paths (list of str): Files being reverted
        info (DriftInfo): Result from drift_collect()

    Return:
        str: Commit message
    """
    hunks = sum(sum(1 for vdt in info.verdicts.get(path, [])
                    if vdt.state == drift.DRIFT) for path in paths)
    files = '\n'.join(f' - {path}' for path in paths)
    return (
        f'{area}: Drop unintended deltas from upstream\n'
        '\n'
        'These deltas differ from upstream but no downstream commit accounts\n'
        'for them, so they crept in while cherry-picking, most likely through\n'
        'a conflict resolution.  Put the affected hunks back to what upstream\n'
        f'has at {info.base[:12]}\n'
        '\n'
        f'This reverts {hunks} hunk(s) in {len(paths)} file(s):\n'
        f'{files}\n')


# pylint: disable-next=too-many-arguments,too-many-locals,too-many-branches
def drift_revert_area(info, area, paths, branch, msg=None, missing=False,
                      build_cmd=None):
    """Create a branch which reverts the drift in one area of the tree

    Args:
        info (DriftInfo): Result from drift_collect()
        area (str): Area of the tree, e.g. 'drivers/video'
        paths (list of str): Files to revert
        branch (str): Downstream branch to base the revert on
        msg (str): Commit message, or None to describe it as drift
        missing (bool): True to restore files upstream has, rather than
            revert drift hunks
        build_cmd (str): Command to check the reverted tree builds, or None
            to commit without checking

    Return:
        str: Name of the branch created, or None on failure
    """
    name = ('missing-' if missing else 'drift-') + area.replace('/', '-')
    if gitutil.branch_exists(name):
        tout.info(f'Deleting existing branch {name}')
        gitutil.delete_branch(name)
    gitutil.create_branch(name, branch)

    wanted = set(paths)
    fdiffs = [fdiff for fdiff in info.fdiffs if fdiff.path in wanted]
    states = (drift.ABSENT,) if missing else (drift.DRIFT,)
    patch = drift.build_patch(fdiffs, info.verdicts, states)

    if patch:
        with tempfile.NamedTemporaryFile(mode='w', suffix='.patch',
                                         errors='surrogateescape',
                                         delete=False) as fpath:
            fpath.write(patch)
            pname = fpath.name
        try:
            # The patch runs upstream-to-downstream, so reverse it to take the
            # tree back towards upstream.  Whitespace complaints are upstream's
            # to deal with: the point here is to match it exactly
            gitutil.apply_patch(pname, reverse=True, whitespace='nowarn')
        except Exception as exc:  # pylint: disable=broad-except
            what = 'restore' if missing else 'revert drift in'
            tout.error(f'Failed to {what} {area}: {exc}')
            return None
        finally:
            os.unlink(pname)

    # A binary file cannot be reverted hunk by hunk, so take upstream's whole
    binaries = [path for path in paths if path in info.binary]
    if binaries:
        gitutil.checkout_paths(info.base, binaries)

    # Commit these paths and no others, so that anything else in the tree is
    # left where it is
    # Check before committing: a revert can remove something another file
    # still uses, and the identifier check cannot see every such name
    if build_cmd:
        built, errors = drift_build_ok(build_cmd)
        if not built:
            tout.error(f'{area}: the revert does not build, so it is not '
                       'worth a merge request:')
            for line in errors.splitlines():
                tout.error(f'    {line}')
            # Put the working tree back and drop the branch, which has no
            # commit on it yet
            run_git(['reset', '--hard'])
            gitutil.checkout_branch(branch)
            gitutil.delete_branch(name)
            return None

    gitutil.add(paths)
    gitutil.commit_paths(msg or drift_commit_msg(area, paths, info), paths)
    return name


# pylint: disable-next=too-many-locals,too-many-branches,too-many-statements
def do_drift_fix(args, dbs):
    """Revert drift back to upstream, one area of the tree at a time

    Args:
        args (Namespace): Parsed arguments with 'source', 'branch', 'count',
            'paths', 'unambiguous', 'push', 'remote' and 'target' attributes
        dbs (Database): Database instance

    Return:
        int: 0 on success, 1 on failure
    """
    # Branches are created and patches applied below, so anything left lying
    # around in the working tree would be swept along with it
    if gitutil.has_uncommitted_changes():
        tout.error('Working tree has uncommitted changes - commit or stash '
                   'them first')
        return 1

    info = drift_collect(dbs, args.source, args.branch, not args.shallow)
    if not info:
        return 1

    missing = getattr(args, 'missing', False)
    if missing:
        bad = drift_absent_paths(info)
        if not bad:
            tout.info('No files missing from upstream ✓')
            return 0
    else:
        bad = drift_paths(info)
        if not bad:
            tout.info('No drift from upstream ✓')
            return 0

    bad = drift_select(bad, info, getattr(args, 'paths', None),
                       getattr(args, 'unambiguous', False))
    if not bad:
        tout.info('Nothing to fix with the filters given')
        return 0

    areas = drift.group_by_area([path for path, _ in bad])
    todo = list(areas.items())[:args.count]
    if len(areas) > len(todo):
        tout.info(f'{len(areas)} area(s) have drift, fixing {len(todo)} - '
                  'run again for the rest')

    build_cmd = drift_build_cmd(args)
    if build_cmd:
        tout.info(f"Each area is checked with '{build_cmd}'; --no-build "
                  'turns that off.  Passing means the check passed, not that '
                  'the revert is right')
        # If the check cannot pass the tree as it stands, nothing it says
        # about a reverted tree means anything - and every area would decline,
        # which looks like caution rather than a broken command
        tout.info('  checking it passes on the tree as it is...')
        built, errors = drift_build_ok(build_cmd)
        if not built:
            tout.error('That command fails on the unmodified tree, so its '
                       'verdict on a revert would be meaningless:')
            for line in errors.splitlines():
                tout.error(f'    {line}')
            return 1

    orig = gitutil.current_branch()
    ret = 0
    try:
        for area, paths in todo:
            tout.info(f'{area}: reverting {len(paths)} file(s)')
            held = ({} if missing else
                    drift_load_bearing(info, paths, args.branch))
            if held:
                tout.warning(f'{area}: declining {len(held)} file(s) whose '
                             'revert would change or remove something still '
                             'in use:')
                shown = sorted(held.items())[:DECLINE_SHOWN]
                for path, users in shown:
                    name, user, verb = users[0]
                    tout.warning(f'  {path}: {verb} {name}, still used by '
                                 f'{user}')
                if len(held) > len(shown):
                    tout.warning(f'  ... and {len(held) - len(shown)} more '
                                 f'(showing {len(shown)} of {len(held)})')
                paths = [path for path in paths if path not in held]
                if not paths:
                    tout.warning(f'{area}: nothing left to revert, skipping')
                    continue
            if missing:
                sid = dbs.source_get_id(args.source)
                absent = {path for path, _ in drift_absent_paths(info)}
                origin = drift_absent_origin(dbs, sid, args.source, paths)
                partial = drift_absent_partial(origin, absent)
                if partial:
                    tout.warning(f'{area}: declining {len(partial)} file(s) '
                                 'which would apply only part of a commit:')
                    for path in sorted(partial)[:DECLINE_SHOWN]:
                        chash, _, where, others, changed = partial[path]
                        tout.warning(
                            f'  {path} ({where}): applies only part of '
                            f'{chash[:11]} - {len(others)} file(s) still '
                            f'absent and {len(changed)} it also changes')
                        tout.warning(f'    run: pickman pick {chash[:11]}')
                    if len(partial) > DECLINE_SHOWN:
                        tout.warning(f'  ... and {len(partial) - DECLINE_SHOWN}'
                                     f' more (showing {DECLINE_SHOWN} of '
                                     f'{len(partial)})')
                    paths = [path for path in paths if path not in partial]
                    if len(partial) > len(paths):
                        tout.info('  most of these want picking rather than '
                                  'restoring, which is the tool working')
                    if not paths:
                        tout.warning(f'{area}: nothing left to restore, '
                                     'skipping')
                        continue
                reasons = {path: (rec[0], rec[1])
                           for path, rec in origin.items() if path in paths}
                msg = drift_absent_msg(area, paths, info, reasons)
            else:
                msg = drift_commit_msg(area, paths, info)
            name = drift_revert_area(info, area, paths, args.branch, msg,
                                     missing, build_cmd)
            if not name:
                ret = 1
                continue
            if args.push:
                title = (f'{area}: Restore files which upstream has' if missing
                         else f'{area}: Drop unintended deltas from upstream')
                desc = msg
                if not push_mr(args, name, title, desc):
                    ret = 1
            else:
                tout.info(f'  Created branch {name}')
    finally:
        if gitutil.current_branch() != orig:
            gitutil.checkout_branch(orig)

    return ret


def status_backlog(last_commit, source):
    """Count the upstream work not yet brought into the downstream branch

    Args:
        last_commit (str): Last commit cherry-picked from the source
        source (str): Source branch name, e.g. 'us/master'

    Return:
        tuple:
            int: Number of series (first-parent merges) remaining
            int: Number of non-merge commits remaining
    """
    rng = f'{last_commit}..{source}'
    merges = gitutil.count_revs(None, rng, first_parent=True, merges=True)
    commits = gitutil.count_revs(None, rng, merges=False)
    return merges, commits


def status_parked(dbs, source_id):
    """Get the commits parked as conflicts for a source, oldest first

    A parked conflict is a commit which pickman tried to cherry-pick, hit a
    conflict on and moved past.  Its change is then missing from the
    downstream branch until something else applies it - silently, with no
    merge request and no CI failure - so the backlog is worth surfacing.  The
    commit id stands in for age, since there is no timestamp: a lower id was
    added earlier.

    Args:
        dbs (Database): Database instance
        source_id (int): Source branch id

    Return:
        list of tuple: (id, chash, subject) for each parked commit, oldest
            (lowest id) first
    """
    recs = dbs.commit_get_by_source(source_id, 'conflict')
    return sorted((rec[0], rec[1], rec[4]) for rec in recs)


def parked_overlap(parked, commits):
    """Find where commits about to be applied touch parked-conflict files

    A parked commit's change is missing from the tree.  A later commit which
    touches the same file may be adjusting something the parked one was
    supposed to add, so applying it alone leaves the tree with half the work -
    it still builds, CI stays green, and nothing says a thing.  That is how a
    board came to hang for months on a missing linker-script alignment.

    Args:
        parked (list of tuple): (id, hash, subject) from status_parked()
        commits (list of CommitInfo): Commits about to be applied

    Return:
        dict: Maps each shared path to the list of (hash, subject) parked
            commits which touch it, for the paths the new commits also touch
    """
    if not parked or not commits:
        return {}

    parked_paths = {}
    for _, chash, subj in parked:
        for _, files in gitutil.log_commits_with_files(f'{chash}^!'):
            for path in files:
                parked_paths.setdefault(path, []).append((chash, subj))

    shared = {}
    for commit in commits:
        for _, files in gitutil.log_commits_with_files(f'{commit.hash}^!'):
            for path in files:
                if path in parked_paths:
                    shared[path] = parked_paths[path]
    return shared


def parked_overlap_note(shared):
    """Describe an overlap with parked commits, for a merge request

    Args:
        shared (dict): Result from parked_overlap()

    Return:
        str: Text to add to the merge request, or '' if there is no overlap
    """
    if not shared:
        return ''
    seen = {}
    for path, owners in sorted(shared.items()):
        for chash, subj in owners:
            seen.setdefault((chash, subj), []).append(path)

    out = ['### Touches files which parked conflicts also touch', '',
           'These commits were parked as conflicts, so their change is '
           'missing from the tree. A commit here may be adjusting something '
           'one of them was meant to add:', '']
    for (chash, subj), paths in sorted(seen.items(), key=lambda it: it[0][1]):
        out.append(f'- `{chash[:11]}` {subj}')
        for path in sorted(paths)[:5]:
            out.append(f'  - {path}')
        if len(paths) > 5:
            out.append(f'  - ... and {len(paths) - 5} more')
    return '\n'.join(out) + '\n'


def parked_retry(dbs, parked, branch, dry_run=False):
    """Try each parked commit again against the current tree

    A conflict is often only true of the tree as it stood at the time: once
    the change it clashed with has itself been picked, the commit applies
    cleanly.  Retrying costs nothing and lands work which would otherwise sit
    missing for good.

    Merges are left alone, since a merge carries no change of its own.

    Args:
        dbs (Database): Database instance
        parked (list of tuple): (id, hash, subject) from status_parked()
        branch (str): Branch to base the retry on, e.g. 'ci/master'
        dry_run (bool): True to report what would apply, keeping nothing

    Return:
        tuple:
            list of tuple: The (id, hash, subject) which now apply
            list of tuple: Those which still conflict
            str: Name of the branch holding them, or None if nothing applied
                or this is a dry run
    """
    todo = [rec for rec in parked if not rec[2].startswith('Merge')]
    skipped = len(parked) - len(todo)
    if skipped:
        tout.info(f'  ignoring {skipped} merge(s), which carry no change')

    name = 'parked-retry'
    if gitutil.branch_exists(name):
        gitutil.delete_branch(name)
    gitutil.create_branch(name, branch)

    applied = []
    still = []
    for rec in todo:
        chash = rec[1]
        if not gitutil.ref_exists(f'{chash}^{{commit}}'):
            tout.warning(f'  #{rec[0]} {chash[:11]}: gone from the repo')
            still.append(rec)
            continue
        try:
            run_git(['cherry-pick', '-x', chash])
            applied.append(rec)
        except Exception:  # pylint: disable=broad-except
            # Still conflicts, so put it back and move on to the next
            _abort_in_progress()
            still.append(rec)

    if dry_run or not applied:
        gitutil.checkout_branch(branch)
        gitutil.delete_branch(name)
        return applied, still, None

    for rec in applied:
        dbs.commit_set_status(rec[1], 'applied')
    dbs.commit()
    return applied, still, name


# pylint: disable-next=too-many-branches,too-many-locals
def do_parked(args, dbs):
    """Report the commits parked as conflicts, and optionally retry them

    Args:
        args (Namespace): Parsed arguments with 'source', 'branch', 'retry',
            'dry_run', 'push', 'remote' and 'target'
        dbs (Database): Database instance

    Return:
        int: 0 if nothing is parked, 1 if anything still is, so that this can
            be used as a check
    """
    source = args.source
    source_id = dbs.source_get_id(source)
    if not source_id:
        tout.error(f"Source '{source}' not found - use 'pickman add-source'")
        return 1

    parked = status_parked(dbs, source_id)
    if not parked:
        tout.info(f'No parked conflicts for {source} ✓')
        return 0

    merges = sum(1 for _, _, subj in parked if subj.startswith('Merge'))
    tout.info(f'{len(parked)} parked conflict(s) for {source}, {merges} of '
              'them merges:')
    for cid, chash, subj in parked:
        tout.info(f'  #{cid} {chash[:11]} {subj[:60]}')

    if not args.retry:
        tout.info('')
        tout.info("Each of these is missing from the tree; '--retry' tries "
                  'them again')
        return 1

    # A retry checks out a branch and cherry-picks onto it, so anything left
    # lying around in the working tree would be swept along with it
    if gitutil.has_uncommitted_changes():
        tout.error('Working tree has uncommitted changes - commit or stash '
                   'them first')
        return 1

    tout.info('')
    tout.info(f'Retrying against {args.branch}...')
    orig = gitutil.current_branch()
    try:
        applied, still, name = parked_retry(dbs, parked, args.branch,
                                            args.dry_run)
    finally:
        if gitutil.current_branch() != orig and gitutil.branch_exists(orig):
            gitutil.checkout_branch(orig)

    tout.info('')
    tout.info(f'{len(applied)} now apply cleanly, {len(still)} still conflict')
    for cid, _, subj in applied:
        tout.info(f'  applies: #{cid} {subj[:60]}')

    if args.dry_run:
        tout.info('')
        tout.info('Dry run, so nothing was kept')
    elif name:
        tout.info('')
        if args.push:
            title = f'[pickman] Retry {len(applied)} parked conflict(s)'
            desc = ('These commits were parked as conflicts and apply '
                    'cleanly now that the tree has moved on.\n\n' +
                    '\n'.join(f'- {rec[1][:11]} {rec[2]}' for rec in applied))
            if not push_mr(args, name, title, desc):
                return 1
        else:
            tout.info(f'Created branch {name}')

    return 1 if still else 0


def parked_warning(source, parked):
    """Build a one-line warning about parked conflicts

    Args:
        source (str): Source branch name
        parked (list): Result from status_parked()

    Return:
        str: A short warning, or None if nothing is parked
    """
    if not parked:
        return None
    merges = sum(1 for _, _, subj in parked if subj.startswith('Merge'))
    return (f'{len(parked)} parked conflict(s) for {source} ({merges} '
            f"merges) not landed - run 'pickman status {source}' to review")


def do_status(args, dbs):  # pylint: disable=too-many-locals
    """Summarise the downstream branch against the upstream source

    Reports the upstream series not yet brought in, the commits parked as
    conflicts and so silently missing, and the drift in the downstream branch
    which should be resynced to upstream.  The refs are read as they are; run
    a fetch first for an up-to-date picture.

    Args:
        args (Namespace): Parsed arguments with 'source', 'branch', 'shallow'
            and 'upstream'
        dbs (Database): Database instance

    Return:
        int: 0 on success, 1 if the source is unknown
    """
    source = args.source
    deep = not args.shallow

    try:
        branch, base = resolve_compare(args)
    except ValueError as exc:
        tout.error(str(exc))
        return 1

    # An override supplies the base directly; otherwise use the tracked
    # position.  Everything below compares against this same base
    last_commit = base or dbs.source_get(source)
    if not last_commit:
        tout.error(f"Source '{source}' not found - use 'pickman add-source'")
        return 1

    tout.info(f'Pickman status: {branch} vs {source}')
    tout.info('')

    # Forward backlog: what upstream has that has not been cherry-picked yet
    merges, commits = status_backlog(last_commit, source)
    pos = gitutil.commit_summary(last_commit)
    tip = gitutil.commit_summary(source)
    tout.info('Upstream backlog (to bring in):')
    tout.info(f'  source position: {pos}')
    tout.info(f'  upstream tip:    {tip}')
    tout.info(f'  {merges} series remaining ({commits} non-merge commits)')
    tout.info('')

    # Parked conflicts: commits pickman tried, conflicted on and moved past.
    # Each one's change is missing until something applies it, so make the
    # backlog loud rather than let it sit silently in the database.  This is
    # shown before the drift scan, which can take minutes
    source_id = dbs.source_get_id(source)
    parked = status_parked(dbs, source_id) if source_id else []
    if parked:
        parked_merges = sum(1 for _, _, subj in parked
                            if subj.startswith('Merge'))
        tout.info('Parked conflicts (tried, not landed):')
        tout.info(f'  {len(parked)} commit(s) parked as conflict, '
                  f'{parked_merges} of them merges')
        tout.info('  each parked change is silently missing until something '
                  'applies it')
        tout.info('  oldest still outstanding:')
        for cid, chash, subj in parked[:5]:
            tout.info(f'    #{cid} {chash[:11]} {subj[:55]}')
        if len(parked) > 5:
            tout.info(f'    ... and {len(parked) - 5} more')
        tout.info('')

    # Cleanup backlog: drift in the downstream branch, to resync to upstream
    info = drift_collect(dbs, source, branch, deep, base=last_commit)
    if not info:
        return 1
    states = drift_state_counts(info)
    bad = drift_paths(info)
    depth = 'deep' if deep else 'shallow estimate, drop -s for the full count'
    tout.info('Drift (to resync to upstream):')
    tout.info(f'  {len(info.fdiffs)} file(s) differ from upstream '
              f'{info.base[:12]}')
    tout.info(f'  {states[drift.DRIFT]} spurious hunk(s) in {len(bad)} '
              f'file(s) ({depth})')
    tout.info(f'  {drift_percent(states):.0f}% of the divergence is drift')
    if bad:
        tout.info(f"  run 'pickman drift {source}' for detail")

    return 0


def _check_subtree_merge(merge_hash):
    """Check if a merge commit is a subtree merge.

    Returns:
        tuple: (name, tag) where name is the subtree name (or None for
            unknown paths), or None if not a subtree merge
    """
    subject = run_git(['log', '-1', '--format=%s', merge_hash])
    match = RE_SUBTREE_MERGE.match(subject)
    if not match:
        return None
    tag = match.group(1)
    path = match.group(2)
    return SUBTREE_NAMES.get(path), tag


def find_unprocessed_commits(dbs, last_commit, source, merge_hashes):
    """Find the first merge with unprocessed commits

    Walks through the merge hashes in order, looking for one that has
    commits not yet tracked in the database. Decomposes mega-merges
    (merges containing sub-merges) into individual batches.

    Args:
        dbs (Database): Database instance
        last_commit (str): Hash of the last cherry-picked commit
        source (str): Source branch name
        merge_hashes (list): List of merge commit hashes to check

    Returns:
        NextCommitsInfo: Info about the next commits to process
    """
    prev_commit = last_commit
    skipped_merges = False
    for merge_hash in merge_hashes:
        # Check for subtree merge (e.g. dts/upstream update)
        result = _check_subtree_merge(merge_hash)
        if result is not None:
            name, tag = result
            if name:
                return NextCommitsInfo([], True, merge_hash,
                                       (name, tag))
            # Unknown subtree path - skip past it
            prev_commit = merge_hash
            skipped_merges = True
            continue

        # Check for mega-merge (contains sub-merges)
        sub_merges = detect_sub_merges(merge_hash)
        if sub_merges:
            commits, advance_to = decompose_mega_merge(
                dbs, prev_commit, merge_hash, sub_merges)
            if commits:
                return NextCommitsInfo(commits, True, advance_to)
            # All sub-merges done, skip past this mega-merge
            prev_commit = merge_hash
            skipped_merges = True
            continue

        # Get all commits from prev_commit to this merge
        log_output = run_git([
            'log', '--reverse', '--format=%H|%h|%an|%s|%P',
            f'{prev_commit}..{merge_hash}'
        ])

        if not log_output:
            prev_commit = merge_hash
            continue

        # Parse commits, filtering out those already in database
        all_commits = parse_log_output(log_output, has_parents=True)
        commits = [c for c in all_commits
                   if not dbs.commit_get(c.hash)]

        if commits:
            return NextCommitsInfo(commits, True, merge_hash)

        # All commits in this merge are processed, skip to next
        prev_commit = merge_hash
        skipped_merges = True

    # No merges with unprocessed commits, check remaining commits
    log_output = run_git([
        'log', '--reverse', '--format=%H|%h|%an|%s|%P',
        f'{prev_commit}..{source}'
    ])

    if not log_output:
        # If we skipped merges, advance past them
        advance_to = prev_commit if skipped_merges else None
        return NextCommitsInfo([], False, advance_to)

    all_commits = parse_log_output(log_output, has_parents=True)
    commits = [c for c in all_commits if not dbs.commit_get(c.hash)]

    return NextCommitsInfo(commits, False, None)


def get_next_commits(dbs, source):
    """Get the next set of commits to cherry-pick from a source

    Finds commits between the last cherry-picked commit and the next merge
    commit on the first-parent (mainline) chain of the source branch.
    Skips merges whose commits are already tracked in the database (from
    pending MRs). Decomposes mega-merges (merges containing sub-merges)
    into individual sub-merge batches.

    Args:
        dbs (Database): Database instance
        source (str): Source branch name

    Returns:
        tuple: (NextCommitsInfo, error_msg) where error_msg is None
            on success
    """
    # Get the last cherry-picked commit from database
    last_commit = dbs.source_get(source)

    if not last_commit:
        return None, f"Source '{source}' not found in database"

    # Get all first-parent commits to find merges
    fp_output = run_git([
        'log', '--reverse', '--first-parent', '--format=%H|%h|%an|%s|%P',
        f'{last_commit}..{source}'
    ])

    if not fp_output:
        return NextCommitsInfo([], False, None), None

    # Build list of merge hashes on the first-parent chain
    merge_hashes = []
    for line in fp_output.split('\n'):
        if not line:
            continue
        parts = line.split('|')
        parents = parts[-1].split()
        if len(parents) > 1:
            merge_hashes.append(parts[0])

    return find_unprocessed_commits(
        dbs, last_commit, source, merge_hashes), None


def get_commits_for_pick(commit_spec):
    """Get commits to cherry-pick from a commit specification

    Supports two formats:
    - Commit range: 'hash1..hash2' returns all commits in that range
    - Merge commit: Returns all non-merge commits that were part of the merge

    Args:
        commit_spec (str): Either 'hash1..hash2' for a range, or a single
            hash (which if it's a merge, gets all its child commits)

    Returns:
        tuple: (list of CommitInfo, error_message) - error_message is None
            on success
    """
    commits = None
    err = None

    if '..' in commit_spec:
        # Commit range format: hash1..hash2
        try:
            log_output = run_git([
                'log', '--reverse', '--format=%H|%h|%an|%s',
                commit_spec
            ])
            if log_output:
                commits = parse_log_output(log_output)
            else:
                commits, err = [], f"No commits found in range: {commit_spec}"
        except Exception:  # pylint: disable=broad-except
            err = f"Invalid commit range: {commit_spec}"
    else:
        # Single commit - check if it's a merge
        try:
            parents = run_git(['rev-parse', f'{commit_spec}^@'])
            parent_list = parents.strip().split('\n') if parents.strip() else []

            if len(parent_list) < 2:
                # Not a merge - return just this commit
                log_output = run_git(['log', '-1', '--format=%H|%h|%an|%s',
                                      commit_spec])
                commits = parse_log_output(log_output)
            else:
                # It's a merge - get commits from the merged branch
                # parent_list[0] is main branch, parent_list[1] is merged branch
                log_output = run_git([
                    'log', '--reverse', '--format=%H|%h|%an|%s',
                    f'^{parent_list[0]}', parent_list[1]
                ])
                if log_output:
                    commits = parse_log_output(log_output)
                else:
                    commits = []
                    err = f"No commits found in merge: {commit_spec}"
        except Exception:  # pylint: disable=broad-except
            err = f"Invalid commit: {commit_spec}"

    return commits, err


def detect_sub_merges(merge_hash):
    """Check if a merge commit contains sub-merges

    Examines the second parent's first-parent chain to find merge commits
    (sub-merges) within a larger merge.

    Args:
        merge_hash (str): Hash of the merge commit to check

    Returns:
        list: List of sub-merge hashes in chronological order, or empty
            list if not a merge or has no sub-merges
    """
    # Get parents of the merge
    try:
        parents = run_git(['rev-parse', f'{merge_hash}^@'])
    except command.CommandExc:
        return []

    parent_list = parents.strip().split('\n')
    if len(parent_list) < 2:
        return []

    first_parent = parent_list[0]
    second_parent = parent_list[1]

    # Find merges on the second parent's first-parent chain
    try:
        out = run_git([
            'log', '--reverse', '--first-parent', '--merges',
            '--format=%H', f'^{first_parent}', second_parent
        ])
    except command.CommandExc:
        return []

    if not out:
        return []

    return [line for line in out.split('\n') if line]


def _mega_preadd(dbs, merge_hash):
    """Pre-add a mega-merge commit to the database as 'skipped'.

    This prevents the mega-merge from appearing as an orphan commit.
    Does nothing if the commit already exists in the database.
    """
    if dbs.commit_get(merge_hash):
        return

    source_id = None
    sources = dbs.source_get_all()
    if sources:
        source_id = dbs.source_get_id(sources[0][0])
    if source_id:
        info = run_git(['log', '-1', '--format=%s|%an', merge_hash])
        parts = info.split('|', 1)
        subject = parts[0]
        author = parts[1] if len(parts) > 1 else ''
        dbs.commit_add(merge_hash, source_id, subject, author,
                       status='skipped')
        dbs.commit()


def _mega_get_batch(dbs, exclude_ref, include_ref):
    """Fetch a batch of unprocessed commits between two refs.

    Runs git log for the range ^exclude_ref include_ref, parses the
    output and filters out commits already in the database.

    Returns:
        list: CommitInfo tuples for unprocessed commits, may be empty
    """
    log_output = run_git([
        'log', '--reverse', '--format=%H|%h|%an|%s|%P',
        f'^{exclude_ref}', include_ref
    ])
    if not log_output:
        return []

    all_commits = parse_log_output(log_output, has_parents=True)
    return [c for c in all_commits if not dbs.commit_get(c.hash)]


def decompose_mega_merge(dbs, prev_commit, merge_hash, sub_merges):
    """Return the next unprocessed batch from a mega-merge

    Handles three phases:
    1. Mainline commits before the merge (prev_commit..merge^1)
    2. Sub-merge batches (one at a time, skipping processed ones)
    3. Remainder commits after the last sub-merge

    Pre-adds the mega-merge commit itself to DB as 'skipped' so it does
    not appear as an orphan commit.

    Args:
        dbs (Database): Database instance
        prev_commit (str): Hash of the last processed commit
        merge_hash (str): Hash of the mega-merge commit
        sub_merges (list): List of sub-merge hashes in chronological order

    Returns:
        tuple: (commits, advance_to) where:
            commits: list of CommitInfo tuples for the next batch
            advance_to: hash to advance source to, or None to stay put
    """
    parents = run_git(['rev-parse', f'{merge_hash}^@']).strip().split('\n')
    first_parent = parents[0]
    second_parent = parents[1]

    _mega_preadd(dbs, merge_hash)

    # Phase 1: mainline commits before the merge
    commits = _mega_get_batch(dbs, prev_commit, first_parent)
    if commits:
        return commits, first_parent

    # Phase 2: sub-merge batches
    prev_sub = first_parent
    for sub_hash in sub_merges:
        commits = _mega_get_batch(dbs, prev_sub, sub_hash)
        if commits:
            return commits, None
        prev_sub = sub_hash

    # Phase 3: remainder after the last sub-merge
    last_sub = sub_merges[-1] if sub_merges else first_parent
    commits = _mega_get_batch(dbs, last_sub, second_parent)
    if commits:
        return commits, None

    return [], None


def do_next_set(args, dbs):
    """Show the next set of commits to cherry-pick from a source

    Args:
        args (Namespace): Parsed arguments with 'source' attribute
        dbs (Database): Database instance

    Returns:
        int: 0 on success, 1 if source not found
    """
    source = args.source
    info, err = get_next_commits(dbs, source)

    if err:
        tout.error(err)
        return 1

    if not info.commits:
        tout.info('No new commits to cherry-pick')
        return 0

    if info.merge_found:
        tout.info(f'Next set from {source} '
                  f'({len(info.commits)} commits):')
    else:
        tout.info(f'Remaining commits from {source} '
                  f'({len(info.commits)} commits, no merge found):')

    for commit in info.commits:
        tout.info(f'  {commit.chash} {commit.subject}')

    return 0


def _next_fetch_merges(last_commit, source, count):
    """Fetch the next merge commits from a source.

    Returns:
        list: (hash, short_hash, subject) tuples, up to count entries
    """
    out = run_git([
        'log', '--reverse', '--first-parent', '--merges',
        '--format=%H|%h|%s',
        f'{last_commit}..{source}'
    ])

    if not out:
        return []

    merges = []
    for line in out.split('\n'):
        if not line:
            continue
        parts = line.split('|', 2)
        commit_hash = parts[0]
        chash = parts[1]
        subject = parts[2] if len(parts) > 2 else ''
        merges.append((commit_hash, chash, subject))
        if len(merges) >= count:
            break

    return merges


def _next_build_display(merges):
    """Build display list, expanding mega-merges into sub-merges.

    Each entry is (chash, subject, is_mega, sub_list) where sub_list
    is a list of (chash, subject) for mega-merge sub-merges.

    Returns:
        tuple: (display_list, total_sub_count)
    """
    display = []
    total_sub = 0
    for commit_hash, chash, subject in merges:
        sub_merges = detect_sub_merges(commit_hash)
        if sub_merges:
            sub_list = []
            for sub_hash in sub_merges:
                try:
                    info = run_git(
                        ['log', '-1', '--format=%h|%s', sub_hash])
                    parts = info.strip().split('|', 1)
                    sub_chash = parts[0]
                    sub_subject = parts[1] if len(parts) > 1 else ''
                except Exception:  # pylint: disable=broad-except
                    sub_chash = sub_hash[:11]
                    sub_subject = '(unknown)'
                sub_list.append((sub_chash, sub_subject))
            display.append((chash, subject, True, sub_list))
            total_sub += len(sub_list)
        else:
            display.append((chash, subject, False, None))

    return display, total_sub


def _next_show_merges(source, merges, display, total_sub):
    """Display the next-merges listing."""
    n_items = total_sub + len(merges) - len(
        [d for d in display if d[2]])
    tout.info(f'Next merges from {source} '
              f'({n_items} from {len(merges)} first-parent):')
    idx = 1
    for chash, subject, is_mega, sub_list in display:
        if is_mega:
            tout.info(f'  {chash} {subject} '
                      f'({len(sub_list)} sub-merges):')
            for sub_chash, sub_subject in sub_list:
                tout.info(f'    {idx}. {sub_chash} {sub_subject}')
                idx += 1
        else:
            tout.info(f'  {idx}. {chash} {subject}')
            idx += 1


def do_next_merges(args, dbs):
    """Show the next N merges to be applied from a source

    Args:
        args (Namespace): Parsed arguments with 'source' and 'count' attributes
        dbs (Database): Database instance

    Returns:
        int: 0 on success, 1 if source not found
    """
    source = args.source
    count = args.count

    last_commit = dbs.source_get(source)
    if not last_commit:
        tout.error(f"Source '{source}' not found in database")
        return 1

    merges = _next_fetch_merges(last_commit, source, count)
    if not merges:
        tout.info('No merges remaining')
        return 0

    display, total_sub = _next_build_display(merges)

    _next_show_merges(source, merges, display, total_sub)

    return 0


def do_count_merges(args, dbs):
    """Count total remaining merges to be applied from a source

    Args:
        args (Namespace): Parsed arguments with 'source' attribute
        dbs (Database): Database instance

    Returns:
        int: 0 on success, 1 if source not found
    """
    source = args.source

    # Get the last cherry-picked commit from database
    last_commit = dbs.source_get(source)

    if not last_commit:
        tout.error(f"Source '{source}' not found in database")
        return 1

    # Count merge commits on the first-parent chain
    fp_output = run_git([
        'log', '--first-parent', '--merges', '--oneline',
        f'{last_commit}..{source}'
    ])

    if not fp_output:
        tout.info('0 merges remaining')
        return 0

    count = len([line for line in fp_output.split('\n') if line])
    tout.info(f'{count} merges remaining from {source}')

    return 0


HISTORY_FILE = '.pickman-history'

# Tag added to MR title when skipped
SKIPPED_TAG = '[skipped]'


def parse_instruction(body):
    """Parse a pickman instruction from a comment body

    Recognizes instructions in these formats:
    - pickman <instruction>
    - pickman: <instruction>
    - @pickman <instruction>
    - @pickman: <instruction>

    Args:
        body (str): Comment body text

    Returns:
        str: The instruction (e.g., 'skip', 'unskip'), or None if not found
    """
    # Pattern matches: optional @, 'pickman', optional colon, then the command
    pattern = r'@?pickman:?\s+(\w+)'
    match = re.search(pattern, body.lower())
    if match:
        return match.group(1)
    return None


def has_instruction(body, instruction):
    """Check if a comment body contains a specific pickman instruction

    Args:
        body (str): Comment body text
        instruction (str): Instruction to check for (e.g., 'skip', 'unskip')

    Returns:
        bool: True if the comment contains the specified instruction
    """
    return parse_instruction(body) == instruction


def handle_unskip_comments(remote, mr_iid, title, unresolved, dbs):
    """Handle unskip comments on an MR

    Args:
        remote (str): Remote name
        mr_iid (int): Merge request IID
        title (str): Current MR title
        unresolved (list): List of unresolved comments
        dbs (Database): Database instance

    Returns:
        tuple: (handled, new_unresolved) where handled is True if unskip was
            processed and new_unresolved is the filtered comment list
    """
    unskip_comments = [c for c in unresolved
                       if has_instruction(c.body, 'unskip')]
    if not unskip_comments:
        return False, unresolved

    tout.info(f'MR !{mr_iid} has unskip request')

    # Update MR title to remove [skipped] tag
    if SKIPPED_TAG in title:
        new_title = title.replace(f'{SKIPPED_TAG} ', '')
        new_title = new_title.replace(SKIPPED_TAG, '')
        gitlab_api.update_mr_title(remote, mr_iid, new_title)
        tout.info(f'MR !{mr_iid} unskipped, will resume processing')

    # Mark unskip comments as processed
    for comment in unskip_comments:
        dbs.comment_mark_processed(mr_iid, comment.id)
    dbs.commit()

    # Reply to confirm the unskip
    gitlab_api.reply_to_mr(
        remote, mr_iid,
        'MR unskipped. Processing will resume on next poll.'
    )

    # Remove unskip comments from unresolved list for further processing
    new_unresolved = [c for c in unresolved
                      if not has_instruction(c.body, 'unskip')]
    return True, new_unresolved


def handle_skip_comments(remote, mr_iid, title, unresolved, dbs):
    """Handle skip comments on an MR

    Args:
        remote (str): Remote name
        mr_iid (int): Merge request IID
        title (str): Current MR title
        unresolved (list): List of unresolved comments
        dbs (Database): Database instance

    Returns:
        bool: True if skip was processed
    """
    skip_comments = [c for c in unresolved
                     if has_instruction(c.body, 'skip')]
    if not skip_comments:
        return False

    tout.info(f'MR !{mr_iid} has skip request, marking as skipped')

    # Update MR title to add [skipped] tag
    if SKIPPED_TAG not in title:
        new_title = f'{SKIPPED_TAG} {title}'
        gitlab_api.update_mr_title(remote, mr_iid, new_title)

    # Mark skip comments as processed
    for comment in skip_comments:
        dbs.comment_mark_processed(mr_iid, comment.id)
    dbs.commit()

    # Reply to confirm the skip
    gitlab_api.reply_to_mr(
        remote, mr_iid,
        'MR marked as skipped. Use `pickman unskip` or manually '
        'remove [skipped] from the title to resume processing.'
    )
    return True


def format_history(source, commits, branch_name):
    """Format a summary of the cherry-pick operation

    Args:
        source (str): Source branch name
        commits (list): list of CommitInfo tuples
        branch_name (str): Name of the cherry-pick branch

    Returns:
        str: Formatted summary text
    """
    commit_list = '\n'.join(
        f'- {c.chash} {c.subject}'
        for c in commits
    )

    return f"""## {date.today()}: {source}

Branch: {branch_name}

Commits:
{commit_list}"""


def get_history(fname, source, commits, branch_name, conv_log):
    """Read, update and write history file for a cherry-pick operation

    Args:
        fname (str): History filename to read/write
        source (str): Source branch name
        commits (list): list of CommitInfo tuples
        branch_name (str): Name of the cherry-pick branch
        conv_log (str): The agent's conversation output

    Returns:
        tuple: (content, commit_msg) where content is the updated history
            and commit_msg is the git commit message
    """
    summary = format_history(source, commits, branch_name)
    entry = f"""{summary}

### Conversation log
{conv_log}

---

"""

    # Read existing content
    existing = ''
    if os.path.exists(fname):
        existing = tools.read_file(fname, binary=False)
        # Remove existing entry for this branch (from ## header to ---)
        pattern = rf'## [^\n]+\n\nBranch: {re.escape(branch_name)}\n.*?---\n\n'
        existing = re.sub(pattern, '', existing, flags=re.DOTALL)

    content = existing + entry

    # Write updated history file
    tools.write_file(fname, content, binary=False)

    # Generate commit message
    commit_msg = (f'pickman: Record cherry-pick of {len(commits)} commits '
                  f'from {source}\n\n')
    commit_msg += '\n'.join(f'- {c.chash} {c.subject}' for c in commits)

    return content, commit_msg


def write_history(source, commits, branch_name, conv_log):
    """Write an entry to the pickman history file and commit it

    Args:
        source (str): Source branch name
        commits (list): list of CommitInfo tuples
        branch_name (str): Name of the cherry-pick branch
        conv_log (str): The agent's conversation output
    """
    _, commit_msg = get_history(HISTORY_FILE, source, commits, branch_name,
                                conv_log)

    # Commit the history file (use -f in case .gitignore patterns match)
    run_git(['add', '-f', HISTORY_FILE])
    run_git(['commit', '-m', commit_msg])

    tout.info(f'Updated {HISTORY_FILE}')


def push_mr(args, branch_name, title, description):
    """Push branch and create merge request

    Args:
        args (Namespace): Parsed arguments with 'remote' and 'target'
        branch_name (str): Branch name to push
        title (str): MR title
        description (str): MR description

    Returns:
        bool: True on success, False on failure
    """
    mr_url = gitlab_api.push_and_create_mr(
        args.remote, branch_name, args.target, title, description
    )
    return bool(mr_url)


def _is_merge_in_progress():
    """Check if a git merge is currently in progress.

    Returns:
        bool: True if MERGE_HEAD exists (merge in progress), False otherwise
    """
    try:
        run_git(['rev-parse', '--verify', 'MERGE_HEAD'])
        return True
    except Exception:  # pylint: disable=broad-except
        return False


# git operations which leave a *_HEAD ref while unfinished, mapped to the
# command which abandons each one
IN_PROGRESS_OPS = {
    'CHERRY_PICK_HEAD': ['cherry-pick', '--abort'],
    'MERGE_HEAD': ['merge', '--abort'],
    'REVERT_HEAD': ['revert', '--abort'],
}


def _has_unresolved_conflict():
    """Check whether the working tree has an unresolved conflict

    The cherry-pick agent can return without finishing the job - for example
    if it is blocked partway through - leaving a cherry-pick, merge or revert
    in progress, or unmerged paths in the index.  The git state is the ground
    truth for this, whatever the agent reports.

    Return:
        bool: True if an operation is in progress or a path is unmerged
    """
    for ref in IN_PROGRESS_OPS:
        res = command.run_one('git', 'rev-parse', '--verify', '--quiet', ref,
                              capture=True, raise_on_error=False)
        if not res.return_code:
            return True
    return bool(run_git(['ls-files', '--unmerged']))


def _abort_in_progress():
    """Abort any cherry-pick, merge or revert in progress

    This leaves the working tree clean, so that pickman can move off the
    branch.  Errors are ignored, since this is a best-effort cleanup.
    """
    for ref, git_args in IN_PROGRESS_OPS.items():
        res = command.run_one('git', 'rev-parse', '--verify', '--quiet', ref,
                              capture=True, raise_on_error=False)
        if not res.return_code:
            try:
                run_git(git_args)
            except Exception:  # pylint: disable=broad-except
                pass


def _subtree_run_update(name, tag):
    """Run update-subtree.sh to pull a subtree update.

    On failure, checks whether a merge is in progress (indicating
    conflicts). If so, returns SUBTREE_CONFLICT with the merge state
    intact so the caller can invoke the agent. Otherwise aborts any
    in-progress merge and returns SUBTREE_FAIL.

    Returns:
        int: SUBTREE_OK on success, SUBTREE_CONFLICT on merge conflicts,
            SUBTREE_FAIL on other failures
    """
    try:
        result = command.run_one(
            './tools/update-subtree.sh', 'pull', name, tag,
            capture=True, raise_on_error=False)
        if result.stdout:
            tout.info(result.stdout)
        if result.return_code:
            tout.error(f'Subtree update failed (exit code '
                        f'{result.return_code})')
            if result.stderr:
                tout.error(result.stderr)
            if _is_merge_in_progress():
                return SUBTREE_CONFLICT
            try:
                run_git(['merge', '--abort'])
            except Exception:  # pylint: disable=broad-except
                pass
            return SUBTREE_FAIL
    except Exception as exc:  # pylint: disable=broad-except
        tout.error(f'Subtree update failed: {exc}')
        return SUBTREE_FAIL

    return SUBTREE_OK


def _subtree_record(dbs, source, squash_hash, merge_hash):
    """Mark subtree commits as applied and advance the source position."""
    source_id = dbs.source_get_id(source)
    for commit_hash in [squash_hash, merge_hash]:
        if not dbs.commit_get(commit_hash):
            info = run_git(['log', '-1', '--format=%s|%an', commit_hash])
            parts = info.split('|', 1)
            subj = parts[0]
            auth = parts[1] if len(parts) > 1 else ''
            dbs.commit_add(commit_hash, source_id, subj, auth,
                           status='applied')
    dbs.commit()

    dbs.source_set(source, merge_hash)
    dbs.commit()
    tout.info(f"Advanced source '{source}' past subtree merge "
              f'{merge_hash[:12]}')


def apply_subtree_update(dbs, source, name, tag, merge_hash, remote,  # pylint: disable=too-many-arguments,too-many-locals
                         target, push=True):
    """Apply a subtree update on a branch and create a merge request

    Runs tools/update-subtree.sh to pull the subtree update on a new
    branch (cherry-<hash>), then optionally pushes and creates a merge
    request to the target branch.

    Args:
        dbs (Database): Database instance
        source (str): Source branch name
        name (str): Subtree name ('dts', 'mbedtls', 'lwip')
        tag (str): Tag to pull (e.g. 'v6.15-dts')
        merge_hash (str): Hash of the subtree merge commit to advance past
        remote (str): Git remote name (e.g. 'ci')
        target (str): Target branch name (e.g. 'master')
        push (bool): Whether to push the result to the remote

    Returns:
        int: 0 on success, 1 on failure
    """
    tout.info(f'Applying subtree update: {name} -> {tag}')

    # Get the squash commit (second parent of the merge)
    parents = run_git(['rev-parse', f'{merge_hash}^@']).split()
    if len(parents) < 2:
        tout.error(f'Subtree merge {merge_hash[:12]} has no second parent')
        return 1
    squash_hash = parents[1]

    branch_name = f'cherry-{merge_hash[:11]}'

    # Create a new branch from the target for this subtree update
    try:
        run_git(['checkout', target])
    except command.CommandExc:
        # Bare name may be ambiguous when multiple remotes have it
        try:
            run_git(['checkout', '-b', target,
                     f'{remote}/{target}'])
        except command.CommandExc:
            tout.error(f'Could not checkout {target}')
            return 1

    # Delete the branch if it already exists, then create it
    if run_git(['branch', '--list', branch_name]).strip():
        tout.info(f'Deleting existing branch {branch_name}')
        run_git(['branch', '-D', branch_name])
    run_git(['checkout', '-b', branch_name])

    ret = _subtree_run_update(name, tag)
    if ret == SUBTREE_FAIL:
        return 1
    if ret == SUBTREE_CONFLICT:
        # Resolve via reverse lookup of subtree path
        subtree_path = next(
            (p for p, n in SUBTREE_NAMES.items() if n == name), None)
        tout.info('Merge conflicts detected, invoking agent...')
        success, _ = agent.resolve_subtree_conflicts(
            name, tag, subtree_path)
        if not success:
            tout.error('Agent could not resolve subtree conflicts')
            try:
                run_git(['merge', '--abort'])
            except command.CommandExc:
                pass
            return 1

    if push:
        title = f'Subtree update: {name} -> {tag}'
        mr_url = gitlab_api.push_and_create_mr(
            remote, branch_name, target, title)
        if not mr_url:
            tout.error(f'Failed to push/create MR for {branch_name}')
            return 1

    _subtree_record(dbs, source, squash_hash, merge_hash)

    return 0


def _prepare_get_commits(dbs, source, remote, target):
    """Get the next commits to apply, handling subtrees and skips.

    Fetch the next batch of commits from the source. If a subtree
    update is encountered, apply it and retry. If all commits in a
    merge are already processed, advance the source and retry.

    Args:
        dbs (Database): Database instance
        source (str): Source branch name
        remote (str): Git remote name (e.g. 'ci'), or None to skip
            subtree updates
        target (str): Target branch name (e.g. 'master')

    Returns:
        tuple: (NextCommitsInfo, return_code) where return_code is None
            on success, or an int (0 or 1) if there is nothing to do
    """
    while True:
        info, err = get_next_commits(dbs, source)
        if err:
            tout.error(err)
            return None, 1

        if info.subtree_update:
            name, tag = info.subtree_update
            tout.info(f'Subtree update needed: {name} -> {tag}')
            if not remote:
                tout.error('Cannot apply subtree update without remote')
                return None, 1
            ret = apply_subtree_update(dbs, source, name, tag,
                                       info.advance_to, remote,
                                       target)
            if ret:
                return None, ret
            continue

        if not info.commits:
            if info.advance_to:
                dbs.source_set(source, info.advance_to)
                dbs.commit()
                tout.info(f"Advanced source '{source}' to "
                          f'{info.advance_to[:12]}')
                continue
            tout.info('No new commits to cherry-pick')
            return None, 0

        return info, None


def prepare_apply(dbs, source, branch, remote=None, target=None,  # pylint: disable=too-many-arguments,too-many-locals
                   info=None):
    """Prepare for applying commits from a source branch

    Get the next commits, set up the branch name and prints info about
    what will be applied. When a subtree update is encountered, apply it
    automatically and retry.

    Args:
        dbs (Database): Database instance
        source (str): Source branch name
        branch (str): Branch name to use, or None to auto-generate
        remote (str): Git remote name (e.g. 'ci'), or None to skip
            subtree updates
        target (str): Target branch name (e.g. 'master')
        info (NextCommitsInfo): Pre-fetched commit info from
            _prepare_get_commits(), or None to fetch it here

    Returns:
        tuple: (ApplyInfo, return_code) where ApplyInfo is set if there are
            commits to apply, or None with return_code indicating the result
            (0 for no commits, 1 for error)
    """
    if info is None:
        info, ret = _prepare_get_commits(dbs, source, remote, target)
        if ret is not None:
            return None, ret

    commits = info.commits

    # Save current branch to return to later
    original_branch = run_git(['rev-parse', '--abbrev-ref', 'HEAD'])

    # Generate branch name if not provided
    branch_name = branch
    if not branch_name:
        # Use first commit's short hash as part of branch name
        branch_name = f'cherry-{commits[0].chash}'

    # Delete branch if it already exists
    if run_git(['branch', '--list', branch_name]).strip():
        tout.info(f'Deleting existing branch {branch_name}')
        run_git(['branch', '-D', branch_name])

    if info.merge_found:
        tout.info(f'Applying next set from {source} ({len(commits)} commits):')
    else:
        tout.info(f'Applying remaining commits from {source} '
                  f'({len(commits)} commits, no merge found):')

    tout.info(f'  Branch: {branch_name}')
    for commit in commits:
        tout.info(f'  {commit.chash} {commit.subject}')
    tout.info('')

    return ApplyInfo(commits, branch_name, original_branch,
                     info.merge_found, info.advance_to), 0


# pylint: disable=too-many-arguments
def _applied_advance_source(dbs, source, commits, advance_to,
                            signal_commit):
    """Advance the source position after skipping already-applied commits.

    Chooses the new position from advance_to, signal_commit, or the
    last commit hash, in that priority order.
    """
    if advance_to is not None:
        new_hash = advance_to
    elif signal_commit:
        new_hash = signal_commit
    else:
        new_hash = commits[-1].hash

    dbs.source_set(source, new_hash)
    dbs.commit()
    tout.info(f"Updated source '{source}' to {new_hash[:12]}")


def _applied_create_skip_mr(args, source, commits, branch_name, conv_log):
    """Push a skip branch and create an MR recording the skip.

    Returns:
        int: 0 on success, 1 on failure
    """
    remote = args.remote
    target = args.target

    try:
        run_git(['checkout', '-b', branch_name, f'{remote}/{target}'])
    except Exception:  # pylint: disable=broad-except
        # Branch may already exist from failed attempt
        try:
            run_git(['checkout', branch_name])
        except Exception:  # pylint: disable=broad-except
            tout.error(f'Could not create/checkout branch {branch_name}')
            return 1

    title = f'{SKIPPED_TAG} [pickman] {commits[-1].subject}'
    summary = format_history(source, commits, branch_name)
    description = (f'{summary}\n\n'
                   f'**Status:** Commits already applied to {target} '
                   f'with different hashes.\n\n'
                   f'### Conversation log\n{conv_log}')

    mr_url = gitlab_api.push_and_create_mr(
        remote, branch_name, target, title, description
    )
    if not mr_url:
        return 1

    return 0


def handle_already_applied(dbs, source, commits, branch_name, conv_log, args,
                           signal_commit, advance_to=None):
    """Handle the case where commits are already applied to the target branch

    Creates an MR with [skip] prefix to record the attempt and updates the
    source position in the database.

    Args:
        dbs (Database): Database instance
        source (str): Source branch name
        commits (list): List of CommitInfo namedtuples
        branch_name (str): Branch name that was attempted
        conv_log (str): Conversation log from the agent
        args (Namespace): Parsed arguments with 'push', 'remote', 'target'
        signal_commit (str): Last commit hash from signal file
        advance_to (str): Hash to advance source to, or None to use last
            commit. If explicitly None (sub-merge batch), source is not
            advanced.

    Returns:
        int: 0 on success, 1 on failure
    """
    tout.info('Commits already applied to target branch - creating skip MR')

    for commit in commits:
        dbs.commit_set_status(commit.hash, 'skipped')
    dbs.commit()

    _applied_advance_source(dbs, source, commits, advance_to,
                            signal_commit)

    if args.push:
        return _applied_create_skip_mr(args, source, commits,
                                       branch_name, conv_log)

    return 0


def execute_apply(dbs, source, commits, branch_name, args, advance_to=None):  # pylint: disable=too-many-locals,too-many-branches
    """Execute the apply operation: run agent, update database, push MR

    Args:
        dbs (Database): Database instance
        source (str): Source branch name
        commits (list): List of CommitInfo namedtuples
        branch_name (str): Branch name for cherry-picks
        args (Namespace): Parsed arguments with 'push', 'remote', 'target'
        advance_to (str): Hash to advance source to after success, or None
            to skip source advancement (sub-merge batch)

    Returns:
        tuple: (ret, success, conv_log) where ret is 0 on success,
            1 on failure
    """
    # Check for already applied commits before proceeding
    applied_map = build_applied_map(commits)

    # Add all commits to database with 'pending' status (agent updates later)
    source_id = dbs.source_get_id(source)

    # A parked conflict's change is missing from the tree, so a commit which
    # touches the same file may be adjusting work which is not there.  Say so
    # rather than apply it blind
    shared = parked_overlap(status_parked(dbs, source_id), commits)
    if shared:
        tout.warning(f'{len(shared)} file(s) here are also touched by parked '
                     'conflicts, whose change is missing from the tree:')
        for path in sorted(shared)[:5]:
            owners = ', '.join(chash[:11] for chash, _ in shared[path])
            tout.warning(f'  {path} (parked: {owners})')
        if len(shared) > 5:
            tout.warning(f'  ... and {len(shared) - 5} more')
    for commit in commits:
        dbs.commit_add(commit.hash, source_id, commit.subject, commit.author,
                       status='pending')
    dbs.commit()

    # Convert CommitInfo to AgentCommit format expected by agent
    agent_commits = [AgentCommit(c.hash, c.chash, c.subject,
                                 applied_map.get(c.hash)) for c in commits]
    success, conv_log = agent.cherry_pick_commits(agent_commits, source,
                                                  branch_name)

    # Check for signal file from agent
    signal_status, signal_commit = agent.read_signal_file()
    if signal_status == agent.SIGNAL_APPLIED:
        ret = handle_already_applied(dbs, source, commits, branch_name,
                                     conv_log, args, signal_commit,
                                     advance_to)
        return ret, False, conv_log

    # Verify the branch actually exists - agent may have aborted and deleted it
    if success:
        try:
            exists = run_git(['branch', '--list', branch_name]).strip()
        except Exception:  # pylint: disable=broad-except
            exists = ''
        if not exists:
            tout.warning(f'Branch {branch_name} does not exist - '
                         'agent may have aborted')
            success = False

    # The agent may report success while leaving a conflict unresolved, for
    # example if it was blocked partway through.  Pushing that branch or
    # committing history on top of it would be wrong, and the later checkout
    # back to the original branch would fail, so trust the git state over the
    # agent: abandon the set and get back to a clean tree
    if success and _has_unresolved_conflict():
        tout.warning(f'Branch {branch_name} has unresolved conflicts - '
                     'agent did not finish; abandoning this set')
        _abort_in_progress()
        success = False

    # Update commit status based on result
    status = 'applied' if success else 'conflict'
    for commit in commits:
        dbs.commit_set_status(commit.hash, status)
    dbs.commit()

    ret = 0 if success else 1

    if success:
        # Push and create MR if requested
        if args.push:
            title = f'[pickman] {commits[-1].subject}'
            summary = format_history(source, commits, branch_name)
            note = parked_overlap_note(shared)
            description = (f'{summary}\n\n{note}\n'
                           f'### Conversation log\n{conv_log}')
            if not push_mr(args, branch_name, title, description):
                ret = 1
        else:
            tout.info(f"Use 'pickman commit-source {source} "
                      f"{commits[-1].chash}' to update the database")

    # Update database with the last processed commit if successful
    if success and advance_to is not None:
        dbs.source_set(source, advance_to)
        dbs.commit()

    return ret, success, conv_log


def do_apply(args, dbs, info=None):
    """Apply the next set of commits using Claude agent

    Args:
        args (Namespace): Parsed arguments with 'source' and 'branch' attributes
        dbs (Database): Database instance
        info (NextCommitsInfo): Pre-fetched commit info from
            _prepare_get_commits(), or None to fetch during prepare

    Returns:
        int: 0 on success, 1 on failure
    """
    source = args.source
    info, ret = prepare_apply(dbs, source, args.branch, args.remote,
                              args.target, info=info)
    if not info:
        return ret

    commits = info.commits
    branch_name = info.branch_name
    original_branch = info.original_branch

    ret, success, conv_log = execute_apply(dbs, source, commits,
                                           branch_name, args,
                                           info.advance_to)

    # Write history file if successful
    if success:
        write_history(source, commits, branch_name, conv_log)

    # Return to original branch
    current_branch = run_git(['rev-parse', '--abbrev-ref', 'HEAD'])
    if current_branch != original_branch:
        tout.info(f'Returning to {original_branch}')
        run_git(['checkout', original_branch])

    return ret


def do_pick(args, dbs):  # pylint: disable=unused-argument,too-many-locals
    """Cherry-pick commits ad-hoc using Claude agent

    This allows cherry-picking a commit range or merge commit children without
    tracking in the database. Useful for one-off cherry-picks.

    Args:
        args (Namespace): Parsed arguments with 'commits', 'branch', etc.
        dbs (Database): Database instance (unused for ad-hoc picks)

    Returns:
        int: 0 on success, 1 on failure
    """
    commit_spec = args.commits

    # Get commits to cherry-pick
    commits, err = get_commits_for_pick(commit_spec)
    if err:
        tout.error(err)
        return 1

    if not commits:
        tout.info('No commits to cherry-pick')
        return 0

    # Save current branch to return to later
    original_branch = run_git(['rev-parse', '--abbrev-ref', 'HEAD'])

    # Generate branch name if not provided
    branch_name = args.branch
    if not branch_name:
        branch_name = f'pick-{commits[0].chash}'

    # Delete branch if it already exists
    if run_git(['branch', '--list', branch_name]).strip():
        tout.info(f'Deleting existing branch {branch_name}')
        run_git(['branch', '-D', branch_name])

    tout.info(f'Cherry-picking {len(commits)} commit(s):')
    tout.info(f'  Branch: {branch_name}')
    for commit in commits:
        tout.info(f'  {commit.chash} {commit.subject}')
    tout.info('')

    # Convert CommitInfo to AgentCommit format (no applied_as for ad-hoc)
    agent_commits = [AgentCommit(c.hash, c.chash, c.subject, None)
                     for c in commits]

    # Run the agent to cherry-pick
    success, conv_log = agent.cherry_pick_commits(agent_commits, 'ad-hoc',
                                                  branch_name)

    # Verify the branch actually exists - agent may have aborted and deleted it
    if success:
        try:
            exists = run_git(['branch', '--list', branch_name]).strip()
        except Exception:  # pylint: disable=broad-except
            exists = ''
        if not exists:
            tout.warning(f'Branch {branch_name} does not exist - '
                         'agent may have aborted')
            success = False

    ret = 0 if success else 1

    if success and args.push:
        title = f'[pick] {commits[-1].subject}'
        commit_list = '\n'.join(f'- {c.chash} {c.subject}' for c in commits)
        description = (f'Ad-hoc cherry-pick of {len(commits)} commit(s)\n\n'
                       f'### Commits\n{commit_list}\n\n'
                       f'### Conversation log\n{conv_log}')
        if not push_mr(args, branch_name, title, description):
            ret = 1
    elif success:
        tout.info(f'Commits cherry-picked to branch {branch_name}')

    # Return to original branch
    current_branch = run_git(['rev-parse', '--abbrev-ref', 'HEAD'])
    if current_branch != original_branch:
        tout.info(f'Returning to {original_branch}')
        run_git(['checkout', original_branch])

    return ret


def do_push_branch(args, dbs):  # pylint: disable=unused-argument
    """Push a branch using the GitLab API token for authentication

    This allows pushing as the token owner (e.g., a bot account) rather than
    using the user's configured git credentials. Useful for ensuring all
    pickman commits come from the same account.

    Args:
        args (Namespace): Parsed arguments with 'remote', 'branch', 'force',
            'run_ci'
        dbs (Database): Database instance

    Returns:
        int: 0 on success, 1 on failure
    """
    skip_ci = not args.run_ci
    try:
        gitlab_api.push_branch(args.remote, args.branch, args.force,
                               skip_ci=skip_ci)
    except command.CommandExc:
        return 1
    return 0


def do_commit_source(args, dbs):
    """Update the database with the last cherry-picked commit

    Args:
        args (Namespace): Parsed arguments with 'source' and 'commit' attributes
        dbs (Database): Database instance

    Returns:
        int: 0 on success, 1 on failure
    """
    source = args.source
    commit = args.commit

    # Resolve the commit to a full hash
    try:
        full_hash = run_git(['rev-parse', commit])
    except Exception:  # pylint: disable=broad-except
        tout.error(f"Could not resolve commit '{commit}'")
        return 1

    old_commit = dbs.source_get(source)
    if not old_commit:
        tout.error(f"Source '{source}' not found in database")
        return 1

    dbs.source_set(source, full_hash)
    dbs.commit()

    short_old = old_commit[:12]
    short_new = full_hash[:12]
    tout.info(f"Updated '{source}': {short_old} -> {short_new}")

    return 0


def do_switch_source(args, dbs):
    """Copy a tracked source's position to a new source branch

    Used when upstream renames or pivots the branch being tracked (e.g.
    after a release the integration moves from us/next to us/master). The
    new source is seeded with the old source's last cherry-picked commit
    so subsequent next-set/apply commands resume from the same point.

    Args:
        args (Namespace): Parsed arguments with 'old_source', 'new_source',
            'at' and 'force' attributes
        dbs (Database): Database instance

    Returns:
        int: 0 on success, 1 on failure
    """
    old_source = args.old_source
    new_source = args.new_source

    last_commit = dbs.source_get(old_source)
    if not last_commit:
        tout.error(f"Source '{old_source}' not found in database")
        return 1

    if args.at:
        try:
            seed = gitutil.get_hash(args.at)
        except Exception:  # pylint: disable=broad-except
            tout.error(f"Could not resolve commit '{args.at}'")
            return 1
    else:
        seed = last_commit

    if not gitutil.ref_exists(new_source):
        tout.error(f"New source '{new_source}' is not a valid git ref")
        return 1

    if not gitutil.is_ancestor(seed, new_source):
        tout.error(
            f"Commit {seed[:12]} is not reachable from '{new_source}'; "
            "cherry-picking from it would re-apply commits. Use --at to "
            "pick a different start point.")
        return 1

    existing = dbs.source_get(new_source)
    if existing and not args.force:
        tout.error(
            f"Source '{new_source}' already tracked at {existing[:12]}; "
            "use --force to overwrite")
        return 1

    dbs.source_set(new_source, seed)
    dbs.commit()

    tout.info(
        f"Switched source: '{old_source}' -> '{new_source}' at {seed[:12]}")
    if existing:
        tout.info(
            f"  (previous '{new_source}' position {existing[:12]} "
            "overwritten)")
    tout.info(
        f"Old source '{old_source}' kept at {last_commit[:12]}; remove "
        "manually if no longer needed.")

    return 0


def _rewind_fetch_merges(current, count):
    """Fetch first-parent merges and find target index.

    Returns:
        tuple: (merges, target_idx) where merges is a list of
            (hash, short_hash, subject) tuples, or None on error
    """
    try:
        out = run_git([
            'log', '--first-parent', '--merges', '--format=%H|%h|%s',
            f'-{count + 1}', current
        ])
    except Exception:  # pylint: disable=broad-except
        tout.error(f'Could not read merge history for {current[:12]}')
        return None

    if not out:
        tout.error('No merges found in history')
        return None

    merges = []
    for line in out.strip().split('\n'):
        if not line:
            continue
        parts = line.split('|', 2)
        merges.append((parts[0], parts[1],
                        parts[2] if len(parts) > 2 else ''))

    if len(merges) < 2:
        tout.error(f'Not enough merges to rewind by {count}')
        return None

    target_idx = min(count, len(merges) - 1)
    return merges, target_idx


def _rewind_get_range_commits(dbs, target_hash, current):
    """Get commits in range and filter to those in database.

    Returns:
        tuple: (range_hashes_str, db_commits_list) or None on error
    """
    try:
        range_hashes = run_git([
            'rev-list', f'{target_hash}..{current}'
        ])
    except Exception:  # pylint: disable=broad-except
        tout.error(f'Could not list commits in range '
                   f'{target_hash[:12]}..{current[:12]}')
        return None

    db_commits = []
    if range_hashes:
        for chash in range_hashes.strip().split('\n'):
            if chash and dbs.commit_get(chash):
                db_commits.append(chash)

    return range_hashes, db_commits


def _rewind_find_branches(range_hashes, remote):
    """Find cherry-pick branches matching commits in the range.

    Returns:
        list: Branch names (without remote prefix) that match
    """
    if not range_hashes:
        return []

    hash_set = set(range_hashes.strip().split('\n'))
    try:
        branch_out = run_git(
            ['branch', '-r', '--list', f'{remote}/cherry-*'])
    except Exception:  # pylint: disable=broad-except
        branch_out = ''

    mr_branches = []
    remote_prefix = f'{remote}/'
    for line in branch_out.strip().split('\n'):
        branch = line.strip()
        if not branch:
            continue
        # Branch is like 'ci/cherry-abc1234'; extract the hash part
        short = branch.removeprefix(f'{remote_prefix}cherry-')
        # Check if any commit in the range starts with this hash
        for chash in hash_set:
            if chash.startswith(short):
                mr_branches.append(
                    branch.removeprefix(remote_prefix))
                break

    return mr_branches


def _rewind_find_mrs(mr_branches, remote):
    """Look up MR details for matching branches.

    Returns:
        list: PickmanMr objects whose source_branch matches
    """
    if not mr_branches:
        return []

    matched_mrs = []
    mrs = gitlab_api.get_open_pickman_mrs(remote)
    if mrs:
        branch_set = set(mr_branches)
        for merge_req in mrs:
            if merge_req.source_branch in branch_set:
                matched_mrs.append(merge_req)

    return matched_mrs


def _rewind_show_summary(source, current, merges, target_idx,
                         db_commits, matched_mrs, mr_branches,
                         force):
    """Display rewind summary."""
    current_short = current[:12]
    target_chash = merges[target_idx][1]
    target_subject = merges[target_idx][2]

    prefix = '' if force else '[dry run] '
    tout.info(f"{prefix}Rewind '{source}': "
              f'{current_short} -> {target_chash}')
    tout.info(f'  Target: {target_chash} {target_subject}')
    tout.info('  Merges being rewound:')
    for i in range(target_idx):
        tout.info(f'    {merges[i][1]} {merges[i][2]}')
    tout.info(f'  Commits to delete from database: {len(db_commits)}')

    if matched_mrs:
        tout.info('  MRs to delete on GitLab:')
        for merge_req in matched_mrs:
            tout.info(f'    !{merge_req.iid}: {merge_req.title}')
            tout.info(f'      {merge_req.web_url}')
    elif mr_branches:
        tout.info('  Branches to check for MRs:')
        for branch in mr_branches:
            tout.info(f'    {branch}')


def do_rewind(args, dbs):
    """Rewind the source position back by N merges

    By default performs a dry run, showing what would happen. Use --force
    to actually execute the rewind.

    Walks back N merges on the first-parent chain from the current source
    position, deletes the commits in that range from the database, and
    resets the source to the earlier position.

    Args:
        args (Namespace): Parsed arguments with 'source', 'count', 'force'
        dbs (Database): Database instance

    Returns:
        int: 0 on success, 1 on failure
    """
    source = args.source
    count = args.count
    force = args.force

    current = dbs.source_get(source)
    if not current:
        tout.error(f"Source '{source}' not found in database")
        return 1

    result = _rewind_fetch_merges(current, count)
    if not result:
        return 1
    merges, target_idx = result
    target_hash = merges[target_idx][0]

    result = _rewind_get_range_commits(dbs, target_hash, current)
    if not result:
        return 1
    range_hashes, db_commits = result

    mr_branches = _rewind_find_branches(range_hashes, args.remote)
    matched_mrs = _rewind_find_mrs(mr_branches, args.remote)

    _rewind_show_summary(source, current, merges, target_idx,
                         db_commits, matched_mrs, mr_branches, force)

    if not force:
        tout.info('Use --force to execute this rewind')
        return 0

    for chash in db_commits:
        dbs.commit_delete(chash)

    dbs.source_set(source, target_hash)
    dbs.commit()

    tout.info(f'  Deleted {len(db_commits)} commit(s) from database')

    return 0


# pylint: disable=too-many-locals,too-many-branches,too-many-statements
def process_single_mr(remote, merge_req, dbs, target):
    """Process review comments on a single MR

    Args:
        remote (str): Remote name
        merge_req (PickmanMr): MR object from get_open_pickman_mrs()
        dbs (Database): Database instance for tracking processed comments
        target (str): Target branch for rebase operations

    Returns:
        int: 1 if MR was processed, 0 otherwise
    """
    mr_iid = merge_req.iid
    comments = gitlab_api.get_mr_comments(remote, mr_iid)
    if comments is None:
        comments = []

    # Filter to unresolved comments that haven't been processed
    unresolved = []
    for com in comments:
        if com.resolved:
            continue
        if dbs.comment_is_processed(mr_iid, com.id):
            continue
        unresolved.append(com)

    # Check for unskip comments first (takes precedence)
    handled, unresolved = handle_unskip_comments(
        remote, mr_iid, merge_req.title, unresolved, dbs)
    processed = 1 if handled else 0

    # Check for skip comments
    if handle_skip_comments(remote, mr_iid, merge_req.title, unresolved, dbs):
        return processed + 1

    # If MR is currently skipped, don't process rebases or other comments
    if SKIPPED_TAG in merge_req.title:
        return processed

    # Check if rebase is needed
    needs_rebase = merge_req.needs_rebase or merge_req.has_conflicts

    # Skip if no comments and no rebase needed
    if not unresolved and not needs_rebase:
        return processed

    tout.info('')
    if needs_rebase:
        if merge_req.has_conflicts:
            tout.info(f"MR !{mr_iid} has merge conflicts - rebasing...")
        else:
            tout.info(f"MR !{mr_iid} needs rebase...")
    if unresolved:
        tout.info(f"MR !{mr_iid} has {len(unresolved)} new comment(s):")
        for comment in unresolved:
            tout.info(f'  [{comment.author}]: {comment.body[:80]}...')

    # Run agent to handle comments and/or rebase
    success, conversation_log = agent.handle_mr_comments(
        mr_iid,
        merge_req.source_branch,
        unresolved,
        remote,
        target,
        needs_rebase=needs_rebase,
        has_conflicts=merge_req.has_conflicts,
        mr_description=merge_req.description,
    )

    if success:
        # Mark comments as processed
        for comment in unresolved:
            dbs.comment_mark_processed(mr_iid, comment.id)
        dbs.commit()

        # Update MR description with comments and conversation log
        old_desc = merge_req.description
        comment_summary = '\n'.join(
            f"- [{c.author}]: {c.body}"
            for c in unresolved
        )
        new_desc = (f"{old_desc}\n\n### Review response\n\n"
                    f"**Comments addressed:**\n{comment_summary}\n\n"
                    f"**Response:**\n{conversation_log}")
        gitlab_api.update_mr_desc(remote, mr_iid, new_desc)

        # Update .pickman-history
        update_history(merge_req.source_branch,
                                   unresolved, conversation_log)

        tout.info(f'Updated MR !{mr_iid} description and history')
    else:
        tout.error(f"Failed to handle comments for MR !{mr_iid}")

    return processed + 1


def process_mr_reviews(remote, mrs, dbs, target='master'):
    """Process review comments on open MRs

    Checks each MR for unresolved comments and uses Claude agent to address
    them. Updates MR description and .pickman-history with conversation log.

    Args:
        remote (str): Remote name
        mrs (list): List of MR dicts from get_open_pickman_mrs()
        dbs (Database): Database instance for tracking processed comments
        target (str): Target branch for rebase operations

    Returns:
        int: Number of MRs with comments processed
    """
    # Save current branch to restore later
    original_branch = run_git(['rev-parse', '--abbrev-ref', 'HEAD'])

    # Fetch to get latest remote state (needed for rebase)
    tout.info(f'Fetching {remote}...')
    run_git(['fetch', remote])

    processed = 0
    for merge_req in mrs:
        processed += process_single_mr(remote, merge_req, dbs, target)

    # Restore original branch
    if processed:
        tout.info(f'Returning to {original_branch}')
        run_git(['checkout', original_branch])

    return processed


def _rebase_mr_branch(remote, merge_req, dbs, target):
    """Rebase an MR branch onto the target before attempting a pipeline fix

    When a branch needs rebasing, the pipeline failure may be caused by the
    stale base rather than by the cherry-picked commits. Rebasing and pushing
    triggers a fresh pipeline run.

    Args:
        remote (str): Remote name
        merge_req (PickmanMr): MR with a failed pipeline
        dbs (Database): Database instance for tracking fix attempts
        target (str): Target branch

    Returns:
        True if the branch was rebased and pushed, False if the rebase
        failed (conflicts), or None if no rebase is needed
    """
    if not merge_req.needs_rebase and not merge_req.has_conflicts:
        return None

    mr_iid = merge_req.iid
    branch = merge_req.source_branch
    if merge_req.has_conflicts:
        tout.info(f'MR !{mr_iid}: has conflicts, rebasing before '
                  f'pipeline fix...')
    else:
        tout.info(f'MR !{mr_iid}: needs rebase, rebasing before '
                  f'pipeline fix...')
    run_git(['checkout', branch])
    try:
        run_git(['rebase', f'{remote}/{target}'])
    except command.CommandExc:
        tout.warning(f'MR !{mr_iid}: rebase failed, aborting')
        try:
            run_git(['rebase', '--abort'])
        except command.CommandExc:
            pass
        return False
    gitlab_api.push_branch(remote, branch, force=True, skip_ci=False)
    dbs.pfix_add(mr_iid, merge_req.pipeline_id, 0, 'rebased')
    dbs.commit()
    tout.info(f'MR !{mr_iid}: rebased and pushed, waiting for '
              f'new pipeline')
    return True


def _attempt_pipeline_fix(remote, merge_req, dbs, target, attempt):
    """Run the agent to fix a failed pipeline and report the result

    Fetches the failed-job logs, invokes the fix agent, then pushes the
    result and updates the MR description and history on success, or posts
    a failure comment otherwise.

    Args:
        remote (str): Remote name
        merge_req (PickmanMr): MR with a failed pipeline
        dbs (Database): Database instance for tracking fix attempts
        target (str): Target branch
        attempt (int): Current fix attempt number

    Returns:
        bool: True if the fix was attempted, False if no failed jobs
            were found
    """
    mr_iid = merge_req.iid

    # Fetch failed jobs
    failed_jobs = gitlab_api.get_failed_jobs(remote, merge_req.pipeline_id)
    if not failed_jobs:
        tout.info(f'MR !{mr_iid}: no failed jobs found')
        dbs.pfix_add(mr_iid, merge_req.pipeline_id, attempt, 'no_jobs')
        dbs.commit()
        return False

    # Run agent to fix the failures
    success, conversation_log = agent.fix_pipeline(
        mr_iid,
        merge_req.source_branch,
        failed_jobs,
        remote,
        target,
        mr_description=merge_req.description,
        attempt=attempt,
    )

    status = 'success' if success else 'failure'
    dbs.pfix_add(mr_iid, merge_req.pipeline_id, attempt, status)
    dbs.commit()

    if success:
        # Push the fix branch to the original MR branch
        branch = merge_req.source_branch
        gitlab_api.push_branch(remote, branch, force=True,
                               skip_ci=False)

        # Update MR description with fix log
        old_desc = merge_req.description
        job_names = ', '.join(j.name for j in failed_jobs)
        new_desc = (f"{old_desc}\n\n### Pipeline fix (attempt {attempt})"
                    f"\n\n**Failed jobs:** {job_names}\n\n"
                    f"**Response:**\n{conversation_log}")
        gitlab_api.update_mr_desc(remote, mr_iid, new_desc)

        # Post a comment summarising the fix
        gitlab_api.reply_to_mr(
            remote, mr_iid,
            f'Pipeline fix (attempt {attempt}): '
            f'fixed failed job(s) {job_names}.\n\n'
            f'{conversation_log[:2000]}')

        # Update .pickman-history
        update_history_pipeline_fix(merge_req.source_branch, failed_jobs,
                                    conversation_log, attempt)

        tout.info(f'MR !{mr_iid}: pipeline fix pushed (attempt {attempt})')
    else:
        gitlab_api.reply_to_mr(
            remote, mr_iid,
            f'Pipeline fix attempt {attempt} failed. '
            f'Agent output:\n\n{conversation_log[:1000]}')
        tout.error(f'MR !{mr_iid}: pipeline fix failed '
                   f'(attempt {attempt})')

    return True


def process_pipeline_failures(remote, mrs, dbs, target, max_retries):
    """Process pipeline failures on open MRs

    Checks each MR for failed pipelines and uses Claude agent to diagnose
    and fix them. Tracks attempts in the database to avoid reprocessing.

    Args:
        remote (str): Remote name
        mrs (list): List of active (non-skipped) PickmanMr tuples
        dbs (Database): Database instance for tracking fix attempts
        target (str): Target branch
        max_retries (int): Maximum fix attempts per MR

    Returns:
        int: Number of MRs with pipeline fixes attempted
    """
    # Save current branch to restore later
    original_branch = run_git(['rev-parse', '--abbrev-ref', 'HEAD'])

    # Fetch to get latest remote state
    tout.info(f'Fetching {remote}...')
    run_git(['fetch', remote])

    processed = 0
    for merge_req in mrs:
        mr_iid = merge_req.iid

        # Skip if pipeline is not failed or has no pipeline
        if merge_req.pipeline_status != 'failed':
            continue
        if merge_req.pipeline_id is None:
            continue

        # Skip if this pipeline was already handled
        if dbs.pfix_has(mr_iid, merge_req.pipeline_id):
            continue

        rebased = _rebase_mr_branch(remote, merge_req, dbs, target)
        if rebased is not None:
            if rebased:
                processed += 1
            continue

        attempt = dbs.pfix_count(mr_iid) + 1

        # Check retry limit
        if attempt > max_retries:
            tout.info(f'MR !{mr_iid}: reached fix retry limit '
                      f'({max_retries}), skipping')
            gitlab_api.reply_to_mr(
                remote, mr_iid,
                f'Pipeline fix: reached retry limit ({max_retries} '
                f'attempts). Manual intervention required.')
            dbs.pfix_add(mr_iid, merge_req.pipeline_id, attempt, 'skipped')
            dbs.commit()
            continue

        tout.info('')
        tout.info(f'MR !{mr_iid}: pipeline {merge_req.pipeline_id} failed, '
                  f'attempting fix (attempt {attempt}/{max_retries})...')

        if _attempt_pipeline_fix(remote, merge_req, dbs, target, attempt):
            processed += 1

    # Restore original branch
    if processed:
        tout.info(f'Returning to {original_branch}')
        run_git(['checkout', original_branch])

    return processed


def update_history_pipeline_fix(branch_name, failed_jobs, conversation_log,
                                attempt):
    """Append pipeline fix handling to .pickman-history

    Args:
        branch_name (str): Branch name for the MR
        failed_jobs (list): List of FailedJob tuples that were fixed
        conversation_log (str): Agent conversation log
        attempt (int): Fix attempt number
    """
    job_summary = '\n'.join(
        f'- {j.name} ({j.stage})'
        for j in failed_jobs
    )

    entry = f'''### Pipeline fix: {date.today()} (attempt {attempt})

Branch: {branch_name}

Failed jobs:
{job_summary}

### Conversation log
{conversation_log}

---

'''

    # Append to history file
    existing = ''
    if os.path.exists(HISTORY_FILE):
        with open(HISTORY_FILE, 'r', encoding='utf-8') as fhandle:
            existing = fhandle.read()

    with open(HISTORY_FILE, 'w', encoding='utf-8') as fhandle:
        fhandle.write(existing + entry)

    # Commit the history file
    run_git(['add', '-f', HISTORY_FILE])
    run_git(['commit', '-m',
             f'pickman: Record pipeline fix for {branch_name}'])


def update_history(branch_name, comments, conversation_log):
    """Append review handling to .pickman-history

    Args:
        branch_name (str): Branch name for the MR
        comments (list): List of comments that were addressed
        conversation_log (str): Agent conversation log
    """
    comment_summary = '\n'.join(
        f'- [{c.author}]: {c.body[:100]}...'
        for c in comments
    )

    entry = f'''### Review: {date.today()}

Branch: {branch_name}

Comments addressed:
{comment_summary}

### Conversation log
{conversation_log}

---

'''

    # Append to history file
    existing = ''
    if os.path.exists(HISTORY_FILE):
        existing = tools.read_file(HISTORY_FILE, binary=False)

    tools.write_file(HISTORY_FILE, existing + entry, binary=False)

    # Commit the history file
    run_git(['add', '-f', HISTORY_FILE])
    run_git(['commit', '-m',
             f'pickman: Record review handling for {branch_name}'])


def do_review(args, dbs):
    """Check open pickman MRs and handle comments

    Lists open MRs created by pickman, checks for human comments, and uses
    Claude agent to address them.

    Args:
        args (Namespace): Parsed arguments with 'remote' attribute
        dbs (Database): Database instance

    Returns:
        int: 0 on success, 1 on failure
    """
    remote = args.remote

    # Get open pickman MRs
    mrs = gitlab_api.get_open_pickman_mrs(remote)
    if mrs is None:
        return 1

    if not mrs:
        tout.info('No open pickman MRs found')
        return 0

    tout.info(f'Found {len(mrs)} open pickman MR(s):')
    for merge_req in mrs:
        tout.info(f"  !{merge_req.iid}: {merge_req.title}")

    process_mr_reviews(remote, mrs, dbs)

    return 0


def parse_mr_description(desc):
    """Parse a pickman MR description to extract source and last commit

    Args:
        desc (str): MR description text

    Returns:
        tuple: (source_branch, last_commit_hash) or (None, None)
            if not parseable
    """
    # Extract source branch from "## date: source_branch" line
    source_match = re.search(r'^## [^:]+: (.+)$', desc, re.MULTILINE)
    if not source_match:
        return None, None
    source = source_match.group(1)

    # Extract commits from '- hash subject' lines (must be at least 7 chars)
    commit_matches = re.findall(r'^- ([a-f0-9]{7,}) ', desc, re.MULTILINE)
    if not commit_matches:
        return None, None

    # Last commit is the last one in the list
    last_hash = commit_matches[-1]

    return source, last_hash


def process_merged_mrs(remote, source, dbs):
    """Check for merged MRs and update the database

    Args:
        remote (str): Remote name
        source (str): Source branch name to filter by
        dbs (Database): Database instance

    Returns:
        int: Number of MRs processed, or -1 on error
    """
    merged_mrs = gitlab_api.get_merged_pickman_mrs(remote)
    if merged_mrs is None:
        return -1

    processed = 0
    for merge_req in merged_mrs:
        mr_source, last_hash = parse_mr_description(merge_req.description)

        # Only process MRs for the requested source branch
        if mr_source != source:
            continue

        # Check if this MR's last commit is newer than current database
        current = dbs.source_get(source)
        if not current:
            continue

        # Resolve the short hash to full hash
        try:
            full_hash = run_git(['rev-parse', last_hash])
        except Exception:  # pylint: disable=broad-except
            tout.warning(f"Could not resolve commit '{last_hash}' from "
                         f"MR !{merge_req.iid}")
            continue

        # Skip if already at this position
        if full_hash == current:
            continue

        # Check if this commit is newer than current (current is ancestor of it)
        try:
            # Is current an ancestor of last_hash? (meaning last_hash is newer)
            run_git(['merge-base', '--is-ancestor', current, full_hash])
        except Exception:  # pylint: disable=broad-except
            continue  # current is not an ancestor, so last_hash is not newer

        # Update database
        short_old = current[:12]
        short_new = full_hash[:12]
        tout.info(f"MR !{merge_req.iid} merged, updating '{source}': "
                  f'{short_old} -> {short_new}')
        dbs.source_set(source, full_hash)
        dbs.commit()
        processed += 1

    return processed


def do_step(args, dbs):
    """Create an MR if below the max allowed

    Checks for merged pickman MRs and updates the database, then checks for
    open pickman MRs. If open MRs exist, processes any review comments. If
    the number of open MRs is below max_mrs, runs apply with push to create
    a new one.

    Args:
        args (Namespace): Parsed arguments with 'source', 'remote', 'target',
            'max_mrs'
        dbs (Database): Database instance

    Returns:
        int: 0 on success, 1 on failure
    """
    try:
        ret = _do_step(args, dbs)
    except requests.exceptions.ConnectionError as exc:
        tout.error(f'step failed with connection error: {exc}')
        return 1

    # Surface the parked-conflict backlog at the end of every run, so it does
    # not sit silently in the database.  This is a nicety, so it must never
    # break the step itself
    source_id = dbs.source_get_id(args.source) if dbs else None
    if source_id:
        warn = parked_warning(args.source, status_parked(dbs, source_id))
        if warn:
            tout.warning(warn)
    return ret


def _do_step(args, dbs):
    """Internal implementation of do_step"""
    remote = args.remote
    source = args.source

    # First check for merged MRs and update database
    processed = process_merged_mrs(remote, source, dbs)
    if processed < 0:
        return 1

    # Check for open pickman MRs
    mrs = gitlab_api.get_open_pickman_mrs(remote)
    if mrs is None:
        return 1

    # Separate skipped and active MRs
    active_mrs = [m for m in mrs if SKIPPED_TAG not in m.title]
    skipped_mrs = [m for m in mrs if SKIPPED_TAG in m.title]

    if mrs:
        if active_mrs:
            tout.info(f'Found {len(active_mrs)} open pickman MR(s):')
            for merge_req in active_mrs:
                tout.info(f"  !{merge_req.iid}: {merge_req.title}")
        if skipped_mrs:
            tout.info(f'Found {len(skipped_mrs)} skipped pickman MR(s):')
            for merge_req in skipped_mrs:
                tout.info(f"  !{merge_req.iid}: {merge_req.title}")

        # Process any review comments on all open MRs (including skipped,
        # in case they have an unskip request)
        process_mr_reviews(remote, mrs, dbs, args.target)

        # Process pipeline failures on active MRs only
        if active_mrs and args.fix_retries > 0:
            process_pipeline_failures(remote, active_mrs, dbs,
                                      args.target, args.fix_retries)

    # Process subtree updates and advance past fully-processed merges
    # regardless of MR count, since these don't create MRs
    info, ret = _prepare_get_commits(dbs, source, remote, args.target)
    if ret is not None:
        if ret:
            return ret
        return 0

    # Only block new MR creation if we've reached the max allowed open MRs
    max_mrs = args.max_mrs
    if len(active_mrs) >= max_mrs:
        tout.info('')
        tout.info(f'Already have {len(active_mrs)} open MR(s) (max: {max_mrs})')
        return 0

    # No pending MRs, run apply with push
    # First fetch to get latest remote state
    tout.info(f'Fetching {remote}...')
    run_git(['fetch', remote])

    if active_mrs:
        tout.info('Creating another MR...')
    else:
        tout.info('No pending pickman MRs, creating new one...')
    args.push = True
    args.branch = None  # Let do_apply generate branch name
    return do_apply(args, dbs, info=info)


def do_poll(args, dbs):
    """Run step repeatedly until stopped

    Runs the step command in a loop with a configurable interval. Useful for
    automated workflows that continuously process cherry-picks.

    Args:
        args (Namespace): Parsed arguments with 'source', 'interval', 'remote',
            'target'
        dbs (Database): Database instance

    Returns:
        int: 0 on success (never returns unless interrupted)
    """
    interval = args.interval
    tout.info(f'Polling every {interval} seconds (Ctrl+C to stop)...')
    tout.info('')

    while True:
        try:
            ret = do_step(args, dbs)
            if ret != 0:
                tout.warning(f'step returned {ret}')
            tout.info('')
            tout.info(f'Sleeping {interval} seconds...')
            time.sleep(interval)
            tout.info('')
        except KeyboardInterrupt:
            tout.info('')
            tout.info('Polling stopped by user')
            return 0


def do_test(args, dbs):  # pylint: disable=unused-argument
    """Run tests for this module.

    Args:
        args (Namespace): Parsed arguments
        dbs (Database): Database instance

    Returns:
        int: 0 if tests passed, 1 otherwise
    """
    loader = unittest.TestLoader()
    suite = loader.loadTestsFromModule(ftest)
    runner = unittest.TextTestRunner()
    result = runner.run(suite)

    return 0 if result.wasSuccessful() else 1


# Command dispatch table
COMMANDS = {
    'add-source': do_add_source,
    'apply': do_apply,
    'check': do_check,
    'check-gitlab': do_check_gitlab,
    'commit-source': do_commit_source,
    'compare': do_compare,
    'count-merges': do_count_merges,
    'drift': do_drift,
    'drift-accept': do_drift_accept,
    'drift-fix': do_drift_fix,
    'list-sources': do_list_sources,
    'next-merges': do_next_merges,
    'next-set': do_next_set,
    'parked': do_parked,
    'pick': do_pick,
    'poll': do_poll,
    'push-branch': do_push_branch,
    'review': do_review,
    'rewind': do_rewind,
    'status': do_status,
    'step': do_step,
    'switch-source': do_switch_source,
    'test': do_test,
}


def do_pickman(args):
    """Main entry point for pickman commands.

    Args:
        args (Namespace): Parsed arguments

    Returns:
        int: 0 on success, 1 on failure
    """
    tout.init(tout.INFO)

    handler = COMMANDS.get(args.cmd)
    if handler:
        dbs = database.Database(DB_FNAME)
        dbs.start()
        try:
            return handler(args, dbs)
        finally:
            dbs.close()
    return 1
