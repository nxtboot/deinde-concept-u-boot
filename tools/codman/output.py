# SPDX-License-Identifier: GPL-2.0
#
# Copyright 2025 Canonical Ltd
#
"""Output formatting and display functions for srcman.

This module provides functions for displaying analysis results in various
formats:
- Statistics views (file-level and line-level)
- Directory breakdowns (top-level and subdirectories)
- Per-file summaries
- Detailed line-by-line views
- File listings (used/unused)
- File copying operations
"""

import csv
import os
import shutil
import sys
from collections import defaultdict

# Import from tools directory
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
from u_boot_pylib import terminal, tout  # pylint: disable=wrong-import-position

import category


class DirStats:  # pylint: disable=too-few-public-methods
    """Statistics for a directory.

    Attributes:
        total: Total number of files in directory
        used: Number of files used (compiled)
        unused: Number of files not used
        lines_total: Total lines of code in directory
        lines_used: Number of active lines (after preprocessing)
        files: List of file info dicts (for --show-files)
    """
    def __init__(self):
        self.total = 0
        self.used = 0
        self.unused = 0
        self.lines_total = 0
        self.lines_used = 0
        self.files = []


def count_lines(file_path):
    """Count lines in a file"""
    try:
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            return sum(1 for _ in f)
    except IOError:
        return 0


def klocs(lines):
    """Format line count in thousands, rounded to 1 decimal place.

    Args:
        lines (int): Line count (e.g., 3500)

    Returns:
        Formatted string in thousands (e.g., '3.5')
    """
    kloc = round(lines / 1000, 1)
    return f'{kloc:.1f}'


def percent(numerator, denominator):
    """Calculate percentage, handling division by zero.

    Args:
        numerator (int/float): The numerator
        denominator (int/float): The denominator

    Returns:
        float: Percentage (0-100), or 0 if denominator is 0
    """
    return 100 * numerator / denominator if denominator else 0


def print_heading(text, width=70, char='='):
    """Print a heading with separator lines.

    Args:
        text (str): Heading text to display (empty for separator only)
        width (int): Width of the separator line
        char (str): Character to use for separator
    """
    print(char * width)
    if text:
        print(text)
        print(char * width)


def show_file_detail(detail_file, file_results, srcdir):
    """Show detailed line-by-line analysis for a specific file.

    Args:
        detail_file (str): Path to the file to show details for (relative or
            absolute)
        file_results (dict): Dictionary mapping file paths to analysis results
        srcdir (str): Root directory of the source tree

    Returns:
        True on success, False on error
    """
    detail_path = os.path.realpath(detail_file)
    if detail_path not in file_results:
        # Try relative to source root
        detail_path = os.path.realpath(os.path.join(srcdir, detail_file))

    if detail_path in file_results:
        result = file_results[detail_path]
        rel_path = os.path.relpath(detail_path, srcdir)

        print_heading(f'DETAIL FOR: {rel_path}', width=70)
        print(f'Total lines:    {result.total_lines:6}')
        pct_active = percent(result.active_lines, result.total_lines)
        pct_inactive = percent(result.inactive_lines, result.total_lines)
        print(f'Active lines:   {result.active_lines:6} ({pct_active:.1f}%)')
        print(f'Inactive lines: {result.inactive_lines:6} ' +
              f'({pct_inactive:.1f}%)')
        print()

        # Show the file with status annotations
        with open(detail_path, 'r', encoding='utf-8', errors='ignore') as f:
            lines = f.readlines()

        col = terminal.Color()
        for line_num, line in enumerate(lines, 1):
            status = result.line_status.get(line_num, 'unknown')
            marker = '-' if status == 'inactive' else ' '
            prefix = f'{marker} {line_num:4} | '
            code = line.rstrip()

            if status == 'active':
                # Normal color for active code
                print(prefix + code)
            else:
                # Non-bright cyan for inactive code
                print(prefix + col.build(terminal.Color.CYAN, code,
                                         bright=False))
        return True

    # File not found - caller handles errors
    return False


def show_file_summary(file_results, srcdir):
    """Show per-file summary of line analysis.

    Args:
        file_results (dict): Dictionary mapping file paths to analysis results
        srcdir (str): Root directory of the source tree

    Returns:
        bool: True on success
    """
    print_heading('PER-FILE SUMMARY', width=90)
    print(f"{'File':<50} {'Total':>8} {'Active':>8} "
          f"{'Inactive':>8} {'%Active':>8}")
    print('-' * 90)

    for source_file in sorted(file_results.keys()):
        result = file_results[source_file]
        rel_path = os.path.relpath(source_file, srcdir)
        if len(rel_path) > 47:
            rel_path = '...' + rel_path[-44:]

        pct_active = percent(result.active_lines, result.total_lines)
        print(f'{rel_path:<50} {result.total_lines:>8} '
              f'{result.active_lines:>8} {result.inactive_lines:>8} '
              f'{pct_active:>7.1f}%')

    return True


def list_unused_files(skipped_sources, srcdir):
    """List unused source files.

    Args:
        skipped_sources (set of str): Set of unused source file paths (relative
            to srcdir)
        srcdir (str): Root directory of the source tree

    Returns:
        bool: True on success
    """
    print(f'Unused source files ({len(skipped_sources)}):')
    for source_file in sorted(skipped_sources):
        try:
            rel_path = os.path.relpath(source_file, srcdir)
        except ValueError:
            rel_path = source_file
        print(f'  {rel_path}')

    return True


def list_used_files(used_sources, srcdir):
    """List used source files.

    Args:
        used_sources (set of str): Set of used source file paths (relative
            to srcdir)
        srcdir (str): Root directory of the source tree

    Returns:
        bool: True on success
    """
    print(f'Used source files ({len(used_sources)}):')
    for source_file in sorted(used_sources):
        try:
            rel_path = os.path.relpath(source_file, srcdir)
        except ValueError:
            rel_path = source_file
        print(f'  {rel_path}')

    return True


