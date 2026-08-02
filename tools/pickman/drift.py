# SPDX-License-Identifier: GPL-2.0+
#
# Copyright 2026 Canonical Ltd.
# Written by Simon Glass <simon.glass@canonical.com>
#
"""Drift detection for pickman - finds unintended deltas from upstream

The downstream tree (ci/master) should differ from upstream only where a
downstream commit deliberately makes it so.  Every other delta is drift: a
hunk which crept in through a botched conflict resolution, a dropped hunk or
a hand-edit during a cherry-pick.

Provenance provides the ground truth.  Each commit added downstream since the
trees diverged either carries a '(cherry picked from commit ...)' line, in
which case it should leave the tree identical to upstream, or it does not, in
which case it is downstream-original and its delta is intended.  A delta which
no downstream-original commit accounts for is therefore drift.

Deltas which are intended but which provenance cannot explain - an adaptation
made while resolving a conflict, say, rather than in a commit of its own - are
recorded in the accept file (.pickman-diverge), which is a list of exceptions
rather than a full inventory of the divergence.
"""

from collections import namedtuple
import fnmatch
import hashlib
import re

# File holding the accepted (intentional) deltas from upstream
ACCEPT_FILE = '.pickman-diverge'

# Fingerprint used in the accept file to mean 'every hunk in this file'
ALL_HUNKS = '*'

# Number of hex digits in a hunk fingerprint
FP_LEN = 12

# Start of a new file in 'git diff' output: "diff --git a/path b/path"
RE_DIFF_HEADER = re.compile(r'^diff --git a/(.*) b/(.*)$')

# Start of a hunk: "@@ -1,4 +1,4 @@ optional-heading"
RE_HUNK_HEADER = re.compile(r'^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@')

# A commit which was cherry-picked from upstream records where it came from
RE_CHERRY_PICK = re.compile(r'cherry picked from commit ([a-f0-9]+)')

# One hunk of a diff between upstream and downstream
#
# path: file the hunk applies to, as seen downstream
# start: first line of the hunk on the downstream side
# count: number of lines the hunk covers on the downstream side
# added: downstream line numbers of the lines this hunk adds; empty if the
#     hunk only removes lines which upstream has
# lines: all lines of the hunk, including the '@@' header and context
# fingerprint: hash of the hunk's content, stable across line-number churn
Hunk = namedtuple('Hunk',
                  ['path', 'start', 'count', 'added', 'lines', 'fingerprint'])

# One file which differs between upstream and downstream
#
# path: file path as seen downstream
# header: the 'diff --git' preamble, needed to rebuild an applicable patch
# hunks: list of Hunk
# binary: True if git reported a binary difference (no hunks are available)
# deleted: True if the file exists upstream but not downstream, so that it
#     cannot be blamed
FileDiff = namedtuple('FileDiff',
                      ['path', 'header', 'hunks', 'binary', 'deleted'])

# One line of the accept file
#
# pattern: glob matching the paths this entry covers
# fingerprint: hunk fingerprint, or ALL_HUNKS for every hunk in the file
# reason: why this delta is intentional
Accept = namedtuple('Accept', ['pattern', 'fingerprint', 'reason'])

# The outcome of classifying one hunk
#
# hunk: the Hunk itself
# state: one of WANTED, ACCEPTED or DRIFT
# reason: for ACCEPTED, the reason from the accept file; for WANTED, the
#     downstream commit which accounts for the hunk; None for DRIFT
Verdict = namedtuple('Verdict', ['hunk', 'state', 'reason'])

# A hunk which a downstream-original commit accounts for
WANTED = 'wanted'

# A hunk which the accept file records as intentional
ACCEPTED = 'accepted'

# A hunk which nothing accounts for, and which should go back to upstream
DRIFT = 'drift'


def fingerprint(lines):
    """Fingerprint the content of a hunk

    Only the added and removed lines are hashed, so that the fingerprint
    survives the churn in surrounding context and line numbers which upstream
    causes.  It changes when the delta itself changes, which is intended: an
    accepted delta which has since been reworked deserves a fresh look.

    Args:
        lines (list of str): Lines of the hunk, including the '@@' header

    Return:
        str: Fingerprint, FP_LEN hex digits
    """
    body = [line for line in lines if line[:1] in '+-']
    text = '\n'.join(body)
    # A few files in the tree are not UTF-8 and reach here as surrogates
    raw = text.encode('utf-8', errors='surrogateescape')
    return hashlib.sha1(raw).hexdigest()[:FP_LEN]


