#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
#
# Copyright 2025 Canonical Ltd
#
"""Analyse C source code usage in U-Boot builds.

This script performs file-level and line-level analysis of U-Boot source code:
- File level: which files are compiled vs not compiled
- Line level: which lines within compiled files are active based on CONFIG_*

This combines file-level analysis (which files are used) with optional
line-level analysis: which parts of each file are active based on the
preprocessor and Kconfig options.
"""

import argparse
import fnmatch
import multiprocessing
import os
import re
import subprocess
import sys

# Allow 'from patman import xxx to work'
# pylint: disable=C0413
our_path = os.path.dirname(os.path.realpath(__file__))
sys.path.append(os.path.join(our_path, '..'))

# pylint: disable=wrong-import-position
from u_boot_pylib import terminal, tools, tout

# Import analysis modules
import dwarf
import lsp
import multiboard
import output
import unifdef
# pylint: enable=wrong-import-position

# Pattern to match .cmd files
RE_PATTERN = re.compile(r'^\..*\.cmd$')

# Pattern to extract the source file from a .cmd file
RE_LINE = re.compile(r'^(saved)?cmd_[^ ]*\.o := (?P<command_prefix>.* )'
                     r'(?P<file_path>[^ ]*\.[cS]) *(;|$)')
RE_SOURCE = re.compile(r'^source_[^ ]*\.o := (?P<file_path>[^ ]*\.[cS])')

# Directories to exclude from analysis
EXCLUDE_DIRS = ['.git', 'Documentation', 'doc', 'scripts', 'tools']

# Default base directory for builds
BUILD_BASE = '/tmp/cod'

# Database filename, stored in the source tree root
DB_NAME = 'codman.db'


def cmdfiles_in_dir(directory):
    """Generate paths to all .cmd files under the directory"""
    for dirpath, dirnames, filenames in os.walk(directory, topdown=True):
        dirnames = [d for d in dirnames if d not in EXCLUDE_DIRS]

        for filename in filenames:
            if RE_PATTERN.match(filename):
                yield os.path.join(dirpath, filename)


def extract_source_from_cmdfile(cmdfile_path, srcdir):
    """Extract the source file path from a .cmd file.

    Args:
        cmdfile_path (str): Path to the .cmd file to parse.
        srcdir (str): Root directory of the U-Boot source tree.
    """
    with open(cmdfile_path, 'rt', encoding='utf-8') as f:
        for line in f:
            result = RE_SOURCE.match(line)
            if result:
                file_path = result.group('file_path')
                abs_path = os.path.realpath(os.path.join(srcdir, file_path))
                if os.path.exists(abs_path):
                    return abs_path

            result = RE_LINE.match(line)
            if result:
                file_path = result.group('file_path')
                abs_path = os.path.realpath(os.path.join(srcdir, file_path))
                if os.path.exists(abs_path):
                    return abs_path

    return None


def find_all_source_files(srcdir):
    """Find all C/assembly/header source files in the source tree.

    Args:
        srcdir (str): Root directory of the U-Boot source tree.

    Returns:
        Set of absolute paths to all source files.
    """
    tout.progress('Finding all source files...')
    all_sources = set()
    exclude_dirs = [os.path.join(srcdir, d) for d in EXCLUDE_DIRS]

    for dirpath, dirnames, filenames in os.walk(srcdir, topdown=True):
        # Skip excluded directories
        if any(dirpath.startswith(excl) for excl in exclude_dirs):
            dirnames[:] = []
            continue

        for filename in filenames:
            if filename.endswith(('.c', '.S', '.h')):
                abs_path = os.path.realpath(os.path.join(dirpath, filename))
                all_sources.add(abs_path)

    tout.info(f'Found {len(all_sources)} total source files')

    return all_sources


def extract_deps_from_cmdfile(cmdfile_path):
    """Extract all source file dependencies from a .cmd file.

    This includes the main source file and all headers it depends on.

    Args:
        cmdfile_path (str): Path to the .cmd file to parse.

    Returns:
        Set of absolute paths to source files (c/S/h) used.
    """
    deps = set()

    with open(cmdfile_path, 'rt', encoding='utf-8') as f:
        in_deps_section = False
        for line in f:
            # Look for deps_* := lines
            if line.startswith('deps_'):
                in_deps_section = True
                continue

            # If we're in the deps section, extract file paths
            if in_deps_section:
                # Lines look like: /path/to/file.h \
                # or: $(wildcard include/config/foo.h) \
                if line.strip() == '':
                    in_deps_section = False
                    continue

                # Skip wildcard lines
                if '$(wildcard' in line:
                    continue

                # Extract the file path
                path = line.strip().rstrip('\\').strip()
                if path and os.path.exists(path):
                    abs_path = os.path.realpath(path)
                    # Only include .c, .S, .h files
                    if abs_path.endswith(('.c', '.S', '.h')):
                        deps.add(abs_path)

    return deps