def copy_used_files(used_sources, srcdir, dest_dir):
    """Copy used source files to a destination directory, preserving structure.

    Args:
        used_sources (set): Set of used source file paths (relative to srcdir)
        srcdir (str): Root directory of the source tree
        dest_dir (str): Destination directory for the copy

    Returns:
        True on success, False if errors occurred
    """
    if os.path.exists(dest_dir):
        tout.error(f'Destination directory already exists: {dest_dir}')
        return False

    tout.progress(f'Copying {len(used_sources)} used source files to ' +
                  f'{dest_dir}')

    copied_count = 0
    error_count = 0

    for source_file in sorted(used_sources):
        src_path = os.path.join(srcdir, source_file)
        dest_path = os.path.join(dest_dir, source_file)

        try:
            # Create parent directory if needed
            dest_parent = os.path.dirname(dest_path)
            os.makedirs(dest_parent, exist_ok=True)

            # Copy the file
            shutil.copy2(src_path, dest_path)
            copied_count += 1
        except IOError as e:
            error_count += 1
            tout.error(f'Error copying {source_file}: {e}')

    tout.progress(f'Copied {copied_count} files to {dest_dir}')
    if error_count:
        tout.error(f'Failed to copy {error_count} files')
        return False

    return True


def collect_dir_stats(all_sources, used_sources, file_results, srcdir,
                      by_subdirs, show_files):
    """Collect statistics organized by directory.

    Args:
        all_sources (set): Set of all source file paths
        used_sources (set): Set of used source file paths
        file_results (dict): Optional dict mapping file paths to line
            analysis results (or None)
        srcdir (str): Root directory of the source tree
        by_subdirs (bool): If True, use full subdirectory paths;
            otherwise top-level only
        show_files (bool): If True, collect individual file info within
            each directory

    Returns:
        dict: Directory statistics keyed by directory path
    """
    dir_stats = defaultdict(DirStats)

    for source_file in all_sources:
        rel_path = os.path.relpath(source_file, srcdir)

        if by_subdirs:
            # Use the full directory path (not including the filename)
            dir_path = os.path.dirname(rel_path)
            if not dir_path:
                dir_path = '.'
        else:
            # Use only the top-level directory
            dir_path = (rel_path.split(os.sep)[0] if os.sep in rel_path
                        else rel_path)

        line_count = count_lines(source_file)
        dir_stats[dir_path].total += 1
        dir_stats[dir_path].lines_total += line_count

        if source_file in used_sources:
            dir_stats[dir_path].used += 1
            # Use active line count if line-level analysis was performed
            # Normalize path to match file_results keys (absolute paths)
            abs_source = os.path.realpath(source_file)

            # Try to find the file in file_results
            result = None
            if file_results:
                if abs_source in file_results:
                    result = file_results[abs_source]
                elif source_file in file_results:
                    result = file_results[source_file]

            if result:
                active_lines = result.active_lines
                inactive_lines = result.inactive_lines
                dir_stats[dir_path].lines_used += active_lines
                # Store file info for --show-files (exclude .h files)
                if show_files and not rel_path.endswith('.h'):
                    dir_stats[dir_path].files.append({
                        'path': rel_path,
                        'total': line_count,
                        'active': active_lines,
                        'inactive': inactive_lines
                    })
            else:
                # File not found in results - count all lines
                tout.debug(f'File not in results (using full count): '
                           f'{rel_path}')
                dir_stats[dir_path].lines_used += line_count
                if show_files and not rel_path.endswith('.h'):
                    dir_stats[dir_path].files.append({
                        'path': rel_path,
                        'total': line_count,
                        'active': line_count,
                        'inactive': 0
                    })
        else:
            dir_stats[dir_path].unused += 1

    return dir_stats


def print_dir_stats(dir_stats, file_results, by_subdirs, show_files,
                    show_empty, use_kloc=False):
    """Print directory statistics table.

    Args:
        dir_stats (dict): Directory statistics keyed by directory path
        file_results (dict): Optional dict mapping file paths to line analysis
            results (or None)
        by_subdirs (bool): If True, show full subdirectory breakdown; otherwise
            top-level only
        show_files (bool): If True, show individual files within directories
        show_empty (bool): If True, show directories with 0 lines used
        use_kloc (bool): If True, show line counts in kLOC; otherwise show lines
    """
    # Sort alphabetically by directory name
    sorted_dirs = sorted(dir_stats.items(), key=lambda x: x[0])

    for dir_path, stats in sorted_dirs:
        # Skip subdirectories with 0 lines used unless --show-zero-lines is set
        if by_subdirs and not show_empty and stats.lines_used == 0:
            continue

        pct_used = percent(stats.used, stats.total)
        pct_code = percent(stats.lines_used, stats.lines_total)
        # Truncate long paths
        display_path = dir_path
        if len(display_path) > 37:
            display_path = '...' + display_path[-34:]

        # Format line counts based on use_kloc flag
        if use_kloc:
            lines_total_str = f'{klocs(stats.lines_total):>8}'
            lines_used_str = f'{klocs(stats.lines_used):>7}'
        else:
            lines_total_str = f'{stats.lines_total:>8}'
            lines_used_str = f'{stats.lines_used:>7}'

        print(f'{display_path:<40} {stats.total:>7} {stats.used:>7} '
              f'{pct_used:>6.0f} {pct_code:>6.0f} '
              f'{lines_total_str} {lines_used_str}')

        # Show individual files if requested
        if show_files and stats.files:
            # Sort files alphabetically by filename
            sorted_files = sorted(stats.files, key=lambda x: os.path.basename(x['path']))

            for info in sorted_files:
                # Skip files with 0 active lines unless show_empty is set
                if not show_empty and info['active'] == 0:
                    continue

                filename = os.path.basename(info['path'])
                if len(filename) > 35:
                    filename = filename[:32] + '...'

                if file_results:
                    # Show line-level details
                    pct_active = percent(info['active'], info['total'])
                    # Format line counts based on use_kloc flag
                    if use_kloc:
                        total_str = f'{klocs(info["total"]):>8}'
                        active_str = f'{klocs(info["active"]):>7}'
                    else:
                        total_str = f'{info["total"]:>8}'
                        active_str = f'{info["active"]:>7}'

                    # Align with directory format: skip Files/Used columns,
                    # show %code, then lines column, active in Used column
                    print(f"  {filename:<38} {'':>7} {'':>7} {'':>6} "
                          f"{pct_active:>6.0f} {total_str} {active_str}")
                else:
                    # Show file-level only
                    print(f"  {filename:<38} {info['total']:>7} lines")

            # Add blank line after file list
            print()


