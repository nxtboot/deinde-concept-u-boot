# SPDX-License-Identifier: GPL-2.0
#
# Copyright 2025 Canonical Ltd
#
"""Multi-board scan and query operations for codman.

This module handles building and analysing multiple boards in parallel,
storing results in a SQLite database, and querying the database to answer
cross-board questions such as "which boards compile line X of file Y?"
"""

from concurrent import futures
import multiprocessing
import os
import re
import signal
import shutil
import subprocess
import sys
import threading
import time

# Allow imports from parent directory
our_path = os.path.dirname(os.path.realpath(__file__))
sys.path.append(os.path.join(our_path, '..'))

# pylint: disable=wrong-import-position
from u_boot_pylib import terminal, tout

import codman
import database
import output
# pylint: enable=wrong-import-position


def load_board_list(srcdir, board_specs, exclude=None, jobs=None):
    """Load and select boards using buildman's board selection.

    Args:
        srcdir (str): U-Boot source directory
        board_specs (list): Buildman board specifiers (empty = all boards)
        exclude (list): Board specifiers to exclude
        jobs (int): Number of parallel jobs for board list generation

    Returns:
        list: List of Board objects selected
    """
    from buildman import boards as bm_boards

    brds = bm_boards.Boards()

    # Generate/read the board list
    board_file = os.path.join(srcdir, 'boards.cfg')
    tout.progress('Loading board list...')
    brds.ensure_board_list(board_file, jobs or multiprocessing.cpu_count(),
                           force=False, quiet=True)
    brds.read_boards(board_file)

    # Select boards based on specifiers
    _why, warnings = brds.select_boards(board_specs or [], exclude)
    for warn in warnings:
        tout.warning(warn)

    selected = brds.get_selected()
    tout.info(f'Selected {len(selected)} boards')
    return selected


def get_db_path(srcdir):
    """Get the path to the codman database for a source tree.

    Args:
        srcdir (str): Source directory root

    Returns:
        str: Absolute path to the database file
    """
    return os.path.join(srcdir, codman.DB_NAME)


def _get_git_hash(srcdir):
    """Get the current git HEAD hash for a source tree.

    Args:
        srcdir (str): Source directory root

    Returns:
        str: Git hash string, or empty string on failure
    """
    try:
        result = subprocess.run(
            ['git', 'rev-parse', 'HEAD'], cwd=srcdir,
            capture_output=True, text=True, check=False)
        if result.returncode == 0:
            return result.stdout.strip()
    except OSError:
        pass
    return ''


def _scan_one_board(brd, srcdir, build_base, adjust_cfg, use_dwarf,
                    use_lsp, unifdef_cmd, include_headers,
                    analysis_jobs, filter_pattern, clean_after,
                    isolate=False, make_jobs=1, use_threads=False):
    """Build, analyse, and return results for a single board.

    Does not touch the database — results are returned for the caller
    to store. KeyboardInterrupt is not caught here so it can propagate
    to the caller's handler.

    Args:
        brd: Board object from buildman
        srcdir (str): Source directory root
        build_base (str): Base build directory
        adjust_cfg (list): CONFIG adjustments
        use_dwarf (bool): Use DWARF analysis
        use_lsp (bool): Use LSP analysis
        unifdef_cmd (str): Path to unifdef
        include_headers (bool): Include headers in analysis
        analysis_jobs (int): Number of parallel analysis jobs
        filter_pattern (str): File filter pattern
        clean_after (bool): Delete build dir after analysis
        isolate (bool): Isolate buildman in its own session
        make_jobs (int): Number of make -j jobs for the build
        use_threads (bool): Run the line-level analysis with threads rather
            than a process pool (required when called from a worker thread)

    Returns:
        tuple: (brd, status, results_or_None) where status is 'ok',
            'build_failed', or 'analysis_failed'
    """
    build_dir = os.path.join(build_base, brd.target)

    if not codman.build_board(brd.target, build_dir, srcdir, adjust_cfg,
                              use_dwarf, fatal_on_error=False,
                              make_jobs=make_jobs, isolate=isolate):
        return (brd, 'build_failed', None)

    # Find used sources
    try:
        _all, used, _skip = codman.select_sources(
            srcdir, build_dir, filter_pattern, analysis_jobs)
    except Exception as e:
        tout.error(f'{brd.target}: source selection failed: {e}')
        return (brd, 'analysis_failed', None)

    # Run line-level analysis
    unifdef_path = None
    if not (use_dwarf or use_lsp):
        unifdef_path = unifdef_cmd
    results, _method = codman.do_analysis(
        used, build_dir, srcdir, unifdef_path,
        include_headers, analysis_jobs, use_lsp, use_threads=use_threads)

    if results is None:
        return (brd, 'analysis_failed', None)

    # Clean up build directory if requested
    if clean_after:
        shutil.rmtree(build_dir, ignore_errors=True)

    return (brd, 'ok', results)


