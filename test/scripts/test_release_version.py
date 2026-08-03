#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
"""
Unit tests for the release version calculation script.
"""

import datetime
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

# Add scripts directory to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..',
                               'scripts'))

# pylint: disable=wrong-import-position,import-error
from release_version import (
    calculate_info, get_first_monday, get_next_even_month,
    update_makefile, ReleaseInfo, update_docs, generate_schedule,
    create_parser, main
)


class TestReleaseVersion(unittest.TestCase):
    """Test cases for release version calculation"""

    def setUp(self):
        """Set up test fixtures"""
        self.test_dir = tempfile.mkdtemp()

    def tearDown(self):
        """Clean up test fixtures"""
        shutil.rmtree(self.test_dir)

    def test_get_first_monday(self):
        """Test first Monday calculation for various months"""
        # Feb 2025 - 1st is Saturday, so first Monday is 3rd
        self.assertEqual(get_first_monday(2025, 2), datetime.date(2025, 2, 3))

        # Apr 2025 - 1st is Tuesday, so first Monday is 7th
        self.assertEqual(get_first_monday(2025, 4), datetime.date(2025, 4, 7))

        # Jun 2025 - 1st is Sunday, so first Monday is 2nd
        self.assertEqual(get_first_monday(2025, 6), datetime.date(2025, 6, 2))

        # Jan 2025 - 1st is Wednesday, so first Monday is 6th
        self.assertEqual(get_first_monday(2025, 1), datetime.date(2025, 1, 6))

    def test_get_next_even_month(self):
        """Test next even month calculation"""
        # From January (odd) -> February (even)
        self.assertEqual(
            get_next_even_month(datetime.date(2025, 1, 15)), (2025, 2))

        # From February (even) -> April (skip March)
        self.assertEqual(
            get_next_even_month(datetime.date(2025, 2, 15)), (2025, 4))

        # From December (even) -> February next year
        self.assertEqual(
            get_next_even_month(datetime.date(2025, 12, 15)), (2026, 2))

        # From November (odd) -> December (even)
        self.assertEqual(
            get_next_even_month(datetime.date(2025, 11, 15)), (2025, 12))

    def test_final_release_detection(self):
        """Test detection of final release dates"""
        # 03-Feb-25 is first Monday of February (even month)
        release_info = calculate_info(datetime.date(2025, 2, 3))
        self.assertTrue(release_info.is_final)
        self.assertEqual(release_info.version, '2025.02')
        self.assertEqual(release_info.year, 2025)
        self.assertEqual(release_info.month, 2)

        # 07-Apr-25 is first Monday of April (even month)
        release_info = calculate_info(datetime.date(2025, 4, 7))
        self.assertTrue(release_info.is_final)
        self.assertEqual(release_info.version, '2025.04')

        # 04-Feb-25 is Tuesday after first Monday - should be RC
        release_info = calculate_info(datetime.date(2025, 2, 4))
        self.assertFalse(release_info.is_final)

    def test_rc1_calculation(self):
        """Test RC1 calculation (6 weeks before final release)"""
        # 23-Dec-24 is 6 weeks before 03-Feb-25 first Monday
        release_info = calculate_info(datetime.date(2024, 12, 23))
        self.assertFalse(release_info.is_final)
        self.assertEqual(release_info.version, '2025.02-rc1')
        self.assertEqual(release_info.rc_number, 1)
        self.assertEqual(release_info.weeks_until_final, 6)

    def test_rc2_calculation(self):
        """Test RC2 calculation (4 weeks before final release)"""
        # 06-Jan-25 is 4 weeks before 03-Feb first Monday
        release_info = calculate_info(datetime.date(2025, 1, 6))
        self.assertFalse(release_info.is_final)
        self.assertEqual(release_info.version, '2025.02-rc2')
        self.assertEqual(release_info.rc_number, 2)
        self.assertEqual(release_info.weeks_until_final, 4)

    def test_rc3_calculation(self):
        """Test RC3 calculation (2 weeks before final release)"""
        # 20-Jan-25 is 2 weeks before 03-Feb-25 first Monday
        release_info = calculate_info(datetime.date(2025, 1, 20))
        self.assertFalse(release_info.is_final)
        self.assertEqual(release_info.version, '2025.02-rc3')
        self.assertEqual(release_info.rc_number, 3)
        self.assertEqual(release_info.weeks_until_final, 2)

    def test_dead_period(self):
        """Test dead period for dates too far from release"""
        # 01-Dec-24 is 9+ weeks before 03-Feb-25 -
        # should be dead period
        release_info = calculate_info(datetime.date(2024, 12, 1))
        self.assertFalse(release_info.is_final)
        self.assertTrue(release_info.is_dead_period)
        self.assertEqual(release_info.version, '')
        self.assertEqual(release_info.year, 2025)
        self.assertEqual(release_info.month, 2)
        self.assertGreater(release_info.weeks_until_final, 6)

    def test_cross_year_boundary(self):
        """Test calculations across year boundaries"""
        # 15-Dec-25 should target Feb 2026 but is in
        # dead period (7 weeks)
        release_info = calculate_info(datetime.date(2025, 12, 15))
        self.assertFalse(release_info.is_final)
        self.assertTrue(release_info.is_dead_period)
        self.assertEqual(release_info.year, 2026)
        self.assertEqual(release_info.month, 2)
        self.assertEqual(release_info.version, '')

        # 22-Dec-25 (Monday) should be rc1 for Feb 2026 (6 weeks)
        release_info = calculate_info(datetime.date(2025, 12, 22))
        self.assertFalse(release_info.is_final)
        self.assertFalse(release_info.is_dead_period)
        self.assertEqual(release_info.year, 2026)
        self.assertEqual(release_info.month, 2)
        self.assertEqual(release_info.version, '2026.02-rc1')

    def test_odd_month_targeting(self):
        """Test that odd months target the next even month"""
        # January targets February
        release_info = calculate_info(datetime.date(2025, 1, 15))
        self.assertEqual(release_info.month, 2)

        # March targets April
        release_info = calculate_info(datetime.date(2025, 3, 15))
        self.assertEqual(release_info.month, 4)

        # November targets December
        release_info = calculate_info(datetime.date(2025, 11, 15))
        self.assertEqual(release_info.month, 12)

    def test_makefile_update_final_release(self):
        """Test Makefile update for final release"""
        makefile_content = """# U-Boot Makefile
VERSION = 2024
PATCHLEVEL = 12
SUBLEVEL = 1
EXTRAVERSION = -rc1
NAME = U-Boot
"""

        makefile_path = os.path.join(self.test_dir, 'Makefile')
        with open(makefile_path, 'w', encoding='utf-8') as f:
            f.write(makefile_content)

        release_info = ReleaseInfo(
            is_final=True,
            version='2025.02',
            year=2025,
            month=2
        )

        changes_made = update_makefile(release_info, makefile_path)
        self.assertTrue(changes_made)

        with open(makefile_path, 'r', encoding='utf-8') as f:
            updated_content = f.read()

        self.assertIn('VERSION = 2025', updated_content)
        self.assertIn('PATCHLEVEL = 02', updated_content)
        self.assertIn('SUBLEVEL =', updated_content)
        self.assertIn('EXTRAVERSION =', updated_content)

    def test_makefile_update_rc_release(self):
        """Test Makefile update for RC release"""
        makefile_content = '''# U-Boot Makefile
VERSION = 2024
PATCHLEVEL = 12
SUBLEVEL = 1
EXTRAVERSION = -rc1
NAME = U-Boot
'''

        makefile_path = os.path.join(self.test_dir, 'Makefile')
        with open(makefile_path, 'w', encoding='utf-8') as f:
            f.write(makefile_content)

        release_info = ReleaseInfo(
            is_final=False,
            version='2025.02-rc2',
            year=2025,
            month=2,
            rc_number=2,
            weeks_until_final=4
        )

        changes_made = update_makefile(release_info, makefile_path)
        self.assertTrue(changes_made)

        with open(makefile_path, 'r', encoding='utf-8') as f:
            updated_content = f.read()

        self.assertIn('VERSION = 2025', updated_content)
        self.assertIn('PATCHLEVEL = 02', updated_content)
        self.assertIn('SUBLEVEL =', updated_content)
        self.assertIn('EXTRAVERSION = -rc2', updated_content)

    def test_makefile_no_changes_needed(self):
        """Test Makefile update when no changes are needed"""
        makefile_content = '''# U-Boot Makefile
VERSION = 2025
PATCHLEVEL = 02
SUBLEVEL =
EXTRAVERSION =
NAME = U-Boot
'''

        makefile_path = os.path.join(self.test_dir, 'Makefile')
        with open(makefile_path, 'w', encoding='utf-8') as f:
            f.write(makefile_content)

        release_info = ReleaseInfo(
            is_final=True,
            version='2025.02',
            year=2025,
            month=2
        )

        changes_made = update_makefile(release_info, makefile_path)
        self.assertFalse(changes_made)

    def test_release_docs_update(self):
        """Test release documentation update functionality"""
        docs_path = os.path.join(self.test_dir, 'concept_releases.rst')
        # Create initial content
        with open(docs_path, 'w', encoding='utf-8') as f:
            f.write('''.. SPDX-License-Identifier: GPL-2.0+

Deinde Concept Releases
=======================

This document tracks all concept releases of U-Boot.

Release History
---------------

''')

        release_info = ReleaseInfo(
            is_final=True,
            version='2025.02',
            year=2025,
            month=2
        )

        changes_made = update_docs(release_info, 'abc123def', docs_path,
                                    'Version 2025.02 final release')
        self.assertTrue(changes_made)

        with open(docs_path, 'r', encoding='utf-8') as f:
            content = f.read()

        self.assertIn('**2025.02** - Final Release', content)
        self.assertIn(':Commit: abc123def', content)
        self.assertIn(':Subject: Version 2025.02 final release', content)
        self.assertIn(':Date:', content)

        # Test duplicate detection
        changes_made = update_docs(release_info, 'abc123def', docs_path,
                                   'Version 2025.02 final release')
        self.assertFalse(changes_made)  # Should not add duplicate

    def test_release_docs_dead_period(self):
        """Test release documentation during dead period"""
        release_info = ReleaseInfo(
            is_final=False,
            version='',
            year=2025,
            month=2,
            is_dead_period=True
        )

        changes_made = update_docs(
            release_info, 'abc123def', '/tmp/nonexistent.rst')
        self.assertFalse(changes_made)  # Should not update during dead period

    def test_release_docs_file_read_error(self):
        """Test release documentation with file read error"""
        # Use a path that will trigger the FileNotFoundError in the
        # except clause
        release_info = ReleaseInfo(
            is_final=True,
            version='2025.02',
            year=2025,
            month=2
        )

        docs_path = os.path.join(self.test_dir, 'concept_releases.rst')
        # Create a file that exists for the initial check
        # but will fail on read due to mocking
        with open(docs_path, 'w', encoding='utf-8') as f:
            f.write('test')

        # Mock the file reading to raise an exception
        with patch('builtins.open',
                   side_effect=FileNotFoundError('Mocked error')):
            with patch('os.path.exists',
                       return_value=True):  # File exists check passes
                with patch('sys.stderr',
                           new_callable=io.StringIO) as mock_stderr:
                    changes_made = update_docs(
                        release_info, 'abc123def', docs_path)
                    self.assertFalse(changes_made)
                    self.assertIn('Error: Could not read',
                                  mock_stderr.getvalue())

    def test_release_docs_no_header_fallback(self):
        """Test release documentation fallback when no Release History
        header found."""
        docs_path = os.path.join(self.test_dir, 'concept_releases.rst')
        # Create file without proper header structure
        with open(docs_path, 'w', encoding='utf-8') as f:
            f.write('Some content without Release History header')

        release_info = ReleaseInfo(
            is_final=False,
            version='2025.02-rc1',
            year=2025,
            month=2,
            rc_number=1
        )

        changes_made = update_docs(release_info, 'def456ghi', docs_path)
        self.assertTrue(changes_made)

        with open(docs_path, 'r', encoding='utf-8') as f:
            content = f.read()

        self.assertIn('**2025.02-rc1** - Release Candidate', content)
        self.assertIn(':Commit: def456ghi', content)

    def test_generate_schedule_functionality(self):
        """Test schedule generation works correctly"""
        schedule = generate_schedule()

        # Should contain basic structure
        self.assertIn('Next Release', schedule)
        self.assertIn('------------', schedule)
        self.assertIn('Release candidate schedule:', schedule)
        self.assertIn('-rc3', schedule)
        self.assertIn('-rc2', schedule)
        self.assertIn('-rc1', schedule)
        self.assertIn('Mon ', schedule)


    def test_update_docs_with_next_release_section(self):
        """Test documentation update includes Next Release section"""
        docs_path = os.path.join(self.test_dir, 'concept_releases.rst')
        # Create initial content
        with open(docs_path, 'w', encoding='utf-8') as f:
            f.write('''.. SPDX-License-Identifier: GPL-2.0+

Deinde Concept Releases
=======================

Release History
---------------

''')

        release_info = ReleaseInfo(
            is_final=False,
            version='2025.02-rc1',
            year=2025,
            month=2,
            rc_number=1
        )

        changes_made = update_docs(release_info, 'abc123', docs_path)
        self.assertTrue(changes_made)

        with open(docs_path, 'r', encoding='utf-8') as f:
            content = f.read()

        # Should have Next Release section
        self.assertIn('Next Release', content)
        self.assertIn('------------', content)
        self.assertIn('Release candidate schedule:', content)

        # Should still have the release entry
        self.assertIn('**2025.02-rc1** - Release Candidate', content)
        self.assertIn(':Commit: abc123', content)