def show_dir_breakdown(all_sources, used_sources, file_results, srcdir,
                       by_subdirs, show_files, show_empty, use_kloc=False):
    """Show breakdown by directory (top-level or subdirectories).

    Args:
        all_sources (set): Set of all source file paths
        used_sources (set): Set of used source file paths
        file_results (dict): Optional dict mapping file paths to line analysis
            results (or None)
        srcdir (str): Root directory of the source tree
        by_subdirs (bool): If True, show full subdirectory breakdown; otherwise
             top-level only
        show_files (bool): If True, show individual files within each directory
        show_empty (bool): If True, show directories with 0 lines used
        use_kloc (bool): If True, show line counts in kLOC; otherwise show lines

    Returns:
        bool: True on success
    """
    # Width of the main table (Directory + Total + Used columns)
    table_width = 87

    print_heading('BREAKDOWN BY TOP-LEVEL DIRECTORY' if by_subdirs else '',
                  width=table_width)
    # Column header changes based on use_kloc
    lines_header = 'kLOC' if use_kloc else 'Lines'
    print(f"{'Directory':<40} {'Files':>7} {'Used':>7} {'%Used':>6} " +
          f"{'%Code':>6} {lines_header:>8} {'Used':>7}")
    print('-' * table_width)

    # Collect directory statistics
    dir_stats = collect_dir_stats(all_sources, used_sources, file_results,
                                  srcdir, by_subdirs, show_files)

    # Print directory statistics
    print_dir_stats(dir_stats, file_results, by_subdirs, show_files, show_empty,
                    use_kloc)

    print('-' * table_width)
    total_lines_all = sum(count_lines(f) for f in all_sources)
    # Calculate used lines: if we have file_results, use active_lines from there
    # Otherwise, count all lines in used files
    if file_results:
        total_lines_used = sum(r.active_lines for r in file_results.values())
    else:
        total_lines_used = sum(count_lines(f) for f in used_sources)
    pct_files = percent(len(used_sources), len(all_sources))
    pct_code = percent(total_lines_used, total_lines_all)

    # Format totals based on use_kloc flag
    if use_kloc:
        total_str = f'{klocs(total_lines_all):>8}'
        used_str = f'{klocs(total_lines_used):>7}'
    else:
        total_str = f'{total_lines_all:>8}'
        used_str = f'{total_lines_used:>7}'

    print(f"{'TOTAL':<40} {len(all_sources):>7} {len(used_sources):>7} "
          f"{pct_files:>6.0f} {pct_code:>6.0f} "
          f"{total_str} {used_str}")
    print_heading('', width=table_width)
    print()

    return True