def _init_scan_db(srcdir, args):
    """Create and initialise the scan database.

    Args:
        srcdir (str): Source directory root
        args (Namespace): Parsed command-line arguments

    Returns:
        tuple: (CodmanDatabase, db_path, selected_boards) or None on error
    """
    selected = load_board_list(srcdir, args.board_specs,
                               args.exclude, args.jobs)
    if not selected:
        tout.error('No boards selected')
        return None

    if args.max_boards:
        selected = selected[:args.max_boards]

    db_path = get_db_path(srcdir)

    # The database is kept across runs: each board replaces its own rows as it
    # is re-scanned (see _store_board_result), so scanning some boards leaves
    # the others untouched rather than wiping the whole database.
    db = database.CodmanDatabase(db_path)
    db.create_tables()

    db.set_scan_info('srcdir', srcdir)
    db.set_scan_info('git_hash', _get_git_hash(srcdir))
    db.set_scan_info('scan_start', str(time.time()))

    # Filter out already-completed boards if --resume
    if args.resume:
        completed = db.get_completed_targets()
        before = len(selected)
        selected = [b for b in selected if b.target not in completed]
        skipped = before - len(selected)
        if skipped:
            terminal.tprint(f'Resuming: skipping {skipped} already-scanned '
                            f'boards, {len(selected)} remaining')

    return db, db_path, selected


def _store_board_result(db, brd, status, results, srcdir):
    """Store the scan result for one board in the database.

    Args:
        db (CodmanDatabase): Database instance
        brd: Board object from buildman
        status (str): 'ok', 'build_failed', or 'analysis_failed'
        results (dict): Analysis results (None for failed boards)
        srcdir (str): Source directory root
    """
    # Replace any existing data for this board (e.g. from a previous scan).
    # Done here, as the new result is stored, so an interrupted scan never
    # loses a board it has not got round to re-scanning yet.
    db.delete_board(brd.target)

    defconfig = f'{brd.target}_defconfig'
    if status != 'ok':
        db.add_board(brd.target, brd.arch, brd.cpu, brd.soc,
                     brd.vendor, brd.board_name, defconfig,
                     status=status)
        return

    board_id = db.add_board(brd.target, brd.arch, brd.cpu, brd.soc,
                            brd.vendor, brd.board_name, defconfig)
    db.store_board_results(board_id, results, srcdir)


def _kill_children():
    """Kill all child processes of the current process.

    Uses /proc on Linux to find children without external dependencies.
    Sends SIGTERM first, then SIGKILL after a brief pause.
    """
    pid = os.getpid()
    children = []
    try:
        for entry in os.listdir('/proc'):
            if not entry.isdigit():
                continue
            try:
                with open(f'/proc/{entry}/stat', 'r') as f:
                    parts = f.read().split()
                    if len(parts) > 3 and int(parts[3]) == pid:
                        children.append(int(entry))
            except (IOError, ValueError):
                continue
    except OSError:
        return

    for child in children:
        try:
            # Kill the entire process group of each child session
            os.killpg(os.getpgid(child), signal.SIGTERM)
        except (OSError, ProcessLookupError):
            pass


def _scan_sequential(selected, srcdir, build_base, adjust_cfg, use_dwarf,
                     use_lsp, unifdef_cmd, include_headers, filter_pattern,
                     clean_after, db, scan_start, make_jobs):
    """Scan boards sequentially without threading.

    Useful for debugging since all work happens in the main process. Only one
    board builds at a time, so the build uses make_jobs (typically every core).

    Returns:
        tuple: (ok_count, fail_count)
    """
    terminal.tprint(
        f'Scanning {len(selected)} boards (sequential, -j{make_jobs})...')
    ok_count = 0
    fail_count = 0

    try:
        for i, brd in enumerate(selected):
            elapsed = time.time() - scan_start
            if i > 0:
                per_board = elapsed / i
                remaining = per_board * (len(selected) - i)
                eta = f', ETA {_format_duration(remaining)}'
            else:
                eta = ''

            terminal.tprint(
                f'[{i + 1}/{len(selected)}] {brd.target} '
                f'({_format_duration(elapsed)}{eta})')

            brd, status, results = _scan_one_board(
                brd, srcdir, build_base, adjust_cfg, use_dwarf,
                use_lsp, unifdef_cmd, include_headers,
                make_jobs, filter_pattern, clean_after,
                make_jobs=make_jobs)

            _store_board_result(db, brd, status, results, srcdir)

            if status == 'ok':
                ok_count += 1
            else:
                fail_count += 1
    except KeyboardInterrupt:
        terminal.tprint(f'\nInterrupted. {ok_count} boards completed, '
                        f'{fail_count} failed.')

    return ok_count, fail_count


