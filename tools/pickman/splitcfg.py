# SPDX-License-Identifier: GPL-2.0+
#
# Copyright 2026 Simon Glass <sjg@chromium.org>
#
"""Adapt picked commits to a tree with a separate config for each phase

When each build phase (U-Boot proper, SPL, TPL, VPL) has its own config,
CONFIG_FOO means the value for the phase being built, so plain
IS_ENABLED(CONFIG_FOO) and obj-$(CONFIG_FOO) do the right thing everywhere.
The macros which used to pick the phase's option no longer exist:
CONFIG_IS_ENABLED(), CONFIG_VAL(), CONFIG_IF_ENABLED_INT(),
config_opt_enabled() and the $(PHASE_), $(XPL_), $(SPL_) and $(SPL_TPL_)
Makefile variables.

Commits picked from a tree without a split config still use them. This module
finds them in the lines a commit adds and converts them. It also finds the
options which such a commit keeps out of SPL without an SPL Kconfig option:
upstream, CONFIG_IS_ENABLED(FOO) is false in SPL when there is no
CONFIG_SPL_FOO, but here CONFIG_FOO takes U-Boot proper's value in SPL unless
FOO is listed in scripts/conf_nospl.

The functions here work on text, so the caller provides the git output.
"""

from collections import namedtuple
import os
import re

# The header which defines (or no longer defines) CONFIG_IS_ENABLED()
KCONFIG_H = 'include/linux/kconfig.h'

# Options which are never enabled in xPL, despite having no xPL variant
CONF_NOSPL = 'scripts/conf_nospl'

# Prefixes of the Kconfig options for each xPL phase
XPL_PREFIXES = ('SPL_', 'TPL_', 'VPL_')

# Files which mention the old macros on purpose, so are left alone
SKIP_PATHS = (
    '.pickman-history',
    'include/linux/kconfig.h',
    'scripts/basic/fixdep.c',
    'scripts/checkpatch.pl',
    'tools/pickman/',
    'tools/qconfig.py',
)

# Files which hold code, so are converted; anything else, such as
# documentation, is reported but left for a person to update
CODE_EXTS = ('.c', '.h', '.S', '.lds', '.dts', '.dtsi')
MAKE_NAMES = ('Makefile', 'Kbuild')

# An old-style construct found in a line added by a commit
#
# path (str): File path
# line (int): Line number in the new file
# kind (str): Construct found, e.g. 'CONFIG_IS_ENABLED()'
# option (str): Option it refers to (without CONFIG_), or None
# text (str): The added line
Finding = namedtuple('Finding', 'path,line,kind,option,text')

RE_CIE = re.compile(r'\bCONFIG_IS_ENABLED\((\s*)(?:CONFIG_)?([A-Z0-9_]+)')
RE_VAL = re.compile(r'\bCONFIG_VAL\(\s*([A-Z0-9_]+)\s*\)')
RE_IF_INT = re.compile(r'\bCONFIG_IF_ENABLED_INT\((\s*)(?:CONFIG_)?'
                       r'([A-Z0-9_]+)(\s*,\s*)(?:CONFIG_)?([A-Z0-9_]+)')
RE_COE = re.compile(r'\bconfig_opt_enabled\(\s*(?:CONFIG_)?([A-Z0-9_]+)?')
RE_MAKE = re.compile(r'CONFIG_\$\((?:PHASE_|XPL_|SPL_TPL_|SPL_)\)'
                     r'([A-Za-z0-9_]*)')

# Kinds of construct, in the order they are searched for
KINDS = (
    ('CONFIG_IS_ENABLED()', RE_CIE, 2),
    ('CONFIG_VAL()', RE_VAL, 1),
    ('CONFIG_IF_ENABLED_INT()', RE_IF_INT, 2),
    ('config_opt_enabled()', RE_COE, 1),
    ('CONFIG_$(PHASE_)', RE_MAKE, 1),
)