def generate_html_breakdown(all_sources, used_sources, file_results, srcdir,
                            by_subdirs, show_files, show_empty, use_kloc,
                            html_file, board=None, analysis_method=None):
    """Generate HTML output with colored directory breakdown.

    Args:
        all_sources (set): Set of all source file paths
        used_sources (set): Set of used source file paths
        file_results (dict): Optional dict mapping file paths to line analysis
            results (or None)
        srcdir (str): Root directory of the source tree
        by_subdirs (bool): If True, show full subdirectory breakdown
        show_files (bool): If True, show individual files within directories
        show_empty (bool): If True, show directories with 0 lines used
        use_kloc (bool): If True, show line counts in kLOC
        html_file (str): Path to output HTML file
        board (str): Board name (optional)
        analysis_method (str): Analysis method ('unifdef'/'lsp'/'dwarf')

    Returns:
        bool: True on success
    """
    # Get git information
    import subprocess
    import datetime

    try:
        # Get short commit hash
        git_hash = subprocess.check_output(
            ['git', 'rev-parse', '--short', 'HEAD'],
            cwd=srcdir, stderr=subprocess.DEVNULL, text=True
        ).strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        git_hash = 'unknown'

    try:
        # Get commit date
        git_date = subprocess.check_output(
            ['git', 'log', '-1', '--format=%cd', '--date=short'],
            cwd=srcdir, stderr=subprocess.DEVNULL, text=True
        ).strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        git_date = datetime.date.today().strftime('%Y-%m-%d')

    # Collect directory statistics
    dir_stats = collect_dir_stats(all_sources, used_sources, file_results,
                                  srcdir, by_subdirs, show_files)

    # Calculate totals
    total_lines_all = sum(count_lines(f) for f in all_sources)
    if file_results:
        total_lines_used = sum(r.active_lines for r in file_results.values())
    else:
        total_lines_used = sum(count_lines(f) for f in used_sources)

    # Generate HTML
    lines_header = 'kLOC' if use_kloc else 'Lines'
    board_name = board if board else 'unknown'

    html = f'''<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Code Analysis Report</title>
    <style>
        body {{
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            margin: 20px;
            background-color: #f5f5f5;
        }}
        h1 {{
            color: #333;
            border-bottom: 3px solid #4CAF50;
            padding-bottom: 10px;
            margin-bottom: 10px;
        }}
        .build-info {{
            background-color: #e8f5e9;
            padding: 10px 15px;
            margin: 10px 0 20px 0;
            border-radius: 5px;
            border-left: 4px solid #4CAF50;
            font-size: 0.95em;
        }}
        .build-info span {{
            margin-right: 20px;
            color: #555;
        }}
        .build-info .label {{
            font-weight: bold;
            color: #333;
        }}
        .summary {{
            background-color: #fff;
            padding: 15px;
            margin: 20px 0;
            border-radius: 5px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
        }}
        .summary-stat {{
            display: inline-block;
            margin: 10px 20px 10px 0;
        }}
        .summary-stat .label {{
            font-weight: bold;
            color: #666;
        }}
        .summary-stat .value {{
            font-size: 1.2em;
            color: #4CAF50;
        }}
        table {{
            border-collapse: collapse;
            width: 100%;
            background-color: white;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
            margin: 20px 0;
        }}
        th {{
            background-color: #4CAF50;
            color: white;
            padding: 12px;
            text-align: right;
            position: sticky;
            top: 0;
        }}
        th:first-child {{
            text-align: left;
        }}
        td {{
            padding: 10px 12px;
            border-bottom: 1px solid #ddd;
            text-align: right;
        }}
        td:first-child {{
            text-align: left;
            font-weight: 500;
        }}
        tr.directory {{
            background-color: #f9f9f9;
            cursor: pointer;
        }}
        tr.directory:hover {{
            background-color: #e8f5e9;
        }}
        tr.directory td:first-child::before {{
            content: '▼ ';
            color: #4CAF50;
            font-size: 0.8em;
            margin-right: 5px;
        }}
        tr.directory.collapsed td:first-child::before {{
            content: '▶ ';
        }}
        tr.directory.hidden {{
            display: none;
        }}
        tr.file {{
            background-color: #ffffff;
            font-size: 0.9em;
        }}
        tr.file.hidden {{
            display: none;
        }}
        tr.file td:first-child {{
            padding-left: 40px;
            font-weight: normal;
            color: #555;
        }}
        tr.file:hover {{
            background-color: #f5f5f5;
        }}
        .pct-high {{
            color: #4CAF50;
            font-weight: bold;
        }}
        .pct-med {{
            color: #FF9800;
            font-weight: bold;
        }}
        .pct-low {{
            color: #f44336;
            font-weight: bold;
        }}
        .spacer {{
            height: 10px;
        }}
        tr.total {{
            background-color: #e8f5e9;
            font-weight: bold;
            border-top: 2px solid #4CAF50;
        }}
    </style>
</head>
<body>
    <h1>Code Analysis Report</h1>
    <div class="build-info">
        <span><span class="label">Board:</span> {board_name}</span>
        <span><span class="label">Commit:</span> {git_hash}</span>
        <span><span class="label">Date:</span> {git_date}</span>
        <span><span class="label">Analysis:</span> {analysis_method or 'unknown'}</span>
    </div>
    <div class="summary">
        <div class="summary-stat">
            <span class="label">Total Files:</span>
            <span class="value">{len(all_sources)}</span>
        </div>
        <div class="summary-stat">
            <span class="label">Used Files:</span>
            <span class="value">{len(used_sources)}</span>
        </div>
        <div class="summary-stat">
            <span class="label">Usage:</span>
            <span class="value">{percent(len(used_sources), len(all_sources)):.0f}%</span>
        </div>
        <div class="summary-stat">
            <span class="label">Total Lines:</span>
            <span class="value">{total_lines_all:,}</span>
        </div>
        <div class="summary-stat">
            <span class="label">Used Lines:</span>
            <span class="value">{total_lines_used:,}</span>
        </div>
        <div class="summary-stat">
            <span class="label">Code Usage:</span>
            <span class="value">{percent(total_lines_used, total_lines_all):.0f}%</span>
        </div>
    </div>
    <table>
        <thead>
            <tr>
                <th>Directory</th>
                <th>Files</th>
                <th>Used</th>
                <th>%Used</th>
                <th>%Code</th>
                <th>{lines_header}</th>
                <th>Used</th>
            </tr>
        </thead>
        <tbody>
'''

    # Build hierarchical structure - only show top-level directories initially
    # Group all directories by their top-level component
    top_level_groups = {}
    sorted_dirs = sorted(dir_stats.items(), key=lambda x: x[0])

    for dir_path, stats in sorted_dirs:
        # Skip directories with 0 lines used unless show_empty is set
        if not show_empty and stats.lines_used == 0:
            continue

        # Get top-level directory name
        parts = dir_path.split('/')
        top_level = parts[0]

        if top_level not in top_level_groups:
            top_level_groups[top_level] = []

        top_level_groups[top_level].append((dir_path, stats))

    # Generate HTML for hierarchical structure
    dir_counter = [0]  # Use list to allow modification in nested function

    def render_directory(dir_path, stats, parent_id=None, indent_level=0):
        """Render a directory row and its children."""
        nonlocal html
        dir_id = f'dir-{dir_counter[0]}'
        dir_counter[0] += 1

        pct_used = percent(stats.used, stats.total)
        pct_code = percent(stats.lines_used, stats.lines_total)
        pct_code_class = 'pct-high' if pct_code >= 75 else ('pct-med' if pct_code >= 50 else 'pct-low')

        if use_kloc:
            lines_total_str = f'{klocs(stats.lines_total)}'
            lines_used_str = f'{klocs(stats.lines_used)}'
        else:
            lines_total_str = f'{stats.lines_total:,}'
            lines_used_str = f'{stats.lines_used:,}'

        # Add indentation to directory name
        indent = '&nbsp;&nbsp;' * indent_level
        display_name = f'{indent}{dir_path}' if indent_level > 0 else dir_path

        # Start collapsed
        collapsed_class = ' collapsed'
        hidden_class = ' hidden' if parent_id else ''
        parent_attr = f' data-parent-dir="{parent_id}"' if parent_id else ''

        html += f'''            <tr class="directory{collapsed_class}{hidden_class}" data-dir-id="{dir_id}"{parent_attr}>
                <td>{display_name}</td>
                <td>{stats.total}</td>
                <td>{stats.used}</td>
                <td>{pct_used:.0f}</td>
                <td class="{pct_code_class}">{pct_code:.0f}</td>
                <td>{lines_total_str}</td>
                <td>{lines_used_str}</td>
            </tr>
'''
        return dir_id

    # Render top-level directories and their hierarchies
    for top_level in sorted(top_level_groups.keys()):
        subdirs_list = top_level_groups[top_level]

        # Aggregate stats for top-level directory
        from collections import namedtuple
        DirStats = namedtuple('DirStats', ['total', 'used', 'unused', 'lines_total', 'lines_used', 'files'])
        total_files = sum(s.total for _, s in subdirs_list)
        used_files = sum(s.used for _, s in subdirs_list)
        total_lines = sum(s.lines_total for _, s in subdirs_list)
        used_lines = sum(s.lines_used for _, s in subdirs_list)

        top_stats = DirStats(total=total_files, used=used_files, unused=0,
                            lines_total=total_lines, lines_used=used_lines, files=[])

        # Render top-level directory with aggregated stats
        top_dir_id = render_directory(top_level, top_stats, None, 0)

        # Render all subdirectories under this top-level directory
        for subdir_path, subdir_stats in sorted(subdirs_list):
            subdir_id = render_directory(subdir_path, subdir_stats, top_dir_id, 1)

            # Render files for this subdirectory
            if show_files and subdir_stats.files:
                sorted_files = sorted(subdir_stats.files,
                                    key=lambda x: os.path.basename(x['path']))

                for info in sorted_files:
                    if not show_empty and info['active'] == 0:
                        continue

                    filename = os.path.basename(info['path'])

                    if file_results:
                        pct_active = percent(info['active'], info['total'])
                        pct_active_class = ('pct-high' if pct_active >= 75
                                          else ('pct-med' if pct_active >= 50 else 'pct-low'))

                        if use_kloc:
                            total_str = f'{klocs(info["total"])}'
                            active_str = f'{klocs(info["active"])}'
                        else:
                            total_str = f'{info["total"]:,}'
                            active_str = f'{info["active"]:,}'

                        html += f'''            <tr class="file hidden" data-parent-dir="{subdir_id}">
                <td>&nbsp;&nbsp;&nbsp;&nbsp;{filename}</td>
                <td></td>
                <td></td>
                <td></td>
                <td class="{pct_active_class}">{pct_active:.0f}</td>
                <td>{total_str}</td>
                <td>{active_str}</td>
            </tr>
'''

    # Add total row
    pct_files = percent(len(used_sources), len(all_sources))
    pct_code_total = percent(total_lines_used, total_lines_all)

    if use_kloc:
        total_str = f'{klocs(total_lines_all)}'
        used_str = f'{klocs(total_lines_used)}'
    else:
        total_str = f'{total_lines_all:,}'
        used_str = f'{total_lines_used:,}'

    html += f'''            <tr class="total">
                <td>TOTAL</td>
                <td>{len(all_sources)}</td>
                <td>{len(used_sources)}</td>
                <td>{pct_files:.0f}</td>
                <td>{pct_code_total:.0f}</td>
                <td>{total_str}</td>
                <td>{used_str}</td>
            </tr>
        </tbody>
    </table>
    <script>
        // Toggle file and subdirectory visibility when clicking on directory rows
        document.addEventListener('DOMContentLoaded', function() {{
            const dirRows = document.querySelectorAll('tr.directory');

            dirRows.forEach(function(dirRow) {{
                dirRow.addEventListener('click', function() {{
                    const dirId = this.getAttribute('data-dir-id');
                    const childRows = document.querySelectorAll('[data-parent-dir="' + dirId + '"]');

                    // Toggle collapsed class on directory
                    this.classList.toggle('collapsed');

                    // Toggle hidden class on child rows (both files and subdirectories)
                    childRows.forEach(function(childRow) {{
                        childRow.classList.toggle('hidden');
                    }});
                }});
            }});
        }});
    </script>
</body>
</html>
'''

    # Write HTML to file
    try:
        with open(html_file, 'w', encoding='utf-8') as f:
            f.write(html)
        tout.info(f'HTML report written to: {html_file}')
        return True
    except IOError as e:
        tout.error(f'Failed to write HTML file: {e}')
        return False