def added_lines(start, lines):
    """Work out which downstream lines a hunk adds

    Args:
        start (int): First line of the hunk on the downstream side
        lines (list of str): Lines of the hunk, including the '@@' header

    Return:
        list of int: Downstream line numbers of the lines the hunk adds
    """
    added = []
    lineno = start
    for line in lines[1:]:
        if line.startswith('+'):
            added.append(lineno)
            lineno += 1
        elif line.startswith('-') or line.startswith('\\'):
            # A removed line takes up no space downstream; '\ No newline at
            # end of file' is a note about the line above, not a line itself
            pass
        else:
            lineno += 1
    return added


def parse_diff(diff):
    """Parse the output of 'git diff' into files and hunks

    Args:
        diff (str): Output from 'git diff', with no colour

    Return:
        list of FileDiff: One entry per file which differs
    """
    files = []
    path = None
    header = []
    hunks = []
    lines = None
    binary = False
    deleted = False

    def _flush_hunk():
        """Add the hunk being collected, if any, to the current file"""
        if lines:
            match = RE_HUNK_HEADER.match(lines[0])
            start = int(match.group(3))
            count = int(match.group(4)) if match.group(4) else 1
            hunks.append(Hunk(path, start, count, added_lines(start, lines),
                              list(lines), fingerprint(lines)))

    def _flush_file():
        """Add the file being collected, if any, to the result"""
        _flush_hunk()
        if path:
            files.append(FileDiff(path, list(header), list(hunks), binary,
                                  deleted))

    for line in diff.splitlines():
        match = RE_DIFF_HEADER.match(line)
        if match:
            _flush_file()
            path = match.group(2)
            header = [line]
            hunks = []
            lines = None
            binary = False
            deleted = False
        elif path is None:
            continue
        elif line.startswith('deleted file mode'):
            deleted = True
            header.append(line)
        elif line.startswith('Binary files ') or line.startswith('GIT binary '):
            binary = True
        elif RE_HUNK_HEADER.match(line):
            _flush_hunk()
            lines = [line]
        elif lines is not None:
            lines.append(line)
        else:
            header.append(line)

    _flush_file()
    return files


def read_accepts(text):
    """Parse the accept file

    Blank lines and lines starting with '#' are ignored.  Every other line is
    '<pattern> <fingerprint> <reason>', separated by whitespace, with the
    reason running to the end of the line.

    Args:
        text (str): Contents of the accept file

    Return:
        list of Accept: Entries, in file order

    Raises:
        ValueError: If a line is malformed
    """
    accepts = []
    for num, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        parts = line.split(None, 2)
        if len(parts) < 3:
            raise ValueError(
                f'{ACCEPT_FILE}:{num}: expected '
                f"'<path> <hunk> <reason>', got '{line}'")
        accepts.append(Accept(parts[0], parts[1], parts[2]))
    return accepts


def format_accepts(accepts):
    """Produce the text of the accept file

    The columns are padded so that the file stays readable as it grows, and
    the entries are sorted so that a new entry produces a small diff.

    Args:
        accepts (list of Accept): Entries to write

    Return:
        str: Contents for the accept file, ending in a newline
    """
    out = [
        f'# {ACCEPT_FILE}: deltas from upstream which are intentional',
        '#',
        '# Pickman reverts any delta from upstream which no downstream commit',
        '# accounts for.  Deltas listed here are exempt from that.',
        '#',
        '# <path>  <hunk>  <reason>',
        '#',
        "# <path> may be a glob.  <hunk> is a hunk fingerprint, or '*' for",
        '# every hunk in the file.  Run "pickman drift-accept" to add entries.',
        '',
    ]
    entries = sorted(accepts)
    width = max((len(ent.pattern) for ent in entries), default=0)
    width = max(width, 32)
    for ent in entries:
        out.append(f'{ent.pattern:<{width}}  {ent.fingerprint:<{FP_LEN}}  '
                   f'{ent.reason}')
    return '\n'.join(out) + '\n'


