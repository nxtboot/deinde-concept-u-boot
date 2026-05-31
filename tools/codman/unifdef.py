# SPDX-License-Identifier: GPL-2.0
#
# Copyright 2025 Canonical Ltd
#
"""Unifdef-based line-level analysis for source code.

This module provides functionality to analyse which lines in source files
are active vs inactive based on CONFIG_* settings, using the unifdef tool.
"""

import multiprocessing
import os
import re
import shutil
import subprocess
import tempfile
import time

from buildman import kconfiglib
from u_boot_pylib import tout
from analyser import Analyser, FileResult


def load_config(config_file, srcdir='.'):
    """Load CONFIG_* symbols from a .config file and Kconfig.

    Args:
        config_file (str): Path to .config file
        srcdir (str): Path to source directory (for Kconfig loading)

    Returns:
        tuple: (config_dict, error_message) where config_dict is a dictionary
            mapping CONFIG_* symbol names to values, and error_message is None
            on success or an error string on failure
    """
    config = {}

    # First, load from .config file
    with open(config_file, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()

            # Skip comments and blank lines
            if not line or line.startswith('#'):
                # Check for "is not set" pattern
                if ' is not set' in line:
                    # Extract CONFIG name: '# CONFIG_FOO is not set'
                    parts = line.split()
                    if len(parts) >= 2 and parts[1].startswith('CONFIG_'):
                        config_name = parts[1]
                        config[config_name] = None
                continue

            # Parse CONFIG_* assignments
            if '=' in line:
                name, value = line.split('=', 1)
                if name.startswith('CONFIG_'):
                    config[name] = value

    # Then, load all Kconfig symbols and set undefined ones to None
    # Only do this if we have a Kconfig file (i.e., in a real U-Boot tree)
    kconfig_path = os.path.join(srcdir, 'Kconfig')
    if not os.path.exists(kconfig_path):
        # No Kconfig - probably a test environment, just use .config values
        return config, None

    try:
        # Set environment variables needed by kconfiglib
        old_srctree = os.environ.get('srctree')
        old_ubootversion = os.environ.get('UBOOTVERSION')
        old_objdir = os.environ.get('KCONFIG_OBJDIR')

        os.environ['srctree'] = srcdir
        os.environ['UBOOTVERSION'] = 'dummy'
        os.environ['KCONFIG_OBJDIR'] = ''

        # Load Kconfig
        kconf = kconfiglib.Kconfig(warn=False)

        # Add all defined symbols that aren't already in config as None
        # kconfiglib provides names without CONFIG_ prefix
        for name in kconf.syms:
            config_name = f'CONFIG_{name}'
            if config_name not in config:
                # Symbol is defined in Kconfig but not in .config
                config[config_name] = None

        # Restore environment
        if old_srctree is not None:
            os.environ['srctree'] = old_srctree
        elif 'srctree' in os.environ:
            del os.environ['srctree']
        if old_ubootversion is not None:
            os.environ['UBOOTVERSION'] = old_ubootversion
        elif 'UBOOTVERSION' in os.environ:
            del os.environ['UBOOTVERSION']
        if old_objdir is not None:
            os.environ['KCONFIG_OBJDIR'] = old_objdir
        elif 'KCONFIG_OBJDIR' in os.environ:
            del os.environ['KCONFIG_OBJDIR']

        tout.progress(f'Loaded {len(kconf.syms)} Kconfig symbols')
    except (OSError, IOError, ValueError, ImportError) as e:
        # Return error if kconfiglib fails - we need all symbols for accurate analysis
        return None, f'Failed to load Kconfig symbols: {e}'

    return config, None


def match_lines(orig_lines, processed_output, source_file):
    """Match original and processed lines to determine which are active.

    Parses #line directives from unifdef -n output to determine exactly which
    lines from the original source are active vs inactive.

    Args:
        orig_lines (list): List of original source lines
        processed_output (str): Processed output from unifdef -n
        source_file (str): Path to source file (for matching #line directives)

    Returns:
        dict: Mapping of line numbers (1-indexed) to 'active'/'inactive' status
    """
    total_lines = len(orig_lines)
    line_status = {}

    # set up all lines as inactive
    for i in range(1, total_lines + 1):
        line_status[i] = 'inactive'

    # Parse #line directives to find which lines are active
    # Format: #line <number> '<file>'
    # When we see a #line directive, all following non-directive lines
    # come from that line number onward in the original file
    # If no #line directive appears at start, output starts at line 1
    current_line = 1  # Start at line 1 by default
    line_pattern = re.compile(r'^#line (\d+) "(.+)"$')
    source_basename = source_file.split('/')[-1]

    for output_line in processed_output.splitlines():
        # Check for #line directive
        match = line_pattern.match(output_line)
        if match:
            line_num = int(match.group(1))
            file_path = match.group(2)
            # Only track lines from our source file (unifdef may include
            # #line directives from headers)
            if file_path == source_file or file_path.endswith(source_basename):
                current_line = line_num
            else:
                # This is a #line for a different file (e.g., header)
                # Stop tracking until we see our file again
                current_line = None
        elif current_line is not None:
            # This is a real line from the source file
            if current_line <= total_lines:
                line_status[current_line] = 'active'
            current_line += 1

    return line_status


def worker(args):
    """Run unifdef on a source file to determine active/inactive lines.

    Uses unifdef with -k flag to process the file, then uses difflib to match
    original lines to processed lines to determine which are active vs inactive.

    Args:
        args (tuple): Tuple of (source_file, defs_file, unifdef_path,
            track_lines)

    Returns:
        Tuple of (source_file, total_lines, active_lines, inactive_lines,
            line_status, error_msg)
        line_status is a dict mapping line numbers to 'active'/'inactive', or
            {} if not tracked
        error_msg is None on success, or an error string on failure
    """
    source_file, defs_file, unifdef_path, track_lines = args

    try:
        with open(source_file, 'r', encoding='utf-8', errors='ignore') as f:
            orig_lines = f.readlines()

        total_lines = len(orig_lines)

        # Run unifdef to process the file
        # -n: add #line directives for tracking original line numbers
        # -E: error on unterminated conditionals
        # -f: use defs file
        cmd = [unifdef_path, '-n', '-E', '-f', defs_file]

        # Assembly files are not C, so process them as plain text (-t) and do
        # not parse C comments or character literals. Without this an
        # apostrophe in an '@' comment (e.g. "save R0's value") is read as an
        # unterminated char literal and unifdef aborts with an error.
        if source_file.endswith(('.S', '.s')):
            cmd.append('-t')
        cmd.append(source_file)

        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            encoding='utf-8',
            errors='ignore',
            check=False
        )

        if result.returncode > 1:
            # Error running unifdef
            # Check if it's an 'obfuscated' error - these are expected for
            # complex macros
            if 'Obfuscated' in result.stderr:
                # Obfuscated error - unifdef still produces output, so
                # continue processing (don't return early)
                pass
            else:
                # Real error
                error_msg = (f'unifdef failed on {source_file} with return '
                             f'code {result.returncode}\nstderr: '
                             f'{result.stderr}')
                return (source_file, 0, 0, 0, {}, error_msg)

        # Parse unifdef output to determine which lines are active
        if track_lines:
            line_status = match_lines(orig_lines, result.stdout, source_file)
            active_lines = len([s for s in line_status.values()
                               if s == 'active'])
        else:
            line_status = {}
            # Count non-#line directive lines in output
            active_lines = len([line for line in result.stdout.splitlines()
                               if not line.startswith('#line')])
        inactive_lines = total_lines - active_lines

        return (source_file, total_lines, active_lines, inactive_lines,
                line_status, None)
    except (OSError, IOError) as e:
        # Failed to execute unifdef or read source file
        error_msg = f'Failed to process {source_file}: {e}'
        return (source_file, 0, 0, 0, {}, error_msg)