def _write_file_row(writer, info, features, ignore_patterns, file_results,
                    use_kloc, files_only):
    """Write a single file row to CSV.

    Args:
        writer: CSV writer object
        info (dict): File info with 'path', 'total', 'active' keys
        features (dict): Features dict from category config
        ignore_patterns (list): List of patterns to ignore
        file_results (dict): File analysis results (or None)
        use_kloc (bool): If True, show line counts in kLOC
        files_only (bool): If True, use simplified row format

    Returns:
        tuple: (wrote_row, is_matched) - whether row was written, whether file
            matched a category
    """
    # Skip ignored files (external code)
    if category.should_ignore_file(info['path'], ignore_patterns):
        return False, True  # Not written, but considered matched

    # Match file to feature/category
    feat_id, cat_id = None, None
    if features:
        feat_id, cat_id = category.get_file_feature(info['path'], features)

    is_matched = feat_id is not None

    if file_results:
        pct_active = percent(info['active'], info['total'])

        if use_kloc:
            total_str = klocs(info['total'])
            active_str = klocs(info['active'])
        else:
            total_str = info['total']
            active_str = info['active']

        if files_only:
            writer.writerow([info['path'], cat_id or '', feat_id or '',
                            f'{pct_active:.0f}', total_str, active_str])
        else:
            writer.writerow(['file', info['path'], cat_id or '', feat_id or '',
                            '', '', '', f'{pct_active:.0f}',
                            total_str, active_str])

    return True, is_matched