def resolve_wrapper_file(source_file):
    """Check if a file is a wrapper that only includes another .c file.

    For example lib/libfdt/fdt_overlay.c which holds:
        #include <linux/libfdt_env.h>
        #include "../../scripts/dtc/libfdt/fdt_overlay.c"

    Args:
        source_file (str): Path to the source file

    Returns:
        str: Path to the included .c file if this is a wrapper, else the
            original file
    """
    lines = tools.read_file(source_file, binary=False).splitlines()

    # Check if file only has #include directives (and comments/blank lines)
    included_c_file = None
    has_other_content = False

    for line in lines:
        stripped = line.strip()
        # Skip blank lines and comments
        if not stripped or stripped.startswith('//') or \
           stripped.startswith('/*') or stripped.startswith('*'):
            continue

        # Check for #include directive
        if stripped.startswith('#include'):
            # Extract the included file
            match = re.search(r'#include\s+[<"]([^>"]+)[>"]', stripped)
            if match:
                included = match.group(1)
                # Only track .c file includes (the actual source)
                if included.endswith('.c'):
                    included_c_file = included
            continue

        # Found non-include content
        has_other_content = True
        break

    # If we only found includes and one was a .c file, resolve it
    if not has_other_content and included_c_file:
        # Resolve relative to the wrapper file's directory
        wrapper_dir = os.path.dirname(source_file)
        resolved = os.path.realpath(
            os.path.join(wrapper_dir, included_c_file))
        if os.path.exists(resolved):
            return resolved

    return source_file


def _process_cmdfile(args):
    """Process a single .cmd file to extract source files.

    This is a worker function for multiprocessing.

    Args:
        args: Tuple of (cmdfile_path, srcdir, srcdir_real)

    Returns:
        set: Set of absolute paths to source files found in this .cmd file
    """
    cmdfile, srcdir, srcdir_real = args
    sources = set()

    # Get the main source file (.c or .S)
    source_file = extract_source_from_cmdfile(cmdfile, srcdir)
    if source_file:
        # Resolve wrapper files to their actual source
        resolved = resolve_wrapper_file(source_file)
        # Only include files within the source tree
        if os.path.realpath(resolved).startswith(srcdir_real):
            sources.add(resolved)

    # Get all dependencies (headers)
    deps = extract_deps_from_cmdfile(cmdfile)
    # Filter to only include files within the source tree
    for dep in deps:
        if os.path.realpath(dep).startswith(srcdir_real):
            sources.add(dep)

    return sources


def find_used_sources(build_dir, srcdir, jobs=None):
    """Find all source files used in the build.

    This includes both the compiled .c/.S files and all .h headers they depend
    on. For wrapper files that only include another .c file, the included file
    is returned instead.

    Only files within the source tree are included - system headers and
    toolchain files are excluded.

    Args:
        build_dir (str): Path to the build directory containing .cmd files
        srcdir (str): Path to U-Boot source root directory
        jobs (int): Number of parallel jobs (None = use all CPUs)

    Returns:
        set: Set of absolute paths to all source files used in the build
    """
    tout.progress('Finding used source files...')
    srcdir_real = os.path.realpath(srcdir)

    # Collect all cmdfiles first
    cmdfiles = list(cmdfiles_in_dir(build_dir))
    tout.progress(f'Processing {len(cmdfiles)} .cmd files...')

    # Prepare arguments for each worker
    worker_args = [(cmdfile, srcdir, srcdir_real) for cmdfile in cmdfiles]

    # Use multiprocessing to process cmdfiles in parallel
    if jobs is None:
        jobs = multiprocessing.cpu_count()

    used_sources = set()
    if jobs <= 1:
        # Sequential - safe to call from worker threads
        for args in worker_args:
            used_sources.update(_process_cmdfile(args))
    else:
        with multiprocessing.Pool(processes=jobs) as pool:
            for sources in pool.imap_unordered(_process_cmdfile, worker_args,
                                               chunksize=100):
                used_sources.update(sources)

    tout.info(f'Found {len(used_sources)} used source files')

    return used_sources


