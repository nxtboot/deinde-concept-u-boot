# SPDX-License-Identifier: GPL-2.0
#
# Copyright 2025 Canonical Ltd
#
"""DWARF debug info-based line-level analysis for source code.

This module provides functionality to analyse which lines in source files
were compiled by extracting line information from DWARF debug data in
object files.
"""

import multiprocessing
import os
import subprocess
from collections import defaultdict

from u_boot_pylib import tout
from analyser import Analyser, FileResult


def worker(args):
    """Extract line numbers from DWARF debug info in an object file.

    Uses readelf --debug-dump=decodedline to get the line table, then parses
    section headers and line entries to determine which source lines were
    compiled into the object.

    Args:
        args (tuple): Tuple of (obj_path, build_dir, srcdir)

    Returns:
        tuple: (source_lines_dict, error_msg) where source_lines_dict is a
            mapping of source file paths to sets of line numbers, and
            error_msg is None on success or an error string on failure
    """
    obj_path, build_dir, srcdir = args
    source_lines = defaultdict(set)

    # Get the directory of the .o file relative to build_dir
    rel_to_build = os.path.relpath(obj_path, build_dir)
    obj_dir = os.path.dirname(rel_to_build)

    # Use readelf to extract decoded line information
    try:
        result = subprocess.run(
            ['readelf', '--debug-dump=decodedline', obj_path],
            capture_output=True, text=True, check=False,
            encoding='utf-8', errors='ignore')
        if result.returncode != 0:
            error_msg = (f'readelf failed on {obj_path} with return code '
                        f'{result.returncode}\nstderr: {result.stderr}')
            return (source_lines, error_msg)

        # Parse the output
        # Format is: Section header with full path, then data lines
        current_file = None
        for line in result.stdout.splitlines():
            # Skip header lines and empty lines
            if not line or line.startswith('Contents of') or \
               line.startswith('File name') or line.strip() == '' or \
               line.startswith(' '):
                continue

            # Look for section headers with full path (e.g., '/path/to/file.c:')
            if line.endswith(':'):
                header_path = line.rstrip(':')
                # Try to resolve the path
                if os.path.isabs(header_path):
                    # Absolute path in DWARF
                    abs_path = os.path.realpath(header_path)
                else:
                    # Relative path - try relative to srcdir and obj_dir
                    abs_path = os.path.realpath(
                        os.path.join(srcdir, obj_dir, header_path))
                    if not os.path.exists(abs_path):
                        abs_path = os.path.realpath(
                            os.path.join(srcdir, header_path))

                if os.path.exists(abs_path):
                    current_file = abs_path
                continue

            # Parse data lines - use current_file from section header
            if current_file:
                parts = line.split()
                if len(parts) >= 2:
                    try:
                        line_num = int(parts[1])
                        # Skip special line numbers (like '-')
                        if line_num > 0:
                            source_lines[current_file].add(line_num)
                    except (ValueError, IndexError):
                        continue
    except (OSError, subprocess.SubprocessError) as e:
        error_msg = f'Failed to execute readelf on {obj_path}: {e}'
        return (source_lines, error_msg)

    return (source_lines, None)


# pylint: disable=too-few-public-methods
class DwarfAnalyser(Analyser):
    """Analyser that uses DWARF debug info to determine active lines.

    This analyser extracts line number information from DWARF debug data in
    compiled object files to determine which source lines generated code.
    """
    def __init__(self, build_dir, srcdir, used_sources, keep_temps=False):
        """Initialise the DWARF analyser.

        Args:
            build_dir (str): Build directory containing .o files
            srcdir (str): Path to source root directory
            used_sources (set): Set of source files that are compiled
            keep_temps (bool): If True, keep temporary files for debugging
        """
        super().__init__(srcdir, keep_temps)
        self.build_dir = build_dir
        self.used_sources = used_sources

    def extract_lines(self, jobs=None):
        """Extract used line numbers from DWARF debug info in object files.

        Args:
            jobs (int): Number of parallel jobs (None = use all CPUs)

        Returns:
            dict: Mapping of source file paths to sets of line numbers that
                generated code
        """
        # Find all .o files
        obj_files = self.find_object_files(self.build_dir)

        if not obj_files:
            return defaultdict(set)

        # Prepare arguments for parallel processing
        args_list = [(obj_path, self.build_dir, self.srcdir)
                     for obj_path in obj_files]

        # Process in parallel (sequential when jobs=1 for thread safety)
        num_jobs = jobs if jobs else multiprocessing.cpu_count()
        if num_jobs <= 1:
            results = [worker(args) for args in args_list]
        else:
            with multiprocessing.Pool(num_jobs) as pool:
                results = pool.map(worker, args_list)

        # Merge results from all workers and check for errors
        source_lines = defaultdict(set)
        errors = []
        for result_dict, error_msg in results:
            if error_msg:
                errors.append(error_msg)
            else:
                for source_file, lines in result_dict.items():
                    source_lines[source_file].update(lines)

        # Report any errors
        if errors:
            for error in errors:
                tout.error(error)
            tout.fatal(f'readelf failed on {len(errors)} object file(s)')

        return source_lines

    def process(self, jobs=None):
        """Perform line-level analysis using DWARF debug info.

        Args:
            jobs (int): Number of parallel jobs (None = use all CPUs)

        Returns:
            dict: Mapping of source file paths to FileResult named tuples
        """
        tout.progress('Extracting DWARF line information...')
        dwarf_line_map = self.extract_lines(jobs)

        file_results = {}
        for source_file in self.used_sources:
            abs_path = os.path.realpath(source_file)
            used_lines = dwarf_line_map.get(abs_path, set())

            # Count total lines in the file
            total_lines = self.count_lines(abs_path)

            active_lines = len(used_lines)
            inactive_lines = total_lines - active_lines

            # Create line status dict
            line_status = {}
            for i in range(1, total_lines + 1):
                line_status[i] = 'active' if i in used_lines else 'inactive'

            file_results[abs_path] = FileResult(
                total_lines=total_lines,
                active_lines=active_lines,
                inactive_lines=inactive_lines,
                line_status=line_status
            )

        tout.info(f'Analysed {len(file_results)} files using DWARF debug info')
        return file_results