class TestReleaseHistory(unittest.TestCase):
    """Test release history management functionality"""

    def setUp(self):
        """Set up test fixtures"""
        self.test_dir = tempfile.mkdtemp()

    def tearDown(self):
        """Clean up test fixtures"""
        shutil.rmtree(self.test_dir)

    def test_release_history_ordering(self):
        """Test that releases are added in correct chronological order"""
        docs_path = os.path.join(self.test_dir, 'concept_releases.rst')
        with open(docs_path, 'w', encoding='utf-8') as f:
            f.write('''.. SPDX-License-Identifier: GPL-2.0+

Deinde Concept Releases
=======================

Release History
---------------

**2025.02-rc2** - Release Candidate
   :Date: 2025-01-15
   :Commit: def456

''')

        # Add an older release
        older_info = ReleaseInfo(
            is_final=False,
            version='2025.02-rc1',
            year=2025,
            month=2,
            rc_number=1
        )

        changes_made = update_docs(older_info, 'abc123', docs_path)
        self.assertTrue(changes_made)

        with open(docs_path, 'r', encoding='utf-8') as f:
            content = f.read()

        # rc1 should be added at the top (most recent first)
        lines = content.split('\n')
        rc1_line = next(i for i, line in enumerate(lines)
                       if '**2025.02-rc1**' in line)
        rc2_line = next(i for i, line in enumerate(lines)
                       if '**2025.02-rc2**' in line)

        self.assertLess(rc1_line, rc2_line,
                       'Newer releases should appear before older ones')

    def test_release_history_duplicate_prevention(self):
        """Test that duplicate releases are not added"""
        docs_path = os.path.join(self.test_dir, 'concept_releases.rst')
        with open(docs_path, 'w', encoding='utf-8') as f:
            f.write('''.. SPDX-License-Identifier: GPL-2.0+

Deinde Concept Releases
=======================

Release History
---------------

**2025.02-rc1** - Release Candidate
   :Date: 2025-01-20
   :Commit: abc123

''')

        # Try to add the same release again
        duplicate_info = ReleaseInfo(
            is_final=False,
            version='2025.02-rc1',
            year=2025,
            month=2,
            rc_number=1
        )

        changes_made = update_docs(duplicate_info, 'xyz789', docs_path)
        self.assertFalse(changes_made,
                        'Should not add duplicate release entries')

        with open(docs_path, 'r', encoding='utf-8') as f:
            content = f.read()

        # Should only have one instance of the release
        self.assertEqual(content.count('**2025.02-rc1**'), 1,
                        'Should only have one entry for each release')

    def test_release_history_final_vs_rc(self):
        """Test proper handling of final releases vs RCs"""
        docs_path = os.path.join(self.test_dir, 'concept_releases.rst')
        with open(docs_path, 'w', encoding='utf-8') as f:
            f.write('''.. SPDX-License-Identifier: GPL-2.0+

Deinde Concept Releases
=======================

Release History
---------------

''')

        # Add an RC first
        rc_info = ReleaseInfo(
            is_final=False,
            version='2025.02-rc1',
            year=2025,
            month=2,
            rc_number=1
        )
        update_docs(rc_info, 'rc123', docs_path)

        # Add final release
        final_info = ReleaseInfo(
            is_final=True,
            version='2025.02',
            year=2025,
            month=2
        )
        update_docs(final_info, 'final456', docs_path)

        with open(docs_path, 'r', encoding='utf-8') as f:
            content = f.read()

        # Should have both releases with correct types
        self.assertIn('**2025.02** - Final Release', content)
        self.assertIn('**2025.02-rc1** - Release Candidate', content)

        # Final should come before RC (newer first)
        final_pos = content.find('**2025.02** - Final Release')
        rc_pos = content.find('**2025.02-rc1** - Release Candidate')
        self.assertLess(final_pos, rc_pos,
                       'Final release should appear before RC')

    def test_release_history_commit_tracking(self):
        """Test that commit SHAs are properly tracked"""
        docs_path = os.path.join(self.test_dir, 'concept_releases.rst')
        with open(docs_path, 'w', encoding='utf-8') as f:
            f.write('''.. SPDX-License-Identifier: GPL-2.0+

Deinde Concept Releases
=======================

Release History
---------------

''')

        # Test with commit SHA
        info_with_commit = ReleaseInfo(
            is_final=True,
            version='2025.02',
            year=2025,
            month=2
        )
        update_docs(info_with_commit, '1a2b3c4d5e6f', docs_path)

        # Test without commit SHA
        info_no_commit = ReleaseInfo(
            is_final=False,
            version='2025.02-rc1',
            year=2025,
            month=2,
            rc_number=1
        )
        update_docs(info_no_commit, '', docs_path)

        with open(docs_path, 'r', encoding='utf-8') as f:
            content = f.read()

        # Should show actual commit for first, N/A for second
        self.assertIn(':Commit: 1a2b3c4d5e6f', content)
        self.assertIn(':Commit: N/A', content)

    def test_release_history_cross_year_releases(self):
        """Test release history across year boundaries"""
        docs_path = os.path.join(self.test_dir, 'concept_releases.rst')
        with open(docs_path, 'w', encoding='utf-8') as f:
            f.write('''.. SPDX-License-Identifier: GPL-2.0+

Deinde Concept Releases
=======================

Release History
---------------

''')

        # Add 2024 release
        old_info = ReleaseInfo(
            is_final=True,
            version='2024.12',
            year=2024,
            month=12
        )
        update_docs(old_info, 'old123', docs_path)

        # Add 2025 release
        new_info = ReleaseInfo(
            is_final=True,
            version='2025.02',
            year=2025,
            month=2
        )
        update_docs(new_info, 'new456', docs_path)

        with open(docs_path, 'r', encoding='utf-8') as f:
            content = f.read()

        # Should have both years, with 2025 first
        self.assertIn('**2025.02**', content)
        self.assertIn('**2024.12**', content)

        pos_2025 = content.find('**2025.02**')
        pos_2024 = content.find('**2024.12**')
        self.assertLess(pos_2025, pos_2024,
                       '2025 release should appear before 2024 release')

    def test_update_docs_with_existing_next_release_and_other_section(self):
        """Test updating docs with existing Next Release section"""
        docs_path = os.path.join(self.test_dir, 'concept_releases.rst')
        # Create content with existing Next Release and another section
        with open(docs_path, 'w', encoding='utf-8') as f:
            f.write('''.. SPDX-License-Identifier: GPL-2.0+

Deinde Concept Releases
=======================

Next Release
------------

Old schedule content here.

Some Other Section
------------------

Other content here.

Release History
---------------

''')

        release_info = ReleaseInfo(
            is_final=False,
            version='2025.02-rc1',
            year=2025,
            month=2,
            rc_number=1
        )

        changes_made = update_docs(release_info, 'abc123', docs_path)
        self.assertTrue(changes_made)

        with open(docs_path, 'r', encoding='utf-8') as f:
            content = f.read()

        # Should have updated Next Release section
        self.assertIn('Next Release', content)
        self.assertIn('The next final release is scheduled', content)

        # Should still have preserved the other section content
        self.assertIn('Other content here', content)
        # The section header gets replaced with dashes, but content is preserved

        # Should have the release entry
        self.assertIn('**2025.02-rc1** - Release Candidate', content)