def select_sources(srcdir, build_dir, filter_pattern, jobs=None):
    """Find all and used source files, optionally applying a filter.

    Args:
        srcdir (str): Root directory of the source tree
        build_dir (str): Build directory path
        filter_pattern (str): Optional wildcard pattern to filter files
            (None to skip)
        jobs (int): Number of parallel jobs (None = use all CPUs)

    Returns:
        tuple: (all_sources, used_sources, skipped_sources) - sets of file paths
    """
    all_sources = find_all_source_files(srcdir)

    # Find used source files
    used_sources = find_used_sources(build_dir, srcdir, jobs)

    # Apply filter if specified
    if filter_pattern:
        all_sources = {f for f in all_sources
                       if fnmatch.fnmatch(os.path.basename(f),
                                          filter_pattern) or
                          fnmatch.fnmatch(f, filter_pattern)}
        used_sources = {f for f in used_sources
                        if fnmatch.fnmatch(os.path.basename(f),
                                           filter_pattern) or
                           fnmatch.fnmatch(f, filter_pattern)}
        tout.progress(f'After filter: {len(all_sources)} total, ' +
                     f'{len(used_sources)} used')

    # Calculate unused sources
    skipped_sources = all_sources - used_sources

    return all_sources, used_sources, skipped_sources


def do_build(args):
    """Set up and validate source and build directories.

    Args:
        args (Namespace): Parsed command-line arguments

    Returns:
        tuple: (srcdir, build_dir) on success
        Calls tout.fatal() on failure
    """
    srcdir = os.path.realpath(args.source)

    if not os.path.isdir(srcdir):
        tout.fatal(f'Source directory does not exist: {srcdir}')

    # Determine build directory
    if args.build_dir:
        build_dir = os.path.realpath(args.build_dir)
    else:
        # Use default: build_base/<board>
        build_dir = os.path.join(args.build_base, args.board)

    # If not skipping build, build it
    if not args.no_build:
        if args.board:
            build_board(args.board, build_dir, srcdir, args.adjust,
                        args.use_dwarf)
            # Note: build_board() calls tout.fatal() on failure which exits

    # Verify build directory exists
    if not os.path.isdir(build_dir):
        tout.fatal(f'Build directory does not exist: {build_dir}')

    tout.info(f'Analysing build in: {build_dir}')
    tout.info(f'Source directory: {srcdir}')

    return srcdir, build_dir


def build_board(board, build_dir, srcdir, adjust_cfg=None, use_dwarf=False,
                fatal_on_error=True, make_jobs=None):
    """Build a board using buildman.

    Args:
        board (str): Board name to build
        build_dir (str): Directory to build into
        srcdir (str): U-Boot source directory
        adjust_cfg (list): List of CONFIG adjustments
        use_dwarf (bool): Enable CC_OPTIMIZE_FOR_DEBUG to prevent inlining
        fatal_on_error (bool): If True (default), call tout.fatal() on
            failure (which exits). If False, return False on failure.
        make_jobs (int): Number of make -j jobs (None = buildman default)

    Returns:
        True on success, False on failure (only when fatal_on_error=False)
    """
    tout.info(f"Building board '{board}' with buildman...")
    tout.info(f'Build directory: {build_dir}')

    # Enable CC_OPTIMIZE_FOR_DEBUG if using DWARF to prevent inlining
    if use_dwarf:
        adjust_cfg = list(adjust_cfg or []) + ['CC_OPTIMIZE_FOR_DEBUG']

    if adjust_cfg:
        # Count actual adjustments (handle comma-separated values)
        num_adjustments = sum(len([x for x in item.split(',') if x.strip()])
                              for item in adjust_cfg)
        tout.progress(f'Building with {num_adjustments} Kconfig adjustments')
    else:
        tout.progress('Building')

    # Run buildman to build the board
    # -L: disable LTO, -w: enable warnings, -o: output directory,
    # -m: mrproper (clean), -I: show errors/warnings only (incremental)
    cmd = ['buildman', '--board', board, '-L', '-w', '-m', '-I', '-o',
           build_dir]

    # Limit per-board make parallelism when running multiple boards
    if make_jobs:
        cmd.extend(['-j', str(make_jobs)])

    # Add CONFIG adjustments if specified
    if adjust_cfg:
        for adj in adjust_cfg:
            cmd.extend(['--adjust-cfg', adj])

    # In scan mode (fatal_on_error=False), run in a new session so Ctrl-C
    # does not propagate directly to buildman and its children, and capture
    # output so interrupted subprocesses don't spam the terminal.
    new_session = not fatal_on_error
    capture = not fatal_on_error

    try:
        result = subprocess.run(cmd, cwd=srcdir, check=False,
                              capture_output=capture, text=True,
                              start_new_session=new_session)
        if result.returncode != 0:
            if fatal_on_error:
                tout.fatal(f'buildman exited with code {result.returncode}')
            tout.error(f'buildman exited with code {result.returncode}')
            return False
        return True
    except FileNotFoundError:
        if fatal_on_error:
            tout.fatal('buildman not found. Please ensure buildman is in '
                       'your PATH.')
        tout.error('buildman not found')
        return False
    except OSError as e:
        if fatal_on_error:
            tout.fatal(f'Error running buildman: {e}')
        tout.error(f'Error running buildman: {e}')
        return False


