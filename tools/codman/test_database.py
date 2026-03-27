#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
#
# Copyright 2025 Canonical Ltd
#
"""Tests for database.py - multi-board analysis database"""

import os
import sys
import tempfile
import unittest

# Test configuration
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)
sys.path.insert(0, os.path.join(SCRIPT_DIR, '..'))

# pylint: disable=wrong-import-position
import database
from analyser import FileResult


class TestLineStatusToRanges(unittest.TestCase):
    """Tests for line_status_to_ranges() conversion"""

    def test_empty(self):
        self.assertEqual(database.line_status_to_ranges({}), [])

    def test_all_active(self):
        status = {1: 'active', 2: 'active', 3: 'active', 4: 'active'}
        self.assertEqual(database.line_status_to_ranges(status),
                         [(1, 4)])

    def test_all_inactive(self):
        status = {1: 'inactive', 2: 'inactive', 3: 'inactive'}
        self.assertEqual(database.line_status_to_ranges(status), [])

    def test_single_active(self):
        status = {1: 'inactive', 2: 'active', 3: 'inactive'}
        self.assertEqual(database.line_status_to_ranges(status),
                         [(2, 2)])

    def test_two_ranges(self):
        status = {
            1: 'active', 2: 'active',
            3: 'inactive',
            4: 'active', 5: 'active', 6: 'active',
        }
        self.assertEqual(database.line_status_to_ranges(status),
                         [(1, 2), (4, 6)])

    def test_alternating(self):
        status = {1: 'active', 2: 'inactive', 3: 'active',
                  4: 'inactive', 5: 'active'}
        self.assertEqual(database.line_status_to_ranges(status),
                         [(1, 1), (3, 3), (5, 5)])

    def test_ends_active(self):
        status = {1: 'inactive', 2: 'active', 3: 'active'}
        self.assertEqual(database.line_status_to_ranges(status),
                         [(2, 3)])

    def test_non_contiguous_lines(self):
        """Lines may not be sequential (gaps in line_status dict)"""
        status = {1: 'active', 2: 'active', 10: 'active', 11: 'active'}
        self.assertEqual(database.line_status_to_ranges(status),
                         [(1, 2), (10, 11)])