class UnifdefAnalyser(Analyser):
    """Analyser that uses unifdef to determine active lines.

    This analyser handles the creation of a unifdef configuration file from
    CONFIG_* symbols and provides methods to analyse source files.

    Attributes:
        config (dict): Dictionary of CONFIG_* symbols and their values
        unifdef_cfg (str): Path to temporary unifdef configuration file
    """

    def __init__(self, config_file, srcdir, used_sources, unifdef_path,
                 include_headers, keep_temps=False):
        """Set up the analyser with config file path.

        Args:
            config_file (str): Path to .config file
            srcdir (str): Path to source root directory
            used_sources (set): Set of source files that are compiled
            unifdef_path (str): Path to unifdef executable
            include_headers (bool): If True, include header files; otherwise
                only .c and .S
            keep_temps (bool): If True, keep temporary files for debugging
        """
        super().__init__(srcdir, keep_temps)
        self.config_file = config_file
        self.used_sources = used_sources
        self.unifdef_path = unifdef_path
        self.include_headers = include_headers
        self.unifdef_cfg = None

    def _create_unifdef_config(self, config):
        """Create a temporary unifdef configuration file.

        Args:
            config (dict): Dictionary mapping CONFIG_* names to values

        Creates a file with -D and -U directives for each CONFIG_* symbol
        that can be passed to unifdef via -f flag.
        """
        # Create temporary file for unifdef directives
        fd, self.unifdef_cfg = tempfile.mkstemp(prefix='unifdef_',
                                                suffix='.cfg')

        with os.fdopen(fd, 'w') as f:
            for name, value in sorted(config.items()):
                if value is None or value == '' or value == 'n':
                    # Symbol is not set - undefine it
                    f.write(f'#undef {name}\n')
                elif value is True or value == 'y':
                    # Boolean CONFIG - define it as 1
                    f.write(f'#define {name} 1\n')
                elif value == 'm':
                    # Module - treat as not set for U-Boot
                    f.write(f'#undef {name}\n')
                elif (isinstance(value, str) and value.startswith('"') and
                      value.endswith('"')):
                    # String value with quotes - use as-is
                    f.write(f'#define {name} {value}\n')
                else:
                    # Numeric or other value
                    try:
                        # Try to parse as integer
                        int_val = int(value, 0)
                        f.write(f'#define {name} {int_val}\n')
                    except (ValueError, TypeError):
                        # Not an integer - escape and quote it
                        escaped_value = (str(value).replace('\\', '\\\\')
                                       .replace('"', '\\"'))
                        f.write(f'#define {name} "{escaped_value}"\n')

    def __del__(self):
        """Clean up temporary unifdef config file"""
        if self.unifdef_cfg and os.path.exists(self.unifdef_cfg):
            # Keep the file if requested
            if self.keep_temps:
                tout.debug(f'Keeping unifdef config file: {self.unifdef_cfg}')
                return
            try:
                os.unlink(self.unifdef_cfg)
            except OSError:
                pass

    def process(self, jobs=None):
        """Perform line-level analysis on used source files.

        Args:
            jobs (int): Number of parallel jobs (None = use all CPUs)

        Returns:
            Dictionary mapping source files to analysis results, or None on
                error
        """
        # Validate config file exists
        if not os.path.exists(self.config_file):
            tout.error(f'Config file not found: {self.config_file}')
            return None

        # Check if unifdef exists (check both absolute path and PATH)
        if os.path.isabs(self.unifdef_path):
            # Absolute path - check if it exists
            if not os.path.exists(self.unifdef_path):
                tout.fatal(f'unifdef not found at: {self.unifdef_path}')
        else:
            # Relative path or command name - check PATH
            unifdef_full = shutil.which(self.unifdef_path)
            if not unifdef_full:
                tout.fatal(f'unifdef not found in PATH: {self.unifdef_path}')
            self.unifdef_path = unifdef_full

        # Load configuration
        tout.progress('Loading configuration...')
        config, error = load_config(self.config_file, self.srcdir)
        if error:
            tout.fatal(error)
        tout.progress(f'Loaded {len(config)} config symbols')

        # Create unifdef config file
        self._create_unifdef_config(config)

        tout.progress('Analysing preprocessor conditionals...')
        file_results = {}

        # Filter sources to only .c and .S files unless include_headers is set
        used_sources = self.used_sources
        if not self.include_headers:
            filtered_sources = {s for s in used_sources
                                if s.endswith('.c') or s.endswith('.S')}
            excluded_count = len(used_sources) - len(filtered_sources)
            if excluded_count > 0:
                tout.progress(f'Excluding {excluded_count} header files ' +
                              '(use -i to include them)')
            used_sources = filtered_sources

        # Count lines in defs file
        with open(self.unifdef_cfg, 'r', encoding='utf-8') as f:
            defs_lines = len(f.readlines())

        # Use multiprocessing for parallel unifdef execution
        # Prepare arguments for parallel processing
        source_list = sorted(used_sources)
        worker_args = [(source_file, self.unifdef_cfg, self.unifdef_path, True)
                       for source_file in source_list]

        tout.progress(f'Running unifdef on {len(source_list)} files...')
        start_time = time.time()

        # If jobs=1, run directly without multiprocessing for easier debugging
        if jobs == 1:
            results = [worker(args) for args in worker_args]
        else:
            with multiprocessing.Pool(processes=jobs) as pool:
                results = list(pool.imap(worker, worker_args, chunksize=10))
        elapsed_time = time.time() - start_time

        # Convert results to file_results dict and calculate totals
        # Check for errors first
        total_source_lines = 0
        errors = []
        for (source_file, total_lines, active_lines, inactive_lines,
             line_status, error_msg) in results:
            if error_msg:
                errors.append(error_msg)
            else:
                file_results[source_file] = FileResult(
                    total_lines=total_lines,
                    active_lines=active_lines,
                    inactive_lines=inactive_lines,
                    line_status=line_status
                )
                total_source_lines += total_lines

        # Warn about any files unifdef could not process, but carry on. A
        # single unparsable file should not abort a whole multi-board scan;
        # such files are simply left without line-level data.
        if errors:
            for error in errors:
                tout.warning(error)
            tout.warning(f'unifdef failed on {len(errors)} file(s); '
                         'left unanalysed')

        kloc = total_source_lines // 1000
        tout.info(f'Analysed {len(file_results)} files ({kloc} kLOC, ' +
                  f'{defs_lines} defs) in {elapsed_time:.1f} seconds')
        tout.info(f'Unifdef directives file: {self.unifdef_cfg}')

        # Clean up temporary unifdef config file (unless in debug mode)
        if tout.verbose >= tout.DEBUG:
            tout.debug(f'Keeping unifdef directives file: {self.unifdef_cfg}')
        else:
            try:
                os.unlink(self.unifdef_cfg)
                tout.debug(f'Cleaned up {self.unifdef_cfg}')
            except OSError as e:
                tout.debug(f'Failed to clean up {self.unifdef_cfg}: {e}')

        return file_results