def parse_args(argv=None):
    """Parse command-line arguments.

    Returns:
        Parsed arguments object
    """
    parser = argparse.ArgumentParser(
        description='Analyse C source code usage in U-Boot builds',
        epilog='Example: %(prog)s -b sandbox --stats')

    parser.add_argument('-s', '--source', type=str, default='.',
                        help='Path to U-Boot source directory '
                             '(default: current directory)')
    parser.add_argument('-b', '--board', type=str, default='sandbox',
                        help='Board name to build and analyse (default: sandbox)')
    parser.add_argument('-B', '--build-dir', type=str,
                        help='Use existing build directory instead of building')
    parser.add_argument('--build-base', type=str, default=BUILD_BASE,
                        help=f'Base directory for builds (default: {BUILD_BASE})')
    parser.add_argument('-n', '--no-build', action='store_true',
                        help='Skip building, use existing build directory')
    parser.add_argument('-a', '--adjust', type=str, action='append',
                        help='Adjust CONFIG options '
                             '(e.g., -a CONFIG_FOO, -a ~CONFIG_BAR)')
    parser.add_argument('-w', '--dwarf', action='store_true',
                        dest='use_dwarf',
                        help='Use DWARF debug info '
                             '(more accurate, requires rebuild)')
    parser.add_argument('-l', '--lsp', action='store_true',
                        dest='use_lsp',
                        help='Use clangd LSP to analyse inactive regions '
                             '(requires clangd)')
    parser.add_argument('-u', '--unifdef', type=str, default='unifdef',
                        help='Path to unifdef executable (default: unifdef)')
    parser.add_argument('-j', '--jobs', type=int, metavar='N',
                        help='Number of parallel jobs (default: all CPUs)')
    parser.add_argument('-i', '--include-headers', action='store_true',
                        help='Include header files in unifdef analysis')
    parser.add_argument('-f', '--filter', type=str, metavar='PATTERN',
                        help='Filter files by wildcard pattern (e.g., *acpi*)')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Show verbose output')
    parser.add_argument('-D', '--debug', action='store_true',
                        help='Enable debug mode')

    # Subcommands
    subparsers = parser.add_subparsers(dest='cmd', help='Command to execute')

    # stats command (default)
    stats = subparsers.add_parser('stats',
                                   help='Show statistics about code usage')
    stats.add_argument('--top', type=int, metavar='N', default=20,
                       help='Show top N files with most inactive code '
                            '(default: 20)')

    # dirs command
    dirs = subparsers.add_parser('dirs', help='Show directory breakdown')
    dirs.add_argument('-s', '--subdirs', action='store_true',
                      help='Show breakdown by all subdirectories')
    dirs.add_argument('-f', '--show-files', action='store_true',
                      help='Show individual files within directories')
    dirs.add_argument('-e', '--show-empty', action='store_true',
                      help='Show directories with 0 lines used')
    dirs.add_argument('-k', '--kloc', action='store_true',
                      help='Show line counts in kilolines (kLOC) instead of lines')
    dirs.add_argument('--html', type=str, metavar='FILE',
                      help='Output results as HTML to the specified file')
    dirs.add_argument('--csv', type=str, metavar='FILE',
                      help='Output results as CSV to the specified file')
    dirs.add_argument('-u', '--show-unmatched', action='store_true',
                      help='List all files without a category match')
    dirs.add_argument('-F', '--files-only', action='store_true',
                      help='Only output file rows in CSV (exclude directories)')
    dirs.add_argument('-E', '--show-empty-features', action='store_true',
                      help='List features with no files defined')

    # detail command
    detail = subparsers.add_parser('detail',
                                    help='Show line-by-line analysis of files')
    detail.add_argument('files', nargs='+', metavar='FILE',
                        help='File(s) to analyse')

    # unused command
    subparsers.add_parser('unused', help='List all unused source files')

    # used command
    subparsers.add_parser('used', help='List all used source files')

    # summary command
    subparsers.add_parser('summary',
                          help='Show per-file summary of active/inactive lines')

    # copy-used command
    copy = subparsers.add_parser('copy-used',
                                  help='Copy used source files to a directory')
    copy.add_argument('dest_dir', metavar='DIR',
                      help='Destination directory')

    # scan command - multi-board build + analyse + store
    scan = subparsers.add_parser(
        'scan',
        help='Build and analyse multiple boards, store results in database')
    scan.add_argument('board_specs', nargs='*', metavar='SPEC',
                      help='Buildman board specifiers '
                           '(e.g., arm sandbox "qemu*"). '
                           'If empty, all boards are scanned.')
    scan.add_argument('--resume', action='store_true',
                      help='Skip boards already in the database')
    scan.add_argument('--max-boards', type=int, metavar='N',
                      help='Limit to first N matching boards (for testing)')
    scan.add_argument('--exclude', type=str, action='append',
                      help='Board specifiers to exclude')
    scan.add_argument('-W', '--workers', type=int, metavar='N',
                      help='Number of boards to build in parallel '
                           '(default: CPU count)')
    scan.add_argument('--clean-after', action='store_true',
                      help='Delete build directory after analysing each board')

    # query command - database queries
    query = subparsers.add_parser(
        'query',
        help='Query the multi-board analysis database')
    query.add_argument('--format', choices=['table', 'csv', 'count'],
                       default='table',
                       help='Output format (default: table)')
    query.add_argument('--arch', type=str,
                       help='Filter results by architecture')

    query_sub = query.add_subparsers(dest='query_cmd',
                                     help='Query type')

    # query file - which boards compile a file
    qf = query_sub.add_parser(
        'file', help='Which boards compile a given file')
    qf.add_argument('path', help='File path (supports wildcards)')

    # query line - which boards have a line active
    ql = query_sub.add_parser(
        'line', help='Which boards have a specific line active')
    ql.add_argument('location',
                    help='File path and line number '
                         '(e.g., common/main.c:42)')

    # query board - what files a board compiles
    qb = query_sub.add_parser(
        'board', help='What files a given board compiles')
    qb.add_argument('target', help='Board target name')
    qb.add_argument('file_pattern', nargs='?',
                    help='Optional file pattern filter')

    # query unique - code unique to a board
    qu = query_sub.add_parser(
        'unique',
        help='Find code unique to a board (not compiled by any other)')
    qu.add_argument('target', help='Board target name')

    # query function - which boards build a function
    qfn = query_sub.add_parser(
        'function',
        help='Which boards build a given function')
    qfn.add_argument('name', help='Function name to search for')
    qfn.add_argument('path', nargs='?',
                     help='Optional file path to restrict search')

    # query info - database statistics
    query_sub.add_parser('info', help='Show database statistics')

    args = parser.parse_args(argv)

    # Default command is stats
    if not args.cmd:
        args.cmd = 'stats'
        # Set default value for --top when stats is the default command
        args.top = 20

    # Map subcommand arguments to expected names
    if args.cmd == 'detail':
        args.detail = args.files
    elif args.cmd == 'copy-used':
        args.copy_used = args.dest_dir
    else:
        args.detail = None
        args.copy_used = None

    # Validation
    if args.no_build and args.adjust:
        tout.warning('-a/--adjust ignored when using -n/--no-build')

    return args