class TestMainFunction(unittest.TestCase):
    """Test the main function and command-line interface"""

    def setUp(self):
        """Set up test fixtures"""
        self.test_dir = tempfile.mkdtemp()

    def tearDown(self):
        """Clean up test fixtures"""
        shutil.rmtree(self.test_dir)

    def test_main_version_format(self):
        """Test main function with version output format"""
        parser = create_parser()
        args = parser.parse_args(
            ['--date', '2025-02-03', '--format', 'version'])

        with patch('sys.stdout', new_callable=io.StringIO) as mock_stdout:
            result = main(args)
            self.assertEqual(result, 0)
            self.assertEqual(mock_stdout.getvalue().strip(), '2025.02')

    def test_main_json_format(self):
        """Test main function with JSON output format"""
        parser = create_parser()
        args = parser.parse_args(['--date', '2025-01-20', '--format', 'json'])

        with patch('sys.stdout', new_callable=io.StringIO) as mock_stdout:
            result = main(args)
            self.assertEqual(result, 0)
            output = json.loads(mock_stdout.getvalue())
            self.assertFalse(output['is_final'])
            self.assertEqual(output['version'], '2025.02-rc3')

    def test_main_shell_format(self):
        """Test main function with shell output format"""
        parser = create_parser()
        args = parser.parse_args(['--date', '2025-01-20', '--format', 'shell'])

        with patch('sys.stdout', new_callable=io.StringIO) as mock_stdout:
            result = main(args)
            self.assertEqual(result, 0)
            output = mock_stdout.getvalue()

            # Should contain shell variable assignments
            self.assertIn('IS_FINAL=false', output)
            self.assertIn('VERSION=2025.02-rc3', output)
            self.assertIn('YEAR=2025', output)
            self.assertIn('MONTH=02', output)
            self.assertIn('RC_NUMBER=3', output)
            self.assertIn('WEEKS_UNTIL_FINAL=2', output)

    def test_main_shell_format_final_release(self):
        """Test main function with shell format for final release"""
        parser = create_parser()
        args = parser.parse_args(['--date', '2025-02-03', '--format', 'shell'])

        with patch('sys.stdout', new_callable=io.StringIO) as mock_stdout:
            result = main(args)
            self.assertEqual(result, 0)
            output = mock_stdout.getvalue()

            # Should contain shell variables for final release
            self.assertIn('IS_FINAL=true', output)
            self.assertIn('VERSION=2025.02', output)
            self.assertIn('YEAR=2025', output)
            self.assertIn('MONTH=02', output)
            # Should NOT contain RC-specific variables
            self.assertNotIn('RC_NUMBER=', output)
            self.assertNotIn('WEEKS_UNTIL_FINAL=', output)

    def test_main_shell_format_dead_period(self):
        """Test main function with shell format during dead period"""
        parser = create_parser()
        args = parser.parse_args(['--date', '2024-08-01', '--format', 'shell'])

        with patch('sys.stdout', new_callable=io.StringIO) as mock_stdout:
            result = main(args)
            self.assertEqual(result, 0)
            output = mock_stdout.getvalue()

            # Should contain dead period shell variables
            self.assertIn('IS_DEAD_PERIOD=true', output)
            self.assertIn('WEEKS_UNTIL_FINAL=', output)
            self.assertIn('NEXT_RELEASE_YEAR=', output)
            self.assertIn('NEXT_RELEASE_MONTH=', output)

    def test_main_dead_period(self):
        """Test main function during dead period"""
        parser = create_parser()
        args = parser.parse_args(
            ['--date', '2024-08-01', '--format', 'version'])

        with patch('sys.stdout', new_callable=io.StringIO) as mock_stdout:
            result = main(args)
            self.assertEqual(result, 0)
            self.assertEqual(mock_stdout.getvalue().strip(), 'NO_RELEASE')

    def test_main_invalid_date(self):
        """Test main function with invalid date"""
        parser = create_parser()
        args = parser.parse_args(['--date', 'invalid-date'])

        with patch('sys.stderr', new_callable=io.StringIO) as mock_stderr:
            result = main(args)
            self.assertEqual(result, 1)
            self.assertIn('Invalid date format', mock_stderr.getvalue())

    def test_main_makefile_update_dead_period(self):
        """Test main function updating Makefile during dead period"""
        parser = create_parser()
        args = parser.parse_args(['--date', '2024-08-01', '--update-makefile'])

        with patch('sys.stdout', new_callable=io.StringIO) as mock_stdout:
            result = main(args)
            self.assertEqual(result, 1)
            self.assertIn('No release during dead period',
                          mock_stdout.getvalue())

    def test_main_release_docs_update_dead_period(self):
        """Test main function trying to update release docs
        during dead period."""
        parser = create_parser()
        args = parser.parse_args(
            ['--date', '2024-08-01', '--update-release-docs'])

        with patch('sys.stdout', new_callable=io.StringIO) as mock_stdout:
            result = main(args)
            self.assertEqual(result, 1)
            self.assertIn('No release during dead period',
                          mock_stdout.getvalue())

    def test_main_makefile_update_success(self):
        """Test main function successfully updating Makefile"""
        makefile_path = os.path.join(self.test_dir, 'Makefile')
        with open(makefile_path, 'w', encoding='utf-8') as f:
            f.write('VERSION = 2024\nPATCHLEVEL = 12\nSUBLEVEL = 1\n'
                    'EXTRAVERSION = -rc1\n')

        parser = create_parser()
        args = parser.parse_args(['--date', '2025-02-03',
                                  '--update-makefile', '--makefile',
                                  makefile_path])

        with patch('sys.stdout', new_callable=io.StringIO) as mock_stdout:
            result = main(args)
            self.assertEqual(result, 0)
            self.assertIn('Updated Makefile for version 2025.02',
                          mock_stdout.getvalue())

    def test_main_makefile_no_changes(self):
        """Test main function when Makefile is already up-to-date"""
        makefile_path = os.path.join(self.test_dir, 'Makefile')
        with open(makefile_path, 'w', encoding='utf-8') as f:
            f.write('VERSION = 2025\nPATCHLEVEL = 02\nSUBLEVEL =\n'
                    'EXTRAVERSION =\n')

        parser = create_parser()
        args = parser.parse_args(['--date', '2025-02-03',
                                  '--update-makefile', '--makefile',
                                  makefile_path])

        with patch('sys.stdout', new_callable=io.StringIO) as mock_stdout:
            result = main(args)
            self.assertEqual(result, 0)
            expected_msg = (
                'Makefile is already up-to-date for version 2025.02')
            self.assertIn(expected_msg, mock_stdout.getvalue())

    def test_main_release_docs_update_success(self):
        """Test main function successfully updating release docs"""
        docs_path = os.path.join(self.test_dir, 'concept_releases.rst')
        content = ('.. SPDX-License-Identifier: GPL-2.0+\n\n'
                  'Deinde Concept Releases\n=======================\n\n'
                  'Release History\n---------------\n\n')
        with open(docs_path, 'w', encoding='utf-8') as f:
            f.write(content)

        parser = create_parser()
        args = parser.parse_args(['--date', '2025-02-03',
                                  '--update-release-docs',
                                  '--release-docs', docs_path,
                                  '--commit-sha', 'abc123'])

        with patch('sys.stdout', new_callable=io.StringIO) as mock_stdout:
            result = main(args)
            self.assertEqual(result, 0)
            expected_msg = 'Updated documentation for version 2025.02'
            self.assertIn(expected_msg, mock_stdout.getvalue())