def _report_matching_stats(features, total_files, unmatched_files,
                           show_unmatched, show_empty_features):
    """Report category matching statistics.

    Args:
        features (dict): Features dict from category config
        total_files (int): Total number of files processed
        unmatched_files (list): List of file paths without category match
        show_unmatched (bool): If True, list all unmatched files
        show_empty_features (bool): If True, list features with no files
    """
    if features and total_files > 0:
        matched = total_files - len(unmatched_files)
        print(f'Category matching: {matched}/{total_files} files matched, '
              f'{len(unmatched_files)} unmatched')
        if show_unmatched and unmatched_files:
            print('Unmatched files:')
            for filepath in sorted(unmatched_files):
                print(f'  {filepath}')

    if features and show_empty_features:
        empty_features = [
            feat_id for feat_id, feat_data in features.items()
            if not feat_data.get('files', [])
        ]
        if empty_features:
            print(f'Features with no files ({len(empty_features)}):')
            for feat_id in sorted(empty_features):
                print(f'  {feat_id}')


def generate_csv(all_sources, used_sources, file_results, srcdir,
                 by_subdirs, show_files, show_empty, use_kloc, csv_file,
                 show_unmatched=False, files_only=False,
                 show_empty_features=False):
    """Generate CSV output with directory breakdown.

    Args:
        all_sources (set): Set of all source file paths
        used_sources (set): Set of used source file paths
        file_results (dict): Optional dict mapping file paths to line analysis
            results (or None)
        srcdir (str): Root directory of the source tree
        by_subdirs (bool): If True, show full subdirectory breakdown
        show_files (bool): If True, show individual files within directories
        show_empty (bool): If True, show directories with 0 lines used
        use_kloc (bool): If True, show line counts in kLOC
        csv_file (str): Path to output CSV file
        show_unmatched (bool): If True, list all unmatched files to stdout
        files_only (bool): If True, only output file rows (exclude directories)
        show_empty_features (bool): If True, list features with no files defined

    Returns:
        bool: True on success
    """

    # Load category configuration for file-to-feature matching
    cfg = category.load_category_config(srcdir)
    features = cfg.features if cfg else None
    ignore_patterns = cfg.ignore if cfg else None

    # Collect directory statistics
    dir_stats = collect_dir_stats(all_sources, used_sources, file_results,
                                  srcdir, by_subdirs, show_files)

    # Calculate totals
    total_lines_all = sum(count_lines(f) for f in all_sources)
    if file_results:
        total_lines_used = sum(r.active_lines for r in file_results.values())
    else:
        total_lines_used = sum(count_lines(f) for f in used_sources)

    # Track unmatched files
    unmatched_files = []
    total_files = 0

    try:
        with open(csv_file, 'w', newline='', encoding='utf-8') as f:
            writer = csv.writer(f)

            # Write header
            lines_header = 'kLOC' if use_kloc else 'Lines'
            if files_only:
                writer.writerow(['Path', 'Category', 'Feature', '%Code',
                                lines_header, 'Used'])
            else:
                writer.writerow(['Type', 'Path', 'Category', 'Feature', 'Files',
                                'Used', '%Used', '%Code', lines_header, 'Used'])

            # Sort and output directories
            for dir_path in sorted(dir_stats.keys()):
                stats = dir_stats[dir_path]

                # Skip directories with 0 lines used unless show_empty is set
                if not show_empty and stats.lines_used == 0:
                    continue

                pct_used = percent(stats.used, stats.total)
                pct_code = percent(stats.lines_used, stats.lines_total)

                if use_kloc:
                    lines_total_str = klocs(stats.lines_total)
                    lines_used_str = klocs(stats.lines_used)
                else:
                    lines_total_str = stats.lines_total
                    lines_used_str = stats.lines_used

                if not files_only:
                    writer.writerow([
                        'dir', dir_path, '', '', stats.total, stats.used,
                        f'{pct_used:.0f}', f'{pct_code:.0f}',
                        lines_total_str, lines_used_str])

                # Output files if requested
                if show_files and stats.files:
                    sorted_files = sorted(
                        stats.files, key=lambda x: os.path.basename(x['path']))

                    for info in sorted_files:
                        if not show_empty and info['active'] == 0:
                            continue

                        wrote, matched = _write_file_row(
                            writer, info, features, ignore_patterns,
                            file_results, use_kloc, files_only)
                        if wrote:
                            total_files += 1
                            if not matched:
                                unmatched_files.append(info['path'])

            # Write totals row
            pct_files = percent(len(used_sources), len(all_sources))
            pct_code = percent(total_lines_used, total_lines_all)

            if use_kloc:
                total_str = klocs(total_lines_all)
                used_str = klocs(total_lines_used)
            else:
                total_str = total_lines_all
                used_str = total_lines_used

            if not files_only:
                writer.writerow(['total', 'TOTAL', '', '', len(all_sources),
                                len(used_sources), f'{pct_files:.0f}',
                                f'{pct_code:.0f}', total_str, used_str])

        tout.info(f'CSV report written to: {csv_file}')
        _report_matching_stats(features, total_files, unmatched_files,
                               show_unmatched, show_empty_features)
        return True
    except IOError as e:
        tout.error(f'Failed to write CSV file: {e}')
        return False