def match_accept(accepts, path, fprint):
    """Find the accept entry covering a hunk, if any

    Args:
        accepts (list of Accept): Entries from the accept file
        path (str): Path of the file the hunk is in
        fprint (str): Fingerprint of the hunk

    Return:
        Accept: Entry covering this hunk, or None if there is none
    """
    for ent in accepts:
        if ent.fingerprint not in (ALL_HUNKS, fprint):
            continue
        # A pattern ending in '/' covers everything below that directory
        if ent.pattern.endswith('/'):
            if path.startswith(ent.pattern):
                return ent
        elif fnmatch.fnmatch(path, ent.pattern):
            return ent
    return None


def classify(fdiff, accepts, blame=None):
    """Classify each hunk of a file as wanted, accepted or drift

    A hunk is wanted if a downstream-original commit accounts for it, accepted
    if the accept file exempts it, and drift otherwise.

    Where blame is available, a hunk which only removes lines is always
    treated as wanted, since blame can say who wrote a line but not who
    deleted one.  Calling such a hunk drift would revert a deliberate
    downstream deletion, resurrecting the upstream code it removed, so the
    doubt is resolved in favour of leaving the tree alone.  Nothing is lost:
    no downstream commit has touched a file classified without blame, so its
    deletions cannot be deliberate and are still caught.

    Args:
        fdiff (FileDiff): File to classify
        accepts (list of Accept): Entries from the accept file
        blame (dict): Maps downstream line number to the downstream-original
            commit which last touched that line, for the lines this file's
            hunks add; lines left by a cherry-pick are absent.  Pass None when
            no downstream-original commit has ever touched the file, which
            makes every hunk in it drift

    Return:
        list of Verdict: One entry per hunk, in file order
    """
    verdicts = []
    for hunk in fdiff.hunks:
        ent = match_accept(accepts, hunk.path, hunk.fingerprint)
        if ent:
            verdicts.append(Verdict(hunk, ACCEPTED, ent.reason))
        elif blame is None:
            verdicts.append(Verdict(hunk, DRIFT, None))
        elif not hunk.added:
            verdicts.append(Verdict(hunk, WANTED, 'deletion'))
        else:
            owner = None
            for line in hunk.added:
                owner = owner or blame.get(line)
            if owner:
                verdicts.append(Verdict(hunk, WANTED, owner))
            else:
                verdicts.append(Verdict(hunk, DRIFT, None))
    return verdicts


def build_patch(fdiffs, verdicts):
    """Build a patch which reverts the drift hunks

    The patch is expressed as upstream-to-downstream, the same direction as
    the diff it came from, so it must be applied in reverse to take the tree
    back towards upstream.  Line numbers are those of the downstream tree the
    diff was taken against, so the patch applies cleanly there.

    Args:
        fdiffs (list of FileDiff): Files which differ from upstream
        verdicts (dict): Maps path to the list of Verdict for that file

    Return:
        str: Patch text, empty if there is nothing to revert
    """
    out = []
    for fdiff in fdiffs:
        drift = [vdt.hunk for vdt in verdicts.get(fdiff.path, [])
                 if vdt.state == DRIFT]
        if not drift:
            continue
        out += fdiff.header
        for hunk in drift:
            out += hunk.lines
    if not out:
        return ''
    return '\n'.join(out) + '\n'


def group_by_area(paths):
    """Group paths by the area of the tree they sit in

    Drift is reverted an area at a time, so that each merge request stays
    small enough to review and a CI failure in one area does not hold up the
    rest.

    Args:
        paths (list of str): Paths which have drift

    Return:
        dict: Maps area name to the sorted list of paths in that area
    """
    areas = {}
    for path in paths:
        parts = path.split('/')
        if len(parts) == 1:
            area = parts[0]
        elif parts[0] in ('arch', 'board', 'drivers', 'test', 'tools', 'doc'):
            # These are large enough that the top level is too coarse
            area = '/'.join(parts[:2])
        else:
            area = parts[0]
        areas.setdefault(area, []).append(path)
    return {area: sorted(paths) for area, paths in sorted(areas.items())}