def do_analysis(used, build_dir, srcdir, unifdef_path, include_headers, jobs,
                use_lsp, keep_temps=False):
    """Perform line-level analysis if requested.

    Args:
        used (set): Set of used source files
        build_dir (str): Build directory path
        srcdir (str): Source directory path
        unifdef_path (str): Path to unifdef executable (None to use DWARF/LSP)
        include_headers (bool): Include header files in unifdef analysis
        jobs (int): Number of parallel jobs
        use_lsp (bool): Use LSP (clangd) instead of DWARF
        keep_temps (bool): If True, keep temporary files for debugging

    Returns:
        tuple: (analysis_results, analysis_method) where analysis_method is
            'unifdef', 'lsp', or 'dwarf'
    """
    if unifdef_path:
        config_file = os.path.join(build_dir, '.config')
        analyser = unifdef.UnifdefAnalyser(config_file, srcdir, used,
                                            unifdef_path, include_headers,
                                            keep_temps)
        method = 'unifdef'
    elif use_lsp:
        analyser = lsp.LspAnalyser(build_dir, srcdir, used, keep_temps)
        method = 'lsp'
    else:
        analyser = dwarf.DwarfAnalyser(build_dir, srcdir, used, keep_temps)
        method = 'dwarf'
    return analyser.process(jobs), method


