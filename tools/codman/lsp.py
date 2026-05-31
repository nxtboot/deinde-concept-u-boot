# SPDX-License-Identifier: GPL-2.0
#
# Copyright 2025 Canonical Ltd
#
"""LSP-based line-level analysis for source code.

This module provides functionality to analyse which lines in source files
are active vs inactive based on preprocessor conditionals, using clangd's
inactive regions feature via the Language Server Protocol (LSP).
"""

import concurrent.futures
import json
import multiprocessing
import os
import re
import tempfile
import time

from u_boot_pylib import tools, tout
from analyser import Analyser, FileResult
from lsp_client import LspClient


def create_compile_commands(build_dir, srcdir):
    """Create compile_commands.json using gen_compile_commands.py.

    Args:
        build_dir (str): Build directory path
        srcdir (str): Source directory path

    Returns:
        list: List of compile command entries
    """
    # Use the same pattern as gen_compile_commands.py
    line_pattern = re.compile(
        r'^(saved)?cmd_[^ ]*\.o := (?P<command_prefix>.* )'
        r'(?P<file_path>[^ ]*\.[cS]) *(;|$)')

    compile_commands = []

    # Walk through build directory looking for .cmd files
    filename_matcher = re.compile(r'^\..*\.cmd$')
    exclude_dirs = ['.git', 'Documentation', 'include', 'tools']

    for dirpath, dirnames, filenames in os.walk(build_dir, topdown=True):
        # Prune unwanted directories
        dirnames = [d for d in dirnames if d not in exclude_dirs]

        for filename in filenames:
            if not filename_matcher.match(filename):
                continue

            cmd_file = os.path.join(dirpath, filename)
            try:
                with open(cmd_file, 'rt', encoding='utf-8') as f:
                    result = line_pattern.match(f.readline())
                    if result:
                        command_prefix = result.group('command_prefix')
                        file_path = result.group('file_path')

                        # Clean up command prefix (handle escaped #)
                        prefix = command_prefix.replace(r'\#', '#').replace(
                            '$(pound)', '#')

                        # Get absolute path to source file
                        abs_path = os.path.realpath(
                            os.path.join(srcdir, file_path))
                        if os.path.exists(abs_path):
                            compile_commands.append({
                                'directory': srcdir,
                                'file': abs_path,
                                'command': prefix + file_path,
                            })
            except (OSError, IOError):
                continue

    return compile_commands


def worker(args):
    """Analyse a single source file using clangd LSP.

    Args:
        args (tuple): Tuple of (source_file, client)
            where client is a shared LspClient instance

    Returns:
        tuple: (source_file, inactive_regions, error_msg)
    """
    source_file, client = args

    try:
        # Read file content
        content = tools.read_file(source_file, binary=False)

        # Open the document
        client.notify('textDocument/didOpen', {
            'textDocument': {
                'uri': f'file://{source_file}',
                'languageId': 'c',
                'version': 1,
                'text': content
            }
        })

        # Wait for clangd to process and send notifications
        # Poll for inactive regions notification for this specific file
        max_wait = 10  # seconds
        start_time = time.time()
        inactive_regions = None

        while time.time() - start_time < max_wait:
            time.sleep(0.1)

            with client.lock:
                notifications = list(client.notifications)
                # Clear processed notifications to avoid buildup
                client.notifications = []

            for notif in notifications:
                method = notif.get('method', '')
                if method == 'textDocument/clangd.inactiveRegions':
                    params = notif.get('params', {})
                    uri = params.get('uri', '')
                    # Check if this notification is for our file
                    if uri == f'file://{source_file}':
                        inactive_regions = params.get('inactiveRegions', [])
                        break

            if inactive_regions is not None:
                break

        # Close the document to free resources
        client.notify('textDocument/didClose', {
            'textDocument': {
                'uri': f'file://{source_file}'
            }
        })

        if inactive_regions is None:
            # No inactive regions notification received
            # This could mean the file has no inactive code
            inactive_regions = []

        return (source_file, inactive_regions, None)

    except Exception as e:
        return (source_file, None, str(e))


