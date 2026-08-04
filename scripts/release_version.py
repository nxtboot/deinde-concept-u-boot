#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
"""
Release version calculation for U-Boot concept releases.

This script calculates the appropriate version number and type for U-Boot
releases based on the current date and release schedule.

Release Schedule:
- Final releases: First Monday of even-numbered months
  (Feb, Apr, Jun, Aug, Oct, Dec)
- Release candidates: Every second Monday before final release
  - rc1: 6 weeks before final release (first RC)
  - rc2: 4 weeks before final release
  - rc3: 2 weeks before final release (last RC)
"""

import argparse
import datetime
import json
import os
import sys
from typing import Tuple, NamedTuple


class ReleaseInfo(NamedTuple):
    """Information about a calculated release"""
    is_final: bool
    version: str
    year: int
    month: int
    rc_number: int = 0
    weeks_until_final: int = 0
    is_dead_period: bool = False


def get_first_monday(year: int, month: int) -> datetime.date:
    """Get the first Monday of a given month and year"""
    first_day = datetime.date(year, month, 1)
    # Monday is 0 in weekday(), so days_until_monday = (7 - weekday()) % 7
    days_until_monday = (7 - first_day.weekday()) % 7
    return first_day + datetime.timedelta(days=days_until_monday)


def get_next_even_month(curdate: datetime.date) -> Tuple[int, int]:
    """Get the year and month of the next even-numbered month"""
    year = curdate.year
    month = curdate.month

    if month % 2 == 0:
        # Current month is even, next release is 2 months away
        next_month = month + 2
        if next_month > 12:
            next_month -= 12
            year += 1
    else:
        # Current month is odd, next release is next month
        next_month = month + 1

    return year, next_month


def calculate_info(curdate: datetime.date = None) -> ReleaseInfo:
    """Calculate release information based on the current date

    Args:
        curdate: Date to calculate from (defaults to today)

    Returns:
        ReleaseInfo with version details
    """
    if curdate is None:
        curdate = datetime.date.today()

    year = curdate.year
    month = curdate.month
    day_of_week = curdate.weekday()  # Monday = 0
    day_of_month = curdate.day

    # Check if this is a final release day (first Monday of even month)
    if month % 2 == 0 and day_of_week == 0 and day_of_month <= 7:
        # This is a final release
        return ReleaseInfo(
            is_final=True,
            version=f'{year}.{month:02d}',
            year=year,
            month=month
        )

    # This is a release candidate - calculate based on next final release
    next_year, next_month = get_next_even_month(curdate)
    next_release_date = get_first_monday(next_year, next_month)

    # Calculate weeks until next release
    days_until_release = (next_release_date - curdate).days
    weeks_until_release = (days_until_release + 6) // 7  # Round up

    # Determine RC number based on weeks before release
    if weeks_until_release > 6:
        # Too far from release - dead period, no release
        return ReleaseInfo(
            is_final=False,
            version='',
            year=next_year,
            month=next_month,
            is_dead_period=True,
            weeks_until_final=weeks_until_release
        )

    # Check if today is a Monday (releases only happen on Mondays every 2 weeks)
    if day_of_week != 0:  # Not a Monday
        return ReleaseInfo(
            is_final=False,
            version='',
            year=next_year,
            month=next_month,
            is_dead_period=True,
            weeks_until_final=weeks_until_release
        )

    # Check if this Monday is a release Monday (every 2 weeks from final)
    # Final is on first Monday, so release Mondays are at 0, 2, 4, 6 weeks
    if weeks_until_release not in [0, 2, 4, 6]:
        return ReleaseInfo(
            is_final=False,
            version='',
            year=next_year,
            month=next_month,
            is_dead_period=True,
            weeks_until_final=weeks_until_release
        )

    # RC numbering: rc1 is furthest (6 weeks), rc2 is middle (4 weeks),
    # rc3 is closest (2 weeks), then final (0 weeks)
    if weeks_until_release == 6:
        rc_number = 1
    elif weeks_until_release == 4:
        rc_number = 2
    elif weeks_until_release == 2:
        rc_number = 3
    else:  # weeks_until_release == 0 - should not happen, finals caught earlier
        rc_number = 1  # Fallback

    version = f'{next_year}.{next_month:02d}-rc{rc_number}'

    return ReleaseInfo(
        is_final=False,
        version=version,
        year=next_year,
        month=next_month,
        rc_number=rc_number,
        weeks_until_final=weeks_until_release
    )


