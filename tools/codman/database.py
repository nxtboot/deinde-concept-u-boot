# SPDX-License-Identifier: GPL-2.0
#
# Copyright 2025 Canonical Ltd
#
"""SQLite database for multi-board code analysis results.

This module stores per-board, per-file, per-line active/inactive status
using run-length encoding of active line ranges for storage efficiency.
"""

import os
import sqlite3
import time


# Current schema version
SCHEMA_VERSION = 1


class CodmanDatabase:
    """SQLite database for multi-board analysis results.

    The database stores which source files and lines are active for each
    board build, enabling cross-board queries such as "which boards compile
    line X of file Y?"

    Line-level data is stored as active ranges (start_line, end_line) rather
    than one row per line, which compresses ~100x for typical source files.
    """

    def __init__(self, db_path):
        """Open or create the database.

        Args:
            db_path (str): Path to SQLite database file
        """
        self.db_path = db_path
        self.conn = sqlite3.connect(db_path)
        self.conn.row_factory = sqlite3.Row
        self.conn.execute('PRAGMA journal_mode=WAL')
        self.conn.execute('PRAGMA synchronous=NORMAL')
        self.conn.execute('PRAGMA foreign_keys=ON')

        # In-memory cache for file path -> file_id lookups during scan
        self._file_cache = {}

    def close(self):
        """Close the database connection."""
        self.conn.close()

    def create_tables(self):
        """Create all tables and indexes if they don't exist."""
        cur = self.conn.cursor()

        cur.executescript('''
            CREATE TABLE IF NOT EXISTS schema_version (
                version INTEGER NOT NULL
            );

            CREATE TABLE IF NOT EXISTS boards (
                board_id    INTEGER PRIMARY KEY,
                target      TEXT NOT NULL UNIQUE,
                arch        TEXT,
                cpu         TEXT,
                soc         TEXT,
                vendor      TEXT,
                board_name  TEXT,
                defconfig   TEXT,
                scan_time   REAL,
                status      TEXT DEFAULT 'ok'
            );

            CREATE TABLE IF NOT EXISTS files (
                file_id     INTEGER PRIMARY KEY,
                rel_path    TEXT NOT NULL UNIQUE
            );

            CREATE TABLE IF NOT EXISTS board_files (
                board_id    INTEGER NOT NULL,
                file_id     INTEGER NOT NULL,
                total_lines INTEGER NOT NULL,
                active_lines INTEGER NOT NULL,
                inactive_lines INTEGER NOT NULL,
                PRIMARY KEY (board_id, file_id),
                FOREIGN KEY (board_id) REFERENCES boards(board_id),
                FOREIGN KEY (file_id) REFERENCES files(file_id)
            );

            CREATE TABLE IF NOT EXISTS active_ranges (
                board_id    INTEGER NOT NULL,
                file_id     INTEGER NOT NULL,
                start_line  INTEGER NOT NULL,
                end_line    INTEGER NOT NULL,
                FOREIGN KEY (board_id) REFERENCES boards(board_id),
                FOREIGN KEY (file_id) REFERENCES files(file_id)
            );

            CREATE TABLE IF NOT EXISTS scan_info (
                key         TEXT PRIMARY KEY,
                value       TEXT
            );

            CREATE INDEX IF NOT EXISTS idx_active_ranges_bf
                ON active_ranges(board_id, file_id);
            CREATE INDEX IF NOT EXISTS idx_active_ranges_fb
                ON active_ranges(file_id, board_id);
            CREATE INDEX IF NOT EXISTS idx_active_ranges_lines
                ON active_ranges(file_id, start_line, end_line);
            CREATE INDEX IF NOT EXISTS idx_board_files_file
                ON board_files(file_id);
            CREATE INDEX IF NOT EXISTS idx_files_path
                ON files(rel_path);
            CREATE INDEX IF NOT EXISTS idx_boards_target
                ON boards(target);
            CREATE INDEX IF NOT EXISTS idx_boards_arch
                ON boards(arch);
        ''')

        # Set schema version if not already set
        row = cur.execute(
            'SELECT COUNT(*) FROM schema_version').fetchone()
        if row[0] == 0:
            cur.execute('INSERT INTO schema_version VALUES (?)',
                        (SCHEMA_VERSION,))

        self.conn.commit()

    def set_scan_info(self, key, value):
        """Store scan metadata.

        Args:
            key (str): Metadata key
            value (str): Metadata value
        """
        self.conn.execute(
            'INSERT OR REPLACE INTO scan_info (key, value) VALUES (?, ?)',
            (key, str(value)))
        self.conn.commit()

    def get_scan_info(self, key):
        """Retrieve scan metadata.

        Args:
            key (str): Metadata key

        Returns:
            str: Value, or None if not found
        """
        row = self.conn.execute(
            'SELECT value FROM scan_info WHERE key = ?', (key,)).fetchone()
        return row['value'] if row else None

    def add_board(self, target, arch='', cpu='', soc='', vendor='',
                  board_name='', defconfig='', status='ok'):
        """Insert a board record.

        Args:
            target (str): Board target name (e.g. 'sandbox')
            arch (str): Architecture (e.g. 'arm')
            cpu (str): CPU name
            soc (str): SoC name
            vendor (str): Vendor name
            board_name (str): Board name
            defconfig (str): Defconfig filename
            status (str): 'ok', 'build_failed', or 'analysis_failed'

        Returns:
            int: board_id of the inserted row
        """
        cur = self.conn.execute(
            '''INSERT INTO boards
               (target, arch, cpu, soc, vendor, board_name, defconfig,
                scan_time, status)
               VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)''',
            (target, arch, cpu, soc, vendor, board_name, defconfig,
             time.time(), status))
        self.conn.commit()
        return cur.lastrowid

    def has_board(self, target):
        """Check if a board is already in the database.

        Args:
            target (str): Board target name

        Returns:
            bool: True if board exists with status 'ok'
        """
        row = self.conn.execute(
            "SELECT 1 FROM boards WHERE target = ? AND status = 'ok'",
            (target,)).fetchone()
        return row is not None

    def get_or_create_file(self, rel_path):
        """Get file_id for a path, creating if needed.

        Args:
            rel_path (str): File path relative to srcdir

        Returns:
            int: file_id
        """
        if rel_path in self._file_cache:
            return self._file_cache[rel_path]

        row = self.conn.execute(
            'SELECT file_id FROM files WHERE rel_path = ?',
            (rel_path,)).fetchone()
        if row:
            file_id = row['file_id']
        else:
            cur = self.conn.execute(
                'INSERT INTO files (rel_path) VALUES (?)', (rel_path,))
            file_id = cur.lastrowid

        self._file_cache[rel_path] = file_id
        return file_id

    def store_board_results(self, board_id, file_results, srcdir):
        """Store all analysis results for one board.

        Converts FileResult.line_status dicts to active_ranges rows.
        Uses batch inserts for performance.

        Args:
            board_id (int): Board ID from add_board()
            file_results (dict): Maps absolute file paths to FileResult
                namedtuples
            srcdir (str): Source directory root (for making paths relative)
        """
        board_file_rows = []
        range_rows = []

        for abs_path, result in file_results.items():
            rel_path = os.path.relpath(abs_path, srcdir)
            file_id = self.get_or_create_file(rel_path)

            board_file_rows.append((
                board_id, file_id, result.total_lines,
                result.active_lines, result.inactive_lines))

            # Convert line_status to active ranges
            ranges = line_status_to_ranges(result.line_status)
            for start, end in ranges:
                range_rows.append((board_id, file_id, start, end))

        # Batch insert
        self.conn.executemany(
            '''INSERT INTO board_files
               (board_id, file_id, total_lines, active_lines, inactive_lines)
               VALUES (?, ?, ?, ?, ?)''',
            board_file_rows)
        self.conn.executemany(
            '''INSERT INTO active_ranges
               (board_id, file_id, start_line, end_line)
               VALUES (?, ?, ?, ?)''',
            range_rows)
        self.conn.commit()

    # --- Query methods ---

    def query_boards_for_file(self, rel_path_pattern, arch=None):
        """Find which boards compile a given file.

        Args:
            rel_path_pattern (str): File path or LIKE pattern (use % for
                wildcard)
            arch (str): Optional architecture filter

        Returns:
            list of dict: Board info dicts with keys: target, arch,
                active_lines, total_lines
        """
        # Convert glob-style wildcards to SQL LIKE
        pattern = rel_path_pattern.replace('*', '%').replace('?', '_')
        use_like = '%' in pattern or '_' in pattern

        if use_like:
            sql = '''
                SELECT b.target, b.arch, bf.active_lines, bf.total_lines
                FROM board_files bf
                JOIN boards b ON bf.board_id = b.board_id
                JOIN files f ON bf.file_id = f.file_id
                WHERE f.rel_path LIKE ?
                  AND b.status = 'ok'
            '''
        else:
            sql = '''
                SELECT b.target, b.arch, bf.active_lines, bf.total_lines
                FROM board_files bf
                JOIN boards b ON bf.board_id = b.board_id
                JOIN files f ON bf.file_id = f.file_id
                WHERE f.rel_path = ?
                  AND b.status = 'ok'
            '''
        params = [pattern]

        if arch:
            sql += ' AND b.arch = ?'
            params.append(arch)

        sql += ' ORDER BY b.target'
        return [dict(row) for row in self.conn.execute(sql, params)]

    def query_boards_for_line(self, rel_path, line_num, arch=None):
        """Find which boards have a specific line active.

        Args:
            rel_path (str): File path relative to srcdir
            line_num (int): Line number (1-indexed)
            arch (str): Optional architecture filter

        Returns:
            list of dict: Board info dicts with keys: target, arch
        """
        sql = '''
            SELECT DISTINCT b.target, b.arch
            FROM active_ranges ar
            JOIN boards b ON ar.board_id = b.board_id
            JOIN files f ON ar.file_id = f.file_id
            WHERE f.rel_path = ?
              AND ar.start_line <= ?
              AND ar.end_line >= ?
              AND b.status = 'ok'
        '''
        params = [rel_path, line_num, line_num]

        if arch:
            sql += ' AND b.arch = ?'
            params.append(arch)

        sql += ' ORDER BY b.target'
        return [dict(row) for row in self.conn.execute(sql, params)]

    def query_files_for_board(self, target, file_pattern=None):
        """Find what files a board compiles.

        Args:
            target (str): Board target name
            file_pattern (str): Optional file path pattern filter

        Returns:
            list of dict: File info dicts with keys: rel_path, total_lines,
                active_lines, inactive_lines
        """
        sql = '''
            SELECT f.rel_path, bf.total_lines, bf.active_lines,
                   bf.inactive_lines
            FROM board_files bf
            JOIN files f ON bf.file_id = f.file_id
            JOIN boards b ON bf.board_id = b.board_id
            WHERE b.target = ? AND b.status = 'ok'
        '''
        params = [target]

        if file_pattern:
            pattern = file_pattern.replace('*', '%').replace('?', '_')
            sql += ' AND f.rel_path LIKE ?'
            params.append(pattern)

        sql += ' ORDER BY f.rel_path'
        return [dict(row) for row in self.conn.execute(sql, params)]

    def query_unique_code(self, target):
        """Find code that only this board compiles.

        Returns line ranges that are active for the given board but not
        active for any other board.

        Args:
            target (str): Board target name

        Returns:
            list of dict: Dicts with keys: rel_path, start_line, end_line
        """
        sql = '''
            SELECT f.rel_path, ar.start_line, ar.end_line
            FROM active_ranges ar
            JOIN files f ON ar.file_id = f.file_id
            JOIN boards b ON ar.board_id = b.board_id
            WHERE b.target = ? AND b.status = 'ok'
              AND NOT EXISTS (
                SELECT 1 FROM active_ranges ar2
                JOIN boards b2 ON ar2.board_id = b2.board_id
                WHERE ar2.file_id = ar.file_id
                  AND b2.target != ?
                  AND b2.status = 'ok'
                  AND ar2.start_line <= ar.end_line
                  AND ar2.end_line >= ar.start_line
              )
            ORDER BY f.rel_path, ar.start_line
        '''
        return [dict(row) for row in
                self.conn.execute(sql, (target, target))]

    def query_line_heatmap(self, rel_path):
        """Get per-line board count for a file.

        Returns the number of boards that have each line range active,
        useful for identifying heavily-shared vs board-specific code.

        Args:
            rel_path (str): File path relative to srcdir

        Returns:
            list of dict: Dicts with keys: start_line, end_line, board_count
        """
        sql = '''
            SELECT ar.start_line, ar.end_line,
                   COUNT(DISTINCT ar.board_id) as board_count
            FROM active_ranges ar
            JOIN files f ON ar.file_id = f.file_id
            JOIN boards b ON ar.board_id = b.board_id
            WHERE f.rel_path = ? AND b.status = 'ok'
            GROUP BY ar.start_line, ar.end_line
            ORDER BY ar.start_line
        '''
        return [dict(row) for row in self.conn.execute(sql, (rel_path,))]

    def query_info(self):
        """Get database statistics.

        Returns:
            dict: Statistics including board_count, file_count,
                range_count, db_size, and all scan_info entries
        """
        info = {}

        # Counts
        info['board_count'] = self.conn.execute(
            "SELECT COUNT(*) FROM boards WHERE status = 'ok'"
        ).fetchone()[0]
        info['board_failed'] = self.conn.execute(
            "SELECT COUNT(*) FROM boards WHERE status != 'ok'"
        ).fetchone()[0]
        info['file_count'] = self.conn.execute(
            'SELECT COUNT(*) FROM files').fetchone()[0]
        info['board_file_count'] = self.conn.execute(
            'SELECT COUNT(*) FROM board_files').fetchone()[0]
        info['range_count'] = self.conn.execute(
            'SELECT COUNT(*) FROM active_ranges').fetchone()[0]

        # Database file size
        try:
            info['db_size'] = os.path.getsize(self.db_path)
        except OSError:
            info['db_size'] = 0

        # Architecture breakdown
        rows = self.conn.execute(
            '''SELECT arch, COUNT(*) as cnt FROM boards
               WHERE status = 'ok' GROUP BY arch ORDER BY cnt DESC'''
        ).fetchall()
        info['arch_counts'] = {row['arch']: row['cnt'] for row in rows}

        # Scan info
        for row in self.conn.execute('SELECT key, value FROM scan_info'):
            info[row['key']] = row['value']

        return info

    def get_completed_targets(self):
        """Get set of target names already successfully scanned.

        Returns:
            set: Target names with status 'ok'
        """
        rows = self.conn.execute(
            "SELECT target FROM boards WHERE status = 'ok'").fetchall()
        return {row['target'] for row in rows}


def line_status_to_ranges(line_status):
    """Convert a line_status dict to a list of active line ranges.

    Groups consecutive active lines into (start, end) tuples for
    compact storage.

    Args:
        line_status (dict): Maps line numbers (int) to status strings
            ('active' or 'inactive')

    Returns:
        list of tuple: (start_line, end_line) pairs for active ranges,
            both inclusive
    """
    if not line_status:
        return []

    ranges = []
    start = None
    end = None

    prev = None
    for line_num in sorted(line_status):
        if line_status[line_num] == 'active':
            if start is None:
                start = line_num
            elif prev is not None and line_num > prev + 1:
                # Gap in line numbers — close the current range
                ranges.append((start, end))
                start = line_num
            end = line_num
        else:
            if start is not None:
                ranges.append((start, end))
                start = None
                end = None
        prev = line_num

    if start is not None:
        ranges.append((start, end))

    return ranges