class LspAnalyser(Analyser):  # pylint: disable=too-few-public-methods
    """Analyser that uses clangd LSP to determine active lines.

    This analyser uses the Language Server Protocol (LSP) with clangd to
    identify inactive preprocessor regions in source files.
    """

    def __init__(self, build_dir, srcdir, used_sources, keep_temps=False):
        """Set up the LSP analyser.

        Args:
            build_dir (str): Build directory containing .o and .cmd files
            srcdir (str): Path to source root directory
            used_sources (set): Set of source files that are compiled
            keep_temps (bool): If True, keep temporary files for debugging
        """
        super().__init__(srcdir, keep_temps)
        self.build_dir = build_dir
        self.used_sources = used_sources

    def extract_inactive_regions(self, jobs=None):
        """Extract inactive regions from source files using clangd.

        Args:
            jobs (int): Number of parallel jobs (None = use all CPUs)

        Returns:
            dict: Mapping of source file paths to lists of inactive regions
        """
        # Create compile commands database
        tout.progress('Building compile commands database...')
        compile_commands = create_compile_commands(self.build_dir, self.srcdir)

        # Filter to only .c and .S files that we need to analyse
        filtered_files = []
        for cmd in compile_commands:
            source_file = cmd['file']
            if source_file in self.used_sources:
                if source_file.endswith('.c') or source_file.endswith('.S'):
                    filtered_files.append(source_file)

        tout.progress(f'Found {len(filtered_files)} source files to analyse')

        if not filtered_files:
            return {}

        inactive = {}
        errors = []

        # Create a single clangd instance and use it for all files
        with tempfile.TemporaryDirectory() as tmpdir:
            # Write compile commands database
            compile_db = os.path.join(tmpdir, 'compile_commands.json')
            with open(compile_db, 'w', encoding='utf-8') as f:
                json.dump(compile_commands, f)

            # Start a single clangd server
            tout.progress('Starting clangd server...')
            with LspClient(['clangd', '--log=error',
                           f'--compile-commands-dir={tmpdir}']) as client:
                result = client.init(f'file://{self.srcdir}')
                if not result:
                    tout.error('Failed to start clangd')
                    return {}

                # Determine number of workers
                if jobs is None:
                    jobs = min(multiprocessing.cpu_count(), len(filtered_files))
                elif jobs <= 0:
                    jobs = 1

                tout.progress(f'Processing files with {jobs} workers...')

                # Use ThreadPoolExecutor to process files in parallel
                # (threads share the same clangd client)
                with concurrent.futures.ThreadPoolExecutor(
                        max_workers=jobs) as executor:
                    # Submit all tasks
                    future_to_file = {
                        executor.submit(worker, (source_file, client)):
                        source_file
                        for source_file in filtered_files
                    }

                    # Collect results as they complete
                    completed = 0
                    for future in concurrent.futures.as_completed(future_to_file):
                        source_file = future_to_file[future]
                        completed += 1
                        tout.progress(
                            f'Processing {completed}/{len(filtered_files)}: ' +
                            f'{os.path.basename(source_file)}...')

                        try:
                            source_file_result, inactive_regions, error_msg = (
                                future.result())

                            if error_msg:
                                errors.append(f'{source_file}: {error_msg}')
                            elif inactive_regions is not None:
                                inactive[source_file_result] = (
                                    inactive_regions)
                        except Exception as exc:
                            errors.append(f'{source_file}: {exc}')

        # Report any errors
        if errors:
            for error in errors[:10]:  # Show first 10 errors
                tout.error(error)
            if len(errors) > 10:
                tout.error(f'... and {len(errors) - 10} more errors')
            tout.warning(f'Failed to analyse {len(errors)} file(s) with LSP')

        return inactive

    def process(self, jobs=None, use_threads=False):
        """Perform line-level analysis using clangd LSP.

        Args:
            jobs (int): Number of parallel jobs (None = use all CPUs)
            use_threads (bool): Accepted for a uniform analyser interface; the
                LSP analyser already uses threads internally

        Returns:
            dict: Mapping of source file paths to FileResult named tuples
        """
        tout.progress('Extracting inactive regions using clangd LSP...')
        inactive_regions_map = self.extract_inactive_regions(jobs)

        file_results = {}
        for source_file in self.used_sources:
            # Only process .c and .S files
            if not (source_file.endswith('.c') or source_file.endswith('.S')):
                continue

            abs_path = os.path.realpath(source_file)
            inactive_regions = inactive_regions_map.get(abs_path, [])

            # Count total lines in the file
            total_lines = self.count_lines(abs_path)

            # Create line status dict
            line_status = {}
            # Set up all lines as active
            for i in range(1, total_lines + 1):
                line_status[i] = 'active'

            # Mark inactive lines based on regions
            # LSP uses 0-indexed line numbers
            for region in inactive_regions:
                start_line = region['start']['line'] + 1
                end_line = region['end']['line'] + 1
                # Mark lines as inactive (inclusive range)
                for line_num in range(start_line, end_line + 1):
                    if line_num <= total_lines:
                        line_status[line_num] = 'inactive'

            inactive_lines = len([s for s in line_status.values()
                                 if s == 'inactive'])
            active_lines = total_lines - inactive_lines

            file_results[abs_path] = FileResult(
                total_lines=total_lines,
                active_lines=active_lines,
                inactive_lines=inactive_lines,
                line_status=line_status
            )

        tout.info(f'Analysed {len(file_results)} files using clangd LSP')
        return file_results