def show_statistics(all_sources, used_sources, skipped_sources, file_results,
                    srcdir, top_n):
    """Show overall statistics about source file usage.

    Args:
        all_sources (set of str): Set of all source file paths
        used_sources (set of str): Set of used source file paths
        skipped_sources (set of str): Set of unused source file paths
        file_results (dict): Optional dict mapping file paths to line analysis
            results
        srcdir (str): Root directory of the source tree
        top_n (int): Number of top files with most inactive code to show

    Returns:
        bool: True on success
    """
    # Calculate line counts - use file_results (DWARF/unifdef) if available
    if file_results:
        # Use active lines from analysis results
        used_lines = sum(r.active_lines for r in file_results.values())
    else:
        # Fall back to counting all lines in used files
        used_lines = sum(count_lines(f) for f in used_sources)

    unused_lines = sum(count_lines(f) for f in skipped_sources)
    total_lines = used_lines + unused_lines

    print_heading('FILE-LEVEL STATISTICS', width=70)
    print(f'Total source files:   {len(all_sources):6}')
    used_pct = percent(len(used_sources), len(all_sources))
    print(f'Used source files:    {len(used_sources):6} ({used_pct:.1f}%)')
    unused_pct = percent(len(skipped_sources), len(all_sources))
    print(f'Unused source files:  {len(skipped_sources):6} ' +
          f'({unused_pct:.1f}%)')
    print()
    print(f'Total lines of code:  {total_lines:6}')
    used_lines_pct = percent(used_lines, total_lines)
    print(f'Used lines of code:   {used_lines:6} ({used_lines_pct:.1f}%)')
    unused_lines_pct = percent(unused_lines, total_lines)
    print(f'Unused lines of code: {unused_lines:6} ' +
          f'({unused_lines_pct:.1f}%)')
    print_heading('', width=70)

    # If line-level analysis was performed, show those stats too
    if file_results:
        print()
        total_lines_analysed = sum(r.total_lines for r in file_results.values())
        active_lines = sum(r.active_lines for r in file_results.values())
        inactive_lines = sum(r.inactive_lines for r in file_results.values())

        print_heading('LINE-LEVEL STATISTICS (within compiled files)', width=70)
        print(f'Files analysed:           {len(file_results):6}')
        print(f'Total lines in used files:{total_lines_analysed:6}')
        active_pct = percent(active_lines, total_lines_analysed)
        print(f'Active lines:             {active_lines:6} ' +
              f'({active_pct:.1f}%)')
        inactive_pct = percent(inactive_lines, total_lines_analysed)
        print(f'Inactive lines:           {inactive_lines:6} ' +
              f'({inactive_pct:.1f}%)')
        print_heading('', width=70)
        print()

        # Show top files with most inactive code
        files_by_inactive = sorted(
            file_results.items(),
            key=lambda x: x[1].inactive_lines,
            reverse=True
        )

        print(f'TOP {top_n} FILES WITH MOST INACTIVE CODE:')
        print('-' * 70)
        for source_file, result in files_by_inactive[:top_n]:
            rel_path = os.path.relpath(source_file, srcdir)
            pct_inactive = percent(result.inactive_lines, result.total_lines)
            print(f'  {result.inactive_lines:5} inactive lines ' +
                  f'({pct_inactive:4.1f}%) - {rel_path}')

    return True


# --- Query output functions ---


def _format_csv_rows(header, rows):
    """Write rows as CSV to stdout.

    Args:
        header (list of str): Column names
        rows (list of list): Row data
    """
    writer = csv.writer(sys.stdout)
    writer.writerow(header)
    for row in rows:
        writer.writerow(row)


def show_query_info(info):
    """Display database statistics.

    Args:
        info (dict): Statistics from CodmanDatabase.query_info()

    Returns:
        bool: True
    """
    print_heading('DATABASE INFO', width=50)
    print(f"  Boards (OK):     {info.get('board_count', 0):>8}")
    print(f"  Boards (failed): {info.get('board_failed', 0):>8}")
    print(f"  Files:           {info.get('file_count', 0):>8}")
    print(f"  Board-file rows: {info.get('board_file_count', 0):>8}")
    print(f"  Active ranges:   {info.get('range_count', 0):>8}")
    db_size = info.get('db_size', 0)
    if db_size >= 1024 * 1024:
        print(f'  Database size:   {db_size / (1024 * 1024):>7.1f} MB')
    elif db_size >= 1024:
        print(f'  Database size:   {db_size / 1024:>7.1f} KB')
    else:
        print(f'  Database size:   {db_size:>8} B')
    print()

    # Architecture breakdown
    arch_counts = info.get('arch_counts', {})
    if arch_counts:
        print('  Architecture breakdown:')
        for arch, cnt in arch_counts.items():
            print(f'    {arch:<20} {cnt:>5}')
        print()

    # Scan info
    srcdir = info.get('srcdir', '')
    if srcdir:
        print(f'  Source dir: {srcdir}')
    git_hash = info.get('git_hash', '')
    if git_hash:
        print(f'  Git hash:   {git_hash[:12]}')

    return True