class TestRSTFormatting(unittest.TestCase):
    """Test that generated RST content is valid for Sphinx"""

    def setUp(self):
        """Set up test fixtures"""
        self.test_dir = tempfile.mkdtemp()

    def tearDown(self):
        """Clean up test fixtures"""
        shutil.rmtree(self.test_dir)

    def test_generate_schedule_rst_validity(self):
        """Test that generate_schedule() produces valid RST"""
        schedule = generate_schedule()

        # Write schedule to a test file
        test_rst_path = os.path.join(self.test_dir, 'test_schedule.rst')
        with open(test_rst_path, 'w', encoding='utf-8') as f:
            f.write('''.. SPDX-License-Identifier: GPL-2.0+

Test Document
=============

''')
            f.write(schedule)

        # Try to validate with rst2html if available (fallback test)
        try:
            # Run docutils rst2html to validate the RST syntax
            result = subprocess.run(['rst2html', test_rst_path],
                                  capture_output=True, text=True, timeout=30)
            # If rst2html is available and succeeds, the RST is valid
            if result.returncode == 0:
                self.assertTrue(True, "RST validation passed with rst2html")
            else:
                # Check for specific formatting errors
                stderr = result.stderr.lower()
                if 'bullet list ends without a blank line' in stderr:
                    self.fail(f"RST formatting error in schedule: {result.stderr}")
                elif 'unexpected unindent' in stderr:
                    self.fail(f"RST formatting error in schedule: {result.stderr}")
        except (subprocess.TimeoutExpired, FileNotFoundError):
            # rst2html not available or timeout, do basic content checks
            pass

        # Basic RST format validation checks
        lines = schedule.split('\n')

        # Check for proper bullet list formatting
        in_bullet_list = False
        last_line_was_bullet = False

        for i, line in enumerate(lines):
            stripped = line.strip()

            # Check if this line starts a bullet list item
            if stripped.startswith('* '):
                in_bullet_list = True
                last_line_was_bullet = True

                # Check for line continuation issues
                if '\\' in line:
                    # If there's a backslash continuation, ensure it's properly formatted
                    # The continuation should be on the same line, not the next line
                    self.assertFalse(line.rstrip().endswith('\\'),
                                   f"Line {i+1} has improper line continuation: {line}")
            elif in_bullet_list and stripped == '':
                # Empty line might end the bullet list
                last_line_was_bullet = False
            elif in_bullet_list and stripped and not stripped.startswith('  '):
                # Non-indented content after bullet list should have blank line before it
                if last_line_was_bullet:
                    self.fail(f"Line {i+1} lacks blank line after bullet list: {line}")
                in_bullet_list = False
                last_line_was_bullet = False
            elif in_bullet_list and stripped.startswith('  '):
                # Indented content is part of the bullet list
                last_line_was_bullet = False
            else:
                in_bullet_list = False
                last_line_was_bullet = False

    def test_update_docs_rst_validity(self):
        """Test that update_docs() produces valid RST"""
        docs_path = os.path.join(self.test_dir, 'concept_releases.rst')

        # Create initial file
        with open(docs_path, 'w', encoding='utf-8') as f:
            f.write('''.. SPDX-License-Identifier: GPL-2.0+

Deinde Concept Releases
=======================

This document tracks all concept releases of U-Boot.

Release History
---------------

''')

        # Add a release that includes schedule generation
        release_info = ReleaseInfo(
            is_final=False,
            version='2025.02-rc1',
            year=2025,
            month=2,
            rc_number=1
        )

        changes_made = update_docs(release_info, 'abc123def', docs_path)
        self.assertTrue(changes_made)

        # Read the updated content
        with open(docs_path, 'r', encoding='utf-8') as f:
            content = f.read()

        # Try to validate with rst2html if available
        try:
            result = subprocess.run(['rst2html', docs_path],
                                  capture_output=True, text=True, timeout=30)
            if result.returncode != 0:
                stderr = result.stderr.lower()
                if 'bullet list ends without a blank line' in stderr:
                    self.fail(f"RST formatting error in generated docs: {result.stderr}")
                elif 'unexpected unindent' in stderr:
                    self.fail(f"RST formatting error in generated docs: {result.stderr}")
        except (subprocess.TimeoutExpired, FileNotFoundError):
            # rst2html not available, continue with manual checks
            pass

        # Manual validation of RST structure
        lines = content.split('\n')

        # Find the Next Release section and check its formatting
        next_release_idx = -1
        for i, line in enumerate(lines):
            if line.strip() == 'Next Release':
                next_release_idx = i
                break

        if next_release_idx != -1:
            # Check the bullet list in the schedule section
            in_candidate_schedule = False
            for i in range(next_release_idx, len(lines)):
                line = lines[i]
                if 'Release candidate schedule:' in line:
                    in_candidate_schedule = True
                    continue
                elif in_candidate_schedule and line.strip().startswith('* '):
                    # This is a bullet list item
                    # Check it doesn't have improper line continuation
                    self.assertFalse(line.rstrip().endswith('\\'),
                                   f"Line {i+1} has improper line continuation in schedule: {line}")
                elif in_candidate_schedule and line.strip() == '':
                    # Empty line - bullet list might be ending
                    continue
                elif (in_candidate_schedule and line.strip() and
                      not line.strip().startswith('* ') and
                      not line.startswith('  ')):
                    # End of bullet list section
                    break