# Kinds whose option is kept out of xPL upstream when it has no xPL variant
PHASE_KINDS = ('CONFIG_IS_ENABLED()', 'CONFIG_IF_ENABLED_INT()',
               'config_opt_enabled()', 'CONFIG_$(PHASE_)')


def is_split(kconfig_h):
    """Check whether a tree has a separate config for each phase

    Args:
        kconfig_h (str): Contents of include/linux/kconfig.h

    Returns:
        bool: True if CONFIG_IS_ENABLED() is no longer defined
    """
    return not re.search(r'#\s*define\s+CONFIG_IS_ENABLED\b', kconfig_h)


def skip_path(path):
    """Check whether a file mentions the old macros on purpose

    Args:
        path (str): File path

    Returns:
        bool: True to leave the file alone
    """
    return any(path == skip or (skip.endswith('/') and path.startswith(skip))
               for skip in SKIP_PATHS)


def is_code(path):
    """Check whether a file holds code which can be converted

    Args:
        path (str): File path

    Returns:
        bool: True if the file is source code or a Makefile
    """
    name = os.path.basename(path)
    return (name.endswith(CODE_EXTS) or name.startswith(MAKE_NAMES) or
            name.endswith('.mk'))


def parse_added(diff):
    """Find the lines added by a diff

    Args:
        diff (str): Output of 'git diff', preferably with -U0

    Returns:
        list of tuple: (path, line, text) for each added line, where line is
            the line number in the new file
    """
    added = []
    path = None
    line = 0
    for text in diff.splitlines():
        if text.startswith('+++ '):
            path = text[6:] if text.startswith('+++ b/') else None
        elif text.startswith('@@'):
            match = re.match(r'@@ -\S+ \+(\d+)', text)
            line = int(match.group(1))
        elif path and text.startswith('+'):
            added.append((path, line, text[1:]))
            line += 1
        elif text.startswith(' '):
            line += 1
    return added


def find(added):
    """Find old-style constructs in added lines

    Args:
        added (list of tuple): (path, line, text) as returned by parse_added()

    Returns:
        list of Finding: Constructs found, in order
    """
    found = []
    for path, line, text in added:
        if skip_path(path):
            continue
        for kind, regex, group in KINDS:
            for match in regex.finditer(text):
                found.append(Finding(path, line, kind, match.group(group),
                                     text))
    return found


def get_variants(kconfig_grep):
    """Get the options which have an xPL variant

    Args:
        kconfig_grep (str): Output of grepping the Kconfig files for
            'config SPL_...', 'config TPL_...' and 'config VPL_...' lines

    Returns:
        set of str: Option names (without the phase prefix) which have a
            variant for at least one xPL phase
    """
    variants = set()
    for match in re.finditer(r'config\s+(?:SPL|TPL|VPL)_([A-Z0-9_]+)',
                             kconfig_grep):
        variants.add(match.group(1))
    return variants


def get_nospl(text):
    """Get the options listed in conf_nospl

    Args:
        text (str): Contents of scripts/conf_nospl

    Returns:
        set of str: Option names
    """
    return set(re.findall(r'^([A-Z0-9_]+)$', text, re.M))


def needs_nospl(found, variants, nospl):
    """Find options which need adding to conf_nospl

    Upstream, these are only enabled in U-Boot proper, since the code or
    Makefile asks for the phase's option and there is no xPL variant. Here,
    unless listed in conf_nospl, they take U-Boot proper's value in xPL too.

    Args:
        found (list of Finding): Constructs found
        variants (set of str): Options with an xPL variant
        nospl (set of str): Options already in conf_nospl

    Returns:
        list of str: Sorted option names
    """
    need = set()
    for item in found:
        if (item.kind in PHASE_KINDS and item.option and is_code(item.path)
                and not item.option.startswith(XPL_PREFIXES) and
                item.option not in variants and item.option not in nospl):
            need.add(item.option)
    return sorted(need)