def _scan_parallel(selected, srcdir, build_base, adjust_cfg, use_dwarf,
                   use_lsp, unifdef_cmd, include_headers, filter_pattern,
                   clean_after, db, scan_start, workers, make_jobs):
    """Scan boards in parallel using a thread pool.

    Each worker builds and analyses one board with make_jobs jobs, chosen so
    that workers * make_jobs fills the CPU. Results are returned to the main
    thread for database storage.

    Returns:
        tuple: (ok_count, fail_count)
    """
    terminal.tprint(
        f'Scanning {len(selected)} boards ({workers} workers, '
        f'-j{make_jobs} each)...')
    ok_count = 0
    fail_count = 0
    done_count = 0
    count_lock = threading.Lock()
    # Analyse with the same job count as the build, using threads
    # (use_threads=True below) so the analysers parallelise without forking a
    # process pool from these worker threads.
    analysis_jobs = make_jobs

    # Install a signal handler that kills all children and exits
    # immediately. This avoids the problem of as_completed() blocking
    # while killed buildman processes dump error messages.
    orig_handler = signal.getsignal(signal.SIGINT)

    def _sigint_handler(_signum, _frame):
        _kill_children()
        terminal.tprint(f'\nInterrupted. {ok_count} boards completed, '
                        f'{fail_count} failed.')
        db.close()
        os._exit(1)

    signal.signal(signal.SIGINT, _sigint_handler)

    with futures.ThreadPoolExecutor(max_workers=workers) as executor:
        future_map = {}
        for brd in selected:
            fut = executor.submit(
                _scan_one_board, brd, srcdir, build_base,
                adjust_cfg, use_dwarf, use_lsp, unifdef_cmd,
                include_headers, analysis_jobs,
                filter_pattern, clean_after, isolate=True,
                make_jobs=make_jobs, use_threads=True)
            future_map[fut] = brd

        for fut in futures.as_completed(future_map):
            try:
                brd, status, results = fut.result()
            except Exception as e:
                # Don't drop a board silently; record it as failed
                brd = future_map[fut]
                tout.warning(f'{brd.target}: scan worker failed: {e}')
                _store_board_result(db, brd, 'build_failed', None, srcdir)
                with count_lock:
                    done_count += 1
                    fail_count += 1
                continue

            _store_board_result(db, brd, status, results, srcdir)

            with count_lock:
                done_count += 1
                if status == 'ok':
                    ok_count += 1
                else:
                    fail_count += 1
                n = done_count

            elapsed = time.time() - scan_start
            per_board = elapsed / n
            remaining = per_board * (len(selected) - n)
            terminal.tprint(
                f'[{n}/{len(selected)}] {brd.target}: {status} '
                f'({_format_duration(elapsed)}'
                f', ETA {_format_duration(remaining)})')

    signal.signal(signal.SIGINT, orig_handler)
    return ok_count, fail_count