class TestReleaseVersionScenarios(unittest.TestCase):
    """Test realistic release scenarios"""

    def test_february_2025_cycle(self):
        """Test the Feb 2025 release cycle"""
        # Mon 23-Dec-24 - should be rc1 for Feb 2025
        # (6 weeks before)
        info = calculate_info(datetime.date(2024, 12, 23))
        self.assertEqual(info.version, '2025.02-rc1')

        # Mon 06-Jan-25 - should be rc2 for Feb 2025
        info = calculate_info(datetime.date(2025, 1, 6))
        self.assertEqual(info.version, '2025.02-rc2')

        # Mon 20-Jan-25 - should be rc3 for Feb 2025
        info = calculate_info(datetime.date(2025, 1, 20))
        self.assertEqual(info.version, '2025.02-rc3')

        # Mon 03-Feb-25 - should be final 2025.02
        info = calculate_info(datetime.date(2025, 2, 3))
        self.assertEqual(info.version, '2025.02')
        self.assertTrue(info.is_final)

        # Tue 04-Feb-25 - should be dead period
        # (9 weeks until Apr 2025)
        info = calculate_info(datetime.date(2025, 2, 4))
        self.assertTrue(info.is_dead_period)
        self.assertEqual(info.year, 2025)
        self.assertEqual(info.month, 4)

    def test_april_2025_cycle(self):
        """Test the Apr 2025 release cycle"""
        # 24-Feb-25 - should be rc1 for Apr 2025 (6 weeks before)
        info = calculate_info(datetime.date(2025, 2, 24))
        self.assertEqual(info.version, '2025.04-rc1')

        # 10-Mar-25 - should be rc2 for Apr 2025
        info = calculate_info(datetime.date(2025, 3, 10))
        self.assertEqual(info.version, '2025.04-rc2')

        # 24-Mar-25 - should be rc3 for Apr 2025
        info = calculate_info(datetime.date(2025, 3, 24))
        self.assertEqual(info.version, '2025.04-rc3')

        # Mon 07-Apr-25 - should be final 2025.04
        info = calculate_info(datetime.date(2025, 4, 7))
        self.assertEqual(info.version, '2025.04')
        self.assertTrue(info.is_final)

    def test_too_early_for_rc_cycle(self):
        """Test dates too early for RC cycle (dead period)"""
        # 04-Nov-24 (Monday, odd month) targets Dec 2024 release,
        # ~4 weeks away -> rc2 (still valid)
        info = calculate_info(datetime.date(2024, 11, 4))
        self.assertFalse(info.is_final)
        self.assertFalse(info.is_dead_period)
        self.assertEqual(info.version, '2024.12-rc2')
        self.assertEqual(info.rc_number, 2)
        self.assertGreater(info.weeks_until_final, 2)  # More than 2 weeks
                                                        # (would be rc1)

        # 01-Oct-24 (even month) targets Dec 2024 (2 months away),
        # ~9 weeks -> dead period
        info = calculate_info(datetime.date(2024, 10, 1))
        self.assertFalse(info.is_final)
        self.assertTrue(info.is_dead_period)
        self.assertEqual(info.version, '')
        self.assertEqual(info.year, 2024)
        self.assertEqual(info.month, 12)
        self.assertGreater(info.weeks_until_final, 6)  # Way more than 6 weeks

        # 26-Aug-24 (Monday, even month) targets Oct 2024,
        # 6 weeks away -> rc1 (still valid)
        info = calculate_info(datetime.date(2024, 8, 26))
        self.assertFalse(info.is_final)
        self.assertFalse(info.is_dead_period)
        self.assertEqual(info.version, '2024.10-rc1')
        self.assertEqual(info.rc_number, 1)
        self.assertEqual(info.weeks_until_final, 6)

        # Test extreme case: 01-Aug-24 (even) targets Oct 2024,
        # ~9+ weeks -> dead period
        info = calculate_info(datetime.date(2024, 8, 1))
        self.assertFalse(info.is_final)
        self.assertTrue(info.is_dead_period)
        self.assertEqual(info.version, '')
        self.assertEqual(info.year, 2024)
        self.assertEqual(info.month, 10)
        self.assertGreater(info.weeks_until_final, 8)  # Way beyond 6 weeks

    def test_monday_only_releases(self):
        """Test that releases only happen on Mondays"""
        # Tuesday 04-Feb-25 (day after final release Monday)
        info = calculate_info(datetime.date(2025, 2, 4))
        self.assertTrue(info.is_dead_period)
        self.assertEqual(info.version, '')

        # Wednesday 05-Feb-25
        info = calculate_info(datetime.date(2025, 2, 5))
        self.assertTrue(info.is_dead_period)
        self.assertEqual(info.version, '')

        # Sunday 09-Feb-25 (day before potential RC Monday)
        info = calculate_info(datetime.date(2025, 2, 9))
        self.assertTrue(info.is_dead_period)
        self.assertEqual(info.version, '')

        # Monday 10-Feb-25 should be valid for next release cycle
        info = calculate_info(datetime.date(2025, 2, 10))
        # This should be either a valid RC or dead period, but not empty due to day
        if not info.is_dead_period:
            self.assertNotEqual(info.version, '')

    def test_monday_rc_releases(self):
        """Test that RC releases happen on correct Mondays"""
        # 20-Jan-25 is Monday, should be rc3 for Feb 2025
        info = calculate_info(datetime.date(2025, 1, 20))
        self.assertFalse(info.is_final)
        self.assertEqual(info.version, '2025.02-rc3')
        self.assertFalse(info.is_dead_period)

        # 21-Jan-25 is Tuesday, should be dead period
        info = calculate_info(datetime.date(2025, 1, 21))
        self.assertTrue(info.is_dead_period)
        self.assertEqual(info.version, '')

    def test_every_second_monday_only(self):
        """Test that releases only happen every second Monday"""
        # 13-Jan-25 is Monday between rc2 (6-Jan) and rc3 (20-Jan) - should be dead period
        info = calculate_info(datetime.date(2025, 1, 13))
        self.assertTrue(info.is_dead_period)
        self.assertEqual(info.version, '')
        self.assertEqual(info.weeks_until_final, 3)

        # 27-Jan-25 is Monday between rc3 (20-Jan) and final (3-Feb) - should be dead period
        info = calculate_info(datetime.date(2025, 1, 27))
        self.assertTrue(info.is_dead_period)
        self.assertEqual(info.version, '')
        self.assertEqual(info.weeks_until_final, 1)


if __name__ == '__main__':
    unittest.main()