def add_nospl(text, options):
    """Add options to conf_nospl, keeping the list sorted

    Args:
        text (str): Contents of scripts/conf_nospl
        options (list of str): Options to add

    Returns:
        str: New contents
    """
    lines = text.splitlines()
    first = next((i for i, line in enumerate(lines)
                  if re.fullmatch(r'[A-Z0-9_]+', line)), len(lines))
    body = [line for line in lines[first:] if line]
    body = sorted(set(body) | set(options))
    return '\n'.join(lines[:first] + body) + '\n'


def _split_args(text, start):
    """Split the arguments of a call, respecting brackets and strings

    Args:
        text (str): Text containing the call
        start (int): Position just after the opening bracket

    Returns:
        tuple: (list of str, int) giving the raw arguments and the position
            of the closing bracket, or (None, None) if it is not found
    """
    args = []
    depth = 0
    quote = None
    pos = start
    for pos in range(start, len(text)):
        char = text[pos]
        if quote:
            if char == quote and text[pos - 1] != '\\':
                quote = None
        elif char in '"\'':
            quote = char
        elif char in '([{':
            depth += 1
        elif char in ')]}' and depth:
            depth -= 1
        elif char == ')':
            args.append(text[start:pos])
            return args, pos
        elif char == ',' and not depth:
            args.append(text[start:pos])
            start = pos + 1
    return None, None


def _convert_coe(text):
    """Convert config_opt_enabled() calls to IS_ENABLED()

    config_opt_enabled(CONFIG_FOO, value, default) becomes
    IS_ENABLED(CONFIG_FOO, (value), (default)), keeping the layout of the
    arguments. A call which does not have three arguments is left alone.

    Args:
        text (str): Source text

    Returns:
        str: Converted text
    """
    out = []
    pos = 0
    for match in re.finditer(r'\bconfig_opt_enabled\(', text):
        if match.start() < pos:
            continue
        args, end = _split_args(text, match.end())
        if not args or len(args) != 3:
            continue
        wrapped = [args[0]]
        for arg in args[1:]:
            lead = arg[:len(arg) - len(arg.lstrip())]
            wrapped.append(f'{lead}({arg.strip()})')
        out.append(text[pos:match.start()])
        out.append(f"IS_ENABLED({','.join(wrapped)})")
        pos = end + 1
    out.append(text[pos:])
    return ''.join(out)


def convert(path, text):
    """Convert old-style constructs in a file

    Args:
        path (str): File path, used to decide whether the file is code
        text (str): File contents

    Returns:
        str: Converted contents, unchanged if the file is not code or is one
            which mentions the old macros on purpose
    """
    if skip_path(path) or not is_code(path):
        return text
    text = RE_CIE.sub(r'IS_ENABLED(\1CONFIG_\2', text)
    text = RE_VAL.sub(r'CONFIG_\1', text)
    text = RE_IF_INT.sub(r'IF_ENABLED_INT(\1CONFIG_\2\3CONFIG_\4', text)

    # A call nested in the arguments of another is converted on a later pass
    while True:
        new = _convert_coe(text)
        if new == text:
            break
        text = new
    return RE_MAKE.sub(r'CONFIG_\1', text)


def format_report(found, need):
    """Format a report of what was found

    Args:
        found (list of Finding): Constructs found
        need (list of str): Options which need adding to conf_nospl

    Returns:
        list of str: Lines of the report, empty if there is nothing to report
    """
    lines = []
    for item in found:
        where = f'{item.path}:{item.line}'
        if not is_code(item.path):
            where += ' (not code, so not converted)'
        lines.append(f'{where}: {item.kind} {item.option or ""}'.rstrip())
    if need:
        lines.append('Only enabled in U-Boot proper upstream, so need adding '
                     f"to {CONF_NOSPL}: {' '.join(need)}")
    return lines