def show_query_boards(rows, path, fmt):
    """Display which boards compile a file.

    Args:
        rows (list of dict): Results from query_boards_for_file()
        path (str): File path that was queried
        fmt (str): Output format ('table', 'csv', or 'count')

    Returns:
        bool: True
    """
    if fmt == 'count':
        print(len(rows))
        return True

    if fmt == 'csv':
        _format_csv_rows(
            ['target', 'arch', 'active_lines', 'total_lines'],
            [[r['target'], r['arch'], r['active_lines'], r['total_lines']]
             for r in rows])
        return True

    # Table format
    if not rows:
        print(f'No boards compile {path}')
        return True

    print(f'{len(rows)} boards compile {path}:')
    print(f"  {'Board':<40} {'Arch':<12} {'Active':>7} {'Total':>7}"
          f" {'%':>6}")
    print('  ' + '-' * 72)
    for row in rows:
        pct = percent(row['active_lines'], row['total_lines'])
        print(f"  {row['target']:<40} {row['arch']:<12}"
              f" {row['active_lines']:>7} {row['total_lines']:>7}"
              f" {pct:>5.1f}%")
    return True


def show_query_line_boards(rows, rel_path, line_num, fmt):
    """Display which boards have a specific line active.

    Args:
        rows (list of dict): Results from query_boards_for_line()
        rel_path (str): File path
        line_num (int): Line number
        fmt (str): Output format ('table', 'csv', or 'count')

    Returns:
        bool: True
    """
    if fmt == 'count':
        print(len(rows))
        return True

    if fmt == 'csv':
        _format_csv_rows(
            ['target', 'arch'],
            [[r['target'], r['arch']] for r in rows])
        return True

    # Table format
    location = f'{rel_path}:{line_num}'
    if not rows:
        print(f'No boards have {location} active')
        return True

    print(f'{len(rows)} boards have {location} active:')
    print(f"  {'Board':<40} {'Arch':<12}")
    print('  ' + '-' * 52)
    for row in rows:
        print(f"  {row['target']:<40} {row['arch']:<12}")
    return True


def show_query_files(rows, target, fmt):
    """Display what files a board compiles.

    Args:
        rows (list of dict): Results from query_files_for_board()
        target (str): Board target name
        fmt (str): Output format ('table', 'csv', or 'count')

    Returns:
        bool: True
    """
    if fmt == 'count':
        print(len(rows))
        return True

    if fmt == 'csv':
        _format_csv_rows(
            ['rel_path', 'total_lines', 'active_lines', 'inactive_lines'],
            [[r['rel_path'], r['total_lines'], r['active_lines'],
              r['inactive_lines']] for r in rows])
        return True

    # Table format
    if not rows:
        print(f'Board {target} not found or has no files')
        return True

    total_active = sum(r['active_lines'] for r in rows)
    total_all = sum(r['total_lines'] for r in rows)
    print(f'{target}: {len(rows)} files, {total_active} active lines'
          f' of {total_all} total'
          f' ({percent(total_active, total_all):.1f}%)')
    print(f"  {'File':<50} {'Active':>7} {'Total':>7} {'%':>6}")
    print('  ' + '-' * 70)
    for row in rows:
        pct = percent(row['active_lines'], row['total_lines'])
        path = row['rel_path']
        if len(path) > 47:
            path = '...' + path[-44:]
        print(f"  {path:<50} {row['active_lines']:>7}"
              f" {row['total_lines']:>7} {pct:>5.1f}%")
    return True


def show_query_unique(rows, target, fmt):
    """Display code unique to a board.

    Args:
        rows (list of dict): Results from query_unique_code()
        target (str): Board target name
        fmt (str): Output format ('table', 'csv', or 'count')

    Returns:
        bool: True
    """
    if fmt == 'count':
        total_lines = sum(r['end_line'] - r['start_line'] + 1 for r in rows)
        print(total_lines)
        return True

    if fmt == 'csv':
        _format_csv_rows(
            ['rel_path', 'start_line', 'end_line'],
            [[r['rel_path'], r['start_line'], r['end_line']] for r in rows])
        return True

    # Table format
    if not rows:
        print(f'No unique code found for {target}')
        return True

    total_lines = sum(r['end_line'] - r['start_line'] + 1 for r in rows)
    print(f'{target}: {total_lines} unique lines in {len(rows)} ranges:')
    print(f"  {'File':<50} {'Lines':>15}")
    print('  ' + '-' * 65)
    for row in rows:
        path = row['rel_path']
        if len(path) > 47:
            path = '...' + path[-44:]
        lines = row['end_line'] - row['start_line'] + 1
        if row['start_line'] == row['end_line']:
            rng = str(row['start_line'])
        else:
            rng = f"{row['start_line']}-{row['end_line']}"
        print(f"  {path:<50} {rng:>10} ({lines:>3})")
    return True


def show_query_function_boards(rows, func_name, rel_path, start_line,
                               end_line, fmt):
    """Display which boards build a function.

    Args:
        rows (list of dict): Results from query_boards_for_line()
        func_name (str): Function name
        rel_path (str): File containing the function
        start_line (int): First line of the function
        end_line (int): Last line of the function
        fmt (str): Output format ('table', 'csv', or 'count')

    Returns:
        bool: True
    """
    if fmt == 'count':
        print(len(rows))
        return True

    if fmt == 'csv':
        _format_csv_rows(
            ['target', 'arch', 'file', 'start_line', 'end_line'],
            [[r['target'], r['arch'], rel_path, start_line, end_line]
             for r in rows])
        return True

    # Table format
    location = f'{rel_path}:{start_line}-{end_line}'
    if not rows:
        print(f'No boards build {func_name}() ({location})')
        return True

    print(f'{len(rows)} boards build {func_name}() ({location}):')
    print(f"  {'Board':<40} {'Arch':<12}")
    print('  ' + '-' * 52)
    for row in rows:
        print(f"  {row['target']:<40} {row['arch']:<12}")
    return True