def do_output(args, all_srcs, used, skipped, results, srcdir, analysis_method):
    """Perform output operation based on command.

    Args:
        args (argparse.Namespace): Parsed command-line arguments
        all_srcs (set): All source files
        used (set): Used source files
        skipped (set): Unused source files
        results (dict): Line-level analysis results (or None)
        srcdir (str): Source directory path
        analysis_method (str): Analysis method used ('unifdef', 'lsp', or 'dwarf')

    Returns:
        bool: True on success, False on failure
    """
    terminal.print_clear()

    # Execute the command
    if args.cmd == 'detail':
        # Show detail for each file, collecting missing files
        missing = []
        shown = 0
        for fname in args.detail:
            if output.show_file_detail(fname, results, srcdir):
                shown += 1
            else:
                missing.append(fname)

        # Show summary if any files were missing
        if missing:
            tout.warning(f'{len(missing)} file(s) not found in analysed '
                         f"sources: {', '.join(missing)}")

        ok = shown > 0
    elif args.cmd == 'summary':
        ok = output.show_file_summary(results, srcdir)
    elif args.cmd == 'unused':
        ok = output.list_unused_files(skipped, srcdir)
    elif args.cmd == 'used':
        ok = output.list_used_files(used, srcdir)
    elif args.cmd == 'copy-used':
        ok = output.copy_used_files(used, srcdir, args.copy_used)
    elif args.cmd == 'dirs':
        # Check if HTML or CSV output is requested
        html_file = getattr(args, 'html', None)
        csv_file = getattr(args, 'csv', None)
        if html_file:
            ok = output.generate_html_breakdown(all_srcs, used, results, srcdir,
                                                args.subdirs, args.show_files,
                                                args.show_empty,
                                                getattr(args, 'kloc', False),
                                                html_file, args.board,
                                                analysis_method)
        elif csv_file:
            ok = output.generate_csv(
                all_srcs, used, results, srcdir, args.subdirs,
                args.show_files, args.show_empty,
                getattr(args, 'kloc', False), csv_file,
                getattr(args, 'show_unmatched', False),
                getattr(args, 'files_only', False),
                getattr(args, 'show_empty_features', False))
        else:
            ok = output.show_dir_breakdown(all_srcs, used, results, srcdir,
                                            args.subdirs, args.show_files,
                                            args.show_empty,
                                            getattr(args, 'kloc', False))
    else:
        # stats (default)
        ok = output.show_statistics(all_srcs, used, skipped, results, srcdir,
                                     args.top)

    return ok


def main(argv=None):
    """Main function.

    Args:
        argv (list): Command-line arguments (default: sys.argv[1:])

    Returns:
        int: Exit code (0 for success, 1 for failure)
    """
    tout.init(tout.NOTICE)
    args = parse_args(argv)

    # Init tout based on verbosity flags
    if args.debug:
        tout.init(tout.DEBUG)
    elif args.verbose:
        tout.init(tout.INFO)

    # Dispatch scan and query commands separately
    if args.cmd == 'scan':
        return multiboard.do_scan(args)
    if args.cmd == 'query':
        return multiboard.do_query(args)

    srcdir, build_dir = do_build(args)
    all_srcs, used, skipped = select_sources(srcdir, build_dir, args.filter,
                                              args.jobs)

    # Determine which files to analyse
    files_to_analyse = used
    if args.cmd == 'detail':
        # For detail command, only analyse the requested files
        files_to_analyse = set()
        for fname in args.detail:
            abs_path = os.path.realpath(os.path.join(srcdir, fname))
            if abs_path in used:
                files_to_analyse.add(abs_path)

    # Perform line-level analysis
    unifdef_path = None if (args.use_dwarf or args.use_lsp) else args.unifdef
    keep_temps = args.debug
    results, analysis_method = do_analysis(files_to_analyse, build_dir, srcdir,
                                            unifdef_path, args.include_headers,
                                            args.jobs, args.use_lsp, keep_temps)
    if results is None:
        return 1

    if not do_output(args, all_srcs, used, skipped, results, srcdir,
                     analysis_method):
        return 1

    return 0


if __name__ == '__main__':
    sys.exit(main())
