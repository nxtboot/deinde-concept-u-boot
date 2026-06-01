# SPDX-License-Identifier: GPL-2.0
#
# Copyright 2025 Canonical Ltd
#
"""DWARF debug info-based line-level analysis for source code.

This module provides functionality to analyse which lines in source files
were compiled by extracting line information from DWARF debug data in
object files.
"""

import os

from u_boot_pylib import dwarf_lines, tout
from analyser import Analyser, FileResult


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

    def extract_lines(self, jobs=None, use_threads=False):
        """Extract used line numbers from DWARF debug info in object files.

        Args:
            jobs (int): Number of parallel jobs (None = use all CPUs)
            use_threads (bool): Parallelise with threads rather than a process
                pool. Required when called from another thread (the scan
                worker threads); the work is dominated by the readelf
                subprocess, so threads parallelise it well.

        Returns:
            dict: Mapping of source file paths to sets of line numbers that
                generated code
        """
        source_lines, errors = dwarf_lines.extract_lines(
            self.build_dir, self.srcdir, jobs, use_threads=use_threads)
        if errors:
            for error in errors:
                tout.error(error)
            tout.fatal(f'readelf failed on {len(errors)} object file(s)')
        return source_lines

    def process(self, jobs=None, use_threads=False):
        """Perform line-level analysis using DWARF debug info.

        Args:
            jobs (int): Number of parallel jobs (None = use all CPUs)
            use_threads (bool): Parallelise with threads rather than processes
                (safe to call from a thread)

        Returns:
            dict: Mapping of source file paths to FileResult named tuples
        """
        tout.progress('Extracting DWARF line information...')
        dwarf_line_map = self.extract_lines(jobs, use_threads)

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