def generate_schedule():
    """Generate the next release schedule section"""
    curdate = datetime.date.today()
    info = calculate_info(curdate)

    if info.is_dead_period:
        # Find the next release
        target_year = info.year
        target_month = info.month
    else:
        if info.is_final:
            # Just had a final, next is 2 months away
            target_year, target_month = get_next_even_month(curdate)
        else:
            # In RC cycle, use the current target final release
            target_year = info.year
            target_month = info.month

    # Calculate the schedule for the target release
    final_date = get_first_monday(target_year, target_month)

    # Calculate RC dates
    rc3_date = final_date - datetime.timedelta(weeks=6)
    rc2_date = final_date - datetime.timedelta(weeks=4)
    rc1_date = final_date - datetime.timedelta(weeks=2)

    schedule_text = f'''Next Release
------------

The next final release is scheduled for **{target_year}.{target_month:02d}**
on {final_date.strftime('%A, %B %d, %Y')}.

Release candidate schedule:

* **{target_year}.{target_month:02d}-rc3**: {rc3_date.strftime('%a %d-%b-%Y')}
* **{target_year}.{target_month:02d}-rc2**: {rc2_date.strftime('%a %d-%b-%Y')}
* **{target_year}.{target_month:02d}-rc1**: {rc1_date.strftime('%a %d-%b-%Y')}
* **{target_year}.{target_month:02d}** (Final): {final_date.strftime('%a %d-%b-%Y')}

'''
    return schedule_text