def do_scan(args):
    """Build and analyse multiple boards, storing results in a database.

    Boards are processed in parallel using a thread pool, with the cores
    split between the workers so the machine stays busy. Each worker builds
    and analyses one board, returning results to the main thread for database
    storage.

    Args:
        args (Namespace): Parsed command-line arguments

    Returns:
        int: Exit code (0 for success, 1 for failure)
    """
    srcdir = os.path.realpath(args.source)
    if not os.path.isdir(srcdir):
        tout.fatal(f'Source directory does not exist: {srcdir}')

    result = _init_scan_db(srcdir, args)
    if result is None:
        return 1
    db, db_path, selected = result

    if not selected:
        terminal.tprint('All boards already scanned')
        db.close()
        return 0

    # Extract args that workers need
    adjust_cfg = getattr(args, 'adjust', None)
    use_dwarf = getattr(args, 'use_dwarf', False)
    use_lsp = getattr(args, 'use_lsp', False)
    unifdef_cmd = getattr(args, 'unifdef', 'unifdef')
    include_headers = getattr(args, 'include_headers', False)
    filter_pattern = args.filter
    clean_after = getattr(args, 'clean_after', False)
    sequential = getattr(args, 'sequential', False)

    scan_start = time.time()
    ok_count = 0
    fail_count = 0

    cpu_count = multiprocessing.cpu_count()
    if sequential:
        # Only one board builds at a time, so let it use every core
        ok_count, fail_count = _scan_sequential(
            selected, srcdir, args.build_base, adjust_cfg, use_dwarf,
            use_lsp, unifdef_cmd, include_headers, filter_pattern,
            clean_after, db, scan_start, cpu_count)
    else:
        # Run several boards at once and split the cores between them. There
        # is no point having more workers than boards, and each worker then
        # builds with enough make jobs to fill its share of the CPU, so the
        # machine stays busy whether there is one board or hundreds. Default
        # to half the CPU count of workers (capped at 16), since each one also
        # spawns buildman, make and the analysis processes.
        workers = getattr(args, 'workers', None) or min(cpu_count // 2, 16)
        workers = max(1, min(workers, len(selected)))
        make_jobs = max(1, cpu_count // workers)

        ok_count, fail_count = _scan_parallel(
            selected, srcdir, args.build_base, adjust_cfg, use_dwarf,
            use_lsp, unifdef_cmd, include_headers, filter_pattern,
            clean_after, db, scan_start, workers, make_jobs)

    # Finalise
    total_time = time.time() - scan_start
    db.set_scan_info('scan_end', str(time.time()))
    db.set_scan_info('board_count', str(ok_count))

    terminal.tprint(
        f'\nScan complete in {_format_duration(total_time)}: '
        f'{ok_count} OK, {fail_count} failed')
    try:
        db_size = os.path.getsize(db_path)
        terminal.tprint(f'Database: {db_path} ({_format_size(db_size)})')
    except OSError:
        pass

    db.close()
    return 0


def _format_duration(seconds):
    """Format a duration in seconds to a human-readable string.

    Args:
        seconds (float): Duration in seconds

    Returns:
        str: Formatted duration (e.g. '2h 15m', '3m 42s', '15s')
    """
    seconds = int(seconds)
    if seconds >= 3600:
        hours = seconds // 3600
        mins = (seconds % 3600) // 60
        return f'{hours}h {mins:02d}m'
    if seconds >= 60:
        mins = seconds // 60
        secs = seconds % 60
        return f'{mins}m {secs:02d}s'
    return f'{seconds}s'


def _format_size(size_bytes):
    """Format a byte count to a human-readable string.

    Args:
        size_bytes (int): Size in bytes

    Returns:
        str: Formatted size (e.g. '1.5 MB', '256 KB')
    """
    if size_bytes >= 1024 * 1024 * 1024:
        return f'{size_bytes / (1024 * 1024 * 1024):.1f} GB'
    if size_bytes >= 1024 * 1024:
        return f'{size_bytes / (1024 * 1024):.1f} MB'
    if size_bytes >= 1024:
        return f'{size_bytes / 1024:.1f} KB'
    return f'{size_bytes} B'


def _find_function_end(fpath, start_line):
    """Find the closing brace of a function starting at a given line.

    Args:
        fpath (str): Absolute path to the source file
        start_line (int): Line number where the function signature starts

    Returns:
        int: Line number of the closing brace, or start_line on failure
    """
    try:
        with open(fpath, 'r', encoding='utf-8', errors='ignore') as f:
            lines = f.readlines()
    except IOError:
        return start_line

    # Find the opening brace from the start line onwards
    depth = 0
    found_open = False
    for i in range(start_line - 1, len(lines)):
        for ch in lines[i]:
            if ch == '{':
                depth += 1
                found_open = True
            elif ch == '}':
                depth -= 1
                if found_open and depth == 0:
                    return i + 1
    return start_line


def find_function_lines(srcdir, func_name, file_path=None):
    """Find the file and line range for a C function definition.

    Uses grep to quickly locate candidate lines, then reads only those
    files to find the function end.

    Args:
        srcdir (str): Source directory root
        func_name (str): Function name to search for
        file_path (str): Optional file path to restrict search

    Returns:
        list of tuple: (rel_path, start_line, end_line) for each match
    """
    # Use grep for fast initial search — match function references
    grep_pattern = rf'\b{re.escape(func_name)}\s*\('

    cmd = ['grep', '-rn', '--include=*.c', '-E', grep_pattern]
    for exc in codman.EXCLUDE_DIRS:
        cmd.extend(['--exclude-dir', exc])

    if file_path:
        cmd.append(os.path.join(srcdir, file_path))
    else:
        cmd.append(srcdir)

    try:
        result = subprocess.run(cmd, capture_output=True, text=True,
                                check=False, cwd=srcdir)
    except FileNotFoundError:
        return []

    if result.returncode not in (0, 1):  # 1 = no matches
        return []

    # Parse grep output and filter for real definitions (not calls/decls)
    # A definition has a return type before the function name
    def_pattern = re.compile(
        rf'^[a-zA-Z_][\w\s*]*\b{re.escape(func_name)}\s*\(')

    results = []
    for line in result.stdout.splitlines():
        # grep -n output: file:line:content
        parts = line.split(':', 2)
        if len(parts) < 3:
            continue
        fpath, line_num_str, content = parts[0], parts[1], parts[2]
        try:
            line_num = int(line_num_str)
        except ValueError:
            continue

        # Filter: must look like a definition, not a call or declaration
        if not def_pattern.match(content):
            continue

        abs_path = os.path.join(srcdir, fpath) if not os.path.isabs(fpath) \
            else fpath
        end_line = _find_function_end(abs_path, line_num)
        rel_path = os.path.relpath(abs_path, srcdir)
        results.append((rel_path, line_num, end_line))

    return results


def _parse_file_line(location):
    """Parse a file:line location string.

    Args:
        location (str): Location in file:line format (e.g. 'common/main.c:42')

    Returns:
        tuple: (file_path, line_number) or (location, None) if no line number
    """
    parts = location.rsplit(':', 1)
    if len(parts) == 2:
        try:
            return parts[0], int(parts[1])
        except ValueError:
            pass
    return location, None


def do_query(args):
    """Execute a query against the multi-board database.

    Args:
        args (Namespace): Parsed command-line arguments

    Returns:
        int: Exit code (0 for success, 1 for failure)
    """
    srcdir = os.path.realpath(args.source)
    db_path = get_db_path(srcdir)

    if not os.path.exists(db_path):
        tout.error(f'Database not found: {db_path}')
        tout.error('Run "codman scan" first to build the database')
        return 1

    db = database.CodmanDatabase(db_path)
    query_cmd = getattr(args, 'query_cmd', None)

    if not query_cmd:
        tout.error('No query type specified. '
                   'Use: file, line, board, unique, function, or info')
        db.close()
        return 1

    fmt = getattr(args, 'format', 'table')
    arch = getattr(args, 'arch', None)

    if query_cmd == 'info':
        ok = output.show_query_info(db.query_info())

    elif query_cmd == 'file':
        rows = db.query_boards_for_file(args.path, arch=arch)
        ok = output.show_query_boards(rows, args.path, fmt)

    elif query_cmd == 'line':
        # Parse file:line format
        rel_path, line_num = _parse_file_line(args.location)
        if line_num is None:
            tout.error(f'Invalid location format: {args.location}')
            tout.error('Use file:line format, e.g. common/main.c:42')
            db.close()
            return 1
        rows = db.query_boards_for_line(rel_path, line_num, arch=arch)
        ok = output.show_query_line_boards(
            rows, rel_path, line_num, fmt)

    elif query_cmd == 'board':
        file_pattern = getattr(args, 'file_pattern', None)
        rows = db.query_files_for_board(args.target, file_pattern)
        ok = output.show_query_files(rows, args.target, fmt)

    elif query_cmd == 'unique':
        rows = db.query_unique_code(args.target)
        ok = output.show_query_unique(rows, args.target, fmt)

    elif query_cmd == 'function':
        file_path = getattr(args, 'path', None)
        matches = find_function_lines(srcdir, args.name, file_path)
        if not matches:
            tout.error(f'Function {args.name}() not found')
            db.close()
            return 1

        # For each match, query which boards have those lines active
        ok = True
        for rel_path, start_line, end_line in matches:
            # Query using the first line of the function body
            rows = db.query_boards_for_line(rel_path, start_line, arch=arch)
            ok = output.show_query_function_boards(
                rows, args.name, rel_path, start_line, end_line, fmt)
    else:
        tout.error(f'Unknown query type: {query_cmd}')
        db.close()
        return 1

    db.close()
    return 0 if ok else 1