class TestCodmanDatabase(unittest.TestCase):
    """Tests for CodmanDatabase class"""

    def setUp(self):
        self.db_fd, self.db_path = tempfile.mkstemp(suffix='.db')
        os.close(self.db_fd)
        self.db = database.CodmanDatabase(self.db_path)
        self.db.create_tables()

    def tearDown(self):
        self.db.close()
        os.unlink(self.db_path)

    def test_create_tables_idempotent(self):
        """Creating tables twice should not fail"""
        self.db.create_tables()

    def test_scan_info(self):
        self.db.set_scan_info('srcdir', '/tmp/src')
        self.assertEqual(self.db.get_scan_info('srcdir'), '/tmp/src')
        self.assertIsNone(self.db.get_scan_info('nonexistent'))

    def test_scan_info_update(self):
        self.db.set_scan_info('key', 'value1')
        self.db.set_scan_info('key', 'value2')
        self.assertEqual(self.db.get_scan_info('key'), 'value2')

    def test_add_board(self):
        board_id = self.db.add_board('sandbox', arch='sandbox',
                                     cpu='sandbox', defconfig='sandbox_defconfig')
        self.assertIsNotNone(board_id)
        self.assertTrue(self.db.has_board('sandbox'))
        self.assertFalse(self.db.has_board('nonexistent'))

    def test_add_board_failed(self):
        self.db.add_board('broken', status='build_failed')
        # has_board() only returns True for status='ok'
        self.assertFalse(self.db.has_board('broken'))

    def test_get_or_create_file(self):
        fid1 = self.db.get_or_create_file('common/main.c')
        fid2 = self.db.get_or_create_file('common/main.c')
        fid3 = self.db.get_or_create_file('drivers/video.c')
        self.assertEqual(fid1, fid2)
        self.assertNotEqual(fid1, fid3)

    def _add_test_data(self):
        """Add test boards with analysis results.

        Creates:
        - board_a: compiles common/main.c (all active) and
          drivers/video.c (lines 1-5 active, 6-8 inactive, 9-10 active)
        - board_b: compiles common/main.c (all active) only
        """
        srcdir = '/tmp/src'

        # Board A
        bid_a = self.db.add_board('board_a', arch='arm', cpu='armv7',
                                  defconfig='board_a_defconfig')
        results_a = {
            os.path.join(srcdir, 'common/main.c'): FileResult(
                total_lines=10, active_lines=10, inactive_lines=0,
                line_status={i: 'active' for i in range(1, 11)}),
            os.path.join(srcdir, 'drivers/video.c'): FileResult(
                total_lines=10, active_lines=7, inactive_lines=3,
                line_status={
                    1: 'active', 2: 'active', 3: 'active',
                    4: 'active', 5: 'active',
                    6: 'inactive', 7: 'inactive', 8: 'inactive',
                    9: 'active', 10: 'active',
                }),
        }
        self.db.store_board_results(bid_a, results_a, srcdir)

        # Board B
        bid_b = self.db.add_board('board_b', arch='x86', cpu='x86_64',
                                  defconfig='board_b_defconfig')
        results_b = {
            os.path.join(srcdir, 'common/main.c'): FileResult(
                total_lines=10, active_lines=10, inactive_lines=0,
                line_status={i: 'active' for i in range(1, 11)}),
        }
        self.db.store_board_results(bid_b, results_b, srcdir)

        return bid_a, bid_b

    def test_store_and_query_boards_for_file(self):
        self._add_test_data()

        # Both boards compile common/main.c
        rows = self.db.query_boards_for_file('common/main.c')
        targets = [r['target'] for r in rows]
        self.assertEqual(len(rows), 2)
        self.assertIn('board_a', targets)
        self.assertIn('board_b', targets)

        # Only board_a compiles drivers/video.c
        rows = self.db.query_boards_for_file('drivers/video.c')
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]['target'], 'board_a')

    def test_query_boards_for_file_wildcard(self):
        self._add_test_data()

        rows = self.db.query_boards_for_file('drivers/*')
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]['target'], 'board_a')

    def test_query_boards_for_file_arch_filter(self):
        self._add_test_data()

        rows = self.db.query_boards_for_file('common/main.c', arch='arm')
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]['target'], 'board_a')

    def test_query_boards_for_line(self):
        self._add_test_data()

        # Line 1 of common/main.c is active for both boards
        rows = self.db.query_boards_for_line('common/main.c', 1)
        self.assertEqual(len(rows), 2)

        # Line 3 of drivers/video.c is active only for board_a
        rows = self.db.query_boards_for_line('drivers/video.c', 3)
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]['target'], 'board_a')

        # Line 7 of drivers/video.c is inactive for board_a
        rows = self.db.query_boards_for_line('drivers/video.c', 7)
        self.assertEqual(len(rows), 0)

    def test_query_boards_for_line_arch_filter(self):
        self._add_test_data()

        rows = self.db.query_boards_for_line('common/main.c', 1, arch='x86')
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]['target'], 'board_b')

    def test_query_files_for_board(self):
        self._add_test_data()

        rows = self.db.query_files_for_board('board_a')
        paths = [r['rel_path'] for r in rows]
        self.assertEqual(len(rows), 2)
        self.assertIn('common/main.c', paths)
        self.assertIn('drivers/video.c', paths)

        rows = self.db.query_files_for_board('board_b')
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]['rel_path'], 'common/main.c')

    def test_query_files_for_board_filter(self):
        self._add_test_data()

        rows = self.db.query_files_for_board('board_a', 'drivers/*')
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]['rel_path'], 'drivers/video.c')

    def test_query_unique_code(self):
        self._add_test_data()

        # board_a has drivers/video.c which board_b doesn't have at all
        rows = self.db.query_unique_code('board_a')
        self.assertTrue(len(rows) > 0)
        paths = {r['rel_path'] for r in rows}
        self.assertIn('drivers/video.c', paths)

        # common/main.c is shared, so should not appear in unique code
        main_rows = [r for r in rows if r['rel_path'] == 'common/main.c']
        self.assertEqual(len(main_rows), 0)

    def test_query_info(self):
        self._add_test_data()

        info = self.db.query_info()
        self.assertEqual(info['board_count'], 2)
        self.assertEqual(info['file_count'], 2)
        self.assertTrue(info['board_file_count'] > 0)
        self.assertTrue(info['range_count'] > 0)
        self.assertIn('arm', info['arch_counts'])
        self.assertIn('x86', info['arch_counts'])

    def test_query_line_heatmap(self):
        self._add_test_data()

        rows = self.db.query_line_heatmap('common/main.c')
        # Both boards have all lines active, so board_count should be 2
        # for the range covering lines 1-10
        self.assertTrue(len(rows) > 0)
        for row in rows:
            self.assertEqual(row['board_count'], 2)

    def test_get_completed_targets(self):
        self._add_test_data()
        self.db.add_board('broken', status='build_failed')

        completed = self.db.get_completed_targets()
        self.assertIn('board_a', completed)
        self.assertIn('board_b', completed)
        self.assertNotIn('broken', completed)

    def test_active_ranges_correctness(self):
        """Verify that stored ranges match the original line_status"""
        self._add_test_data()

        # Check drivers/video.c for board_a:
        # lines 1-5 active, 6-8 inactive, 9-10 active
        # Should produce ranges: (1,5) and (9,10)
        rows = self.db.conn.execute('''
            SELECT ar.start_line, ar.end_line
            FROM active_ranges ar
            JOIN boards b ON ar.board_id = b.board_id
            JOIN files f ON ar.file_id = f.file_id
            WHERE b.target = 'board_a' AND f.rel_path = 'drivers/video.c'
            ORDER BY ar.start_line
        ''').fetchall()
        ranges = [(r['start_line'], r['end_line']) for r in rows]
        self.assertEqual(ranges, [(1, 5), (9, 10)])


if __name__ == '__main__':
    unittest.main(argv=['test_database.py'], verbosity=2)