def update_docs(info: ReleaseInfo, commit_sha: str = '',
                docs_path: str = 'doc/develop/concept_releases.rst',
                subject: str = '') -> bool:
    """
    Update the documentation file with new information.

    Args:
        info: Release information to document
        commit_sha: Git commit SHA for the release
        docs_path: Path to the documentation file
        subject: Commit subject/title for the release

    Returns:
        True if changes were made, False if already up-to-date
    """
    if info.is_dead_period:
        return False

    # Create directory if it doesn't exist
    docs_dir = os.path.dirname(docs_path)
    if docs_dir:
        os.makedirs(docs_dir, exist_ok=True)

    # Create initial file if it doesn't exist
    if not os.path.exists(docs_path):
        with open(docs_path, 'w', encoding='utf-8') as f:
            f.write('''.. SPDX-License-Identifier: GPL-2.0+

Deinde Concept Releases
=======================

This document tracks all concept releases of U-Boot, including both final
releases and release candidates.

Release History
---------------

''')

    # Read current content
    try:
        with open(docs_path, 'r', encoding='utf-8') as f:
            content = f.read()
    except FileNotFoundError:
        print(f'Error: Could not read {docs_path}', file=sys.stderr)
        return False

    # Generate release entry
    release_type = 'Final Release' if info.is_final else 'Release Candidate'
    date_str = datetime.date.today().strftime('%Y-%m-%d')


    new_entry = f'''**{info.version}** - {release_type}
   :Date: {date_str}
   :Commit: {commit_sha if commit_sha else 'N/A'}
   :Subject: {subject if subject else 'N/A'}

'''

    # Find Release History section to check for duplicates
    lines = content.split('\n')
    history_start = -1
    for i, line in enumerate(lines):
        if (line.strip() == 'Release History' and i + 1 < len(lines) and
              lines[i + 1].strip().startswith('---')):
            history_start = i + 3
            break
    # Check if this release is already documented in the Release History
    if history_start != -1:
        history_content = '\n'.join(lines[history_start:])
        if f'**{info.version}** - ' in history_content:
            return False  # Already documented

    # Update the 'Next Release' section and find release history
    lines = content.split('\n')
    next_release_pos = -1
    history_pos = -1

    # Find existing sections
    for i, line in enumerate(lines):
        if line.strip() == 'Next Release':
            next_release_pos = i
        elif (line.strip() == 'Release History' and i + 1 < len(lines) and
              lines[i + 1].strip().startswith('---')):
            history_pos = i + 3  # After header, separator, empty line
            break

    # Generate and update the Next Release section
    next_schedule = generate_schedule()
    next_schedule_lines = next_schedule.rstrip().split('\n')

    if next_release_pos != -1:
        # Find end of Next Release section
        section_end = len(lines)
        for j in range(next_release_pos + 1, len(lines)):
            if lines[j].strip() == 'Release History':
                section_end = j
                break
            if (j + 1 < len(lines) and lines[j + 1].strip() and
                  lines[j + 1].replace('-', '').strip() == ''):
                # Found next section header
                section_end = j + 1
                break

        # Replace the entire Next Release section
        lines[next_release_pos:section_end] = next_schedule_lines
        lines.insert(next_release_pos + len(next_schedule_lines), '')

        # Recalculate release history position
        history_pos = -1
        for i, line in enumerate(lines):
            if (line.strip() == 'Release History' and i + 1 < len(lines) and
                  lines[i + 1].strip().startswith('---')):
                history_pos = i + 3
                break
    else:
        # Insert Next Release section before Release History
        if history_pos != -1:
            insert_point = history_pos - 3
            for line in reversed(next_schedule_lines + ['']):
                lines.insert(insert_point, line)
            history_pos += len(next_schedule_lines) + 1

    # Add the release entry to Release History
    if history_pos == -1:
        # Fallback: append at the end
        lines.append('')
        lines.append(new_entry.rstrip())
    else:
        # Insert the new entry at the beginning of the release history
        entry_lines = new_entry.rstrip().split('\n')
        for entry_line in reversed(entry_lines):
            lines.insert(history_pos, entry_line)

    # Write the updated content
    with open(docs_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')

    return True


def update_makefile(info: ReleaseInfo, makefile_path: str = 'Makefile') -> bool:
    """
    Update the Makefile with new version information.

    Args:
        info: Release information to write
        makefile_path: Path to the Makefile

    Returns:
        True if changes were made, False if already up-to-date
    """
    try:
        with open(makefile_path, 'r', encoding='utf-8') as f:
            content = f.read()
    except FileNotFoundError:
        print(f'Error: {makefile_path} not found', file=sys.stderr)
        return False

    lines = content.splitlines()
    changes_made = False

    for i, line in enumerate(lines):
        if line.startswith('VERSION ='):
            new_line = f'VERSION = {info.year}'
            if lines[i] != new_line:
                lines[i] = new_line
                changes_made = True
        elif line.startswith('PATCHLEVEL ='):
            new_line = f'PATCHLEVEL = {info.month:02d}'
            if lines[i] != new_line:
                lines[i] = new_line
                changes_made = True
        elif line.startswith('SUBLEVEL ='):
            new_line = 'SUBLEVEL ='
            if lines[i] != new_line:
                lines[i] = new_line
                changes_made = True
        elif line.startswith('EXTRAVERSION ='):
            if info.is_final:
                new_line = 'EXTRAVERSION ='
            else:
                new_line = f'EXTRAVERSION = -rc{info.rc_number}'
            if lines[i] != new_line:
                lines[i] = new_line
                changes_made = True

    if changes_made:
        with open(makefile_path, 'w', encoding='utf-8') as f:
            f.write('\n'.join(lines) + '\n')

    return changes_made


def create_parser():
    """Create and return the argument parser"""
    parser = argparse.ArgumentParser(
        description='Calculate U-Boot release version')
    parser.add_argument('--date', help='Date to calculate from (YYYY-MM-DD)')
    parser.add_argument('--update-makefile', action='store_true',
                       help='Update Makefile with calculated version')
    parser.add_argument('--makefile', default='Makefile',
                       help='Path to Makefile (default: Makefile)')
    parser.add_argument('--update-release-docs', action='store_true',
                       help='Update release documentation with new release')
    parser.add_argument(
        '--release-docs',
        default='doc/develop/concept_releases.rst',
        help='Path to release docs (default: doc/develop/concept_releases.rst)')
    parser.add_argument('--commit-sha', help='Git commit SHA for the release')
    parser.add_argument('--subject', help='Commit subject for the release')
    parser.add_argument('--format', choices=['version', 'json', 'shell'],
                       default='shell', help='Output format')
    return parser


def main(args=None):
    """Main entry point for the script"""
    if args is None:
        parser = create_parser()
        args = parser.parse_args()

    # Parse date if provided
    curdate = None
    if args.date:
        try:
            curdate = datetime.datetime.strptime(
                args.date, '%Y-%m-%d').date()
        except ValueError:
            print(f'Error: Invalid date format \'{args.date}\'. Use YYYY-MM-DD',
                  file=sys.stderr)
            return 1

    # Calculate release info
    info = calculate_info(curdate)

    # Update Makefile if requested
    if args.update_makefile:
        if info.is_dead_period:
            print('No release during dead period - Makefile not updated')
            return 1  # Exit with error code to indicate no action taken
        changes_made = update_makefile(info, args.makefile)
        if not changes_made:
            print(f'Makefile is already up-to-date for version {info.version}')
            return 0
        print(f'Updated Makefile for version {info.version}')

    # Update documentation if requested
    if args.update_release_docs:
        if info.is_dead_period:
            print('No release during dead period - Documentation not updated')
            return 1  # Exit with error code to indicate no action taken
        changes_made = update_docs(
            info, args.commit_sha or '', args.release_docs, args.subject or '')
        if changes_made:
            print(f'Updated documentation for version {info.version}')
        else:
            print(f'Documentation already contains version {info.version}')
            return 0

    # Handle dead period - no release should be generated
    if info.is_dead_period:
        if args.format == 'version':
            print('NO_RELEASE')
        elif args.format == 'json':
            output = {
                'is_dead_period': True,
                'weeks_until_final': info.weeks_until_final,
                'next_release_year': info.year,
                'next_release_month': info.month
            }
            print(json.dumps(output))
        elif args.format == 'shell':
            print('IS_DEAD_PERIOD=true')
            print(f'WEEKS_UNTIL_FINAL={info.weeks_until_final}')
            print(f'NEXT_RELEASE_YEAR={info.year}')
            print(f'NEXT_RELEASE_MONTH={info.month:02d}')
        return 0

    # Output results
    if args.format == 'version':
        print(info.version)
    elif args.format == 'json':
        output = {
            'is_final': info.is_final,
            'version': info.version,
            'year': info.year,
            'month': info.month,
        }
        if not info.is_final:
            output['rc_number'] = info.rc_number
            output['weeks_until_final'] = info.weeks_until_final
        print(json.dumps(output))
    elif args.format == 'shell':
        print(f"IS_FINAL={'true' if info.is_final else 'false'}")
        print(f'VERSION={info.version}')
        print(f'YEAR={info.year}')
        print(f'MONTH={info.month:02d}')
        if not info.is_final:
            print(f'RC_NUMBER={info.rc_number}')
            print(f'WEEKS_UNTIL_FINAL={info.weeks_until_final}')

    return 0


if __name__ == '__main__':
    sys.exit(main())
