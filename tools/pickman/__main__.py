#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
#
# Copyright 2025 Canonical Ltd.
# Written by Simon Glass <simon.glass@canonical.com>
#
"""Entry point for pickman - parses arguments and dispatches to control."""

import argparse
import os
import sys
import unittest

# Allow 'from pickman import xxx' to work via symlink
our_path = os.path.dirname(os.path.realpath(__file__))
sys.path.insert(0, os.path.join(our_path, '..'))

# pylint: disable=wrong-import-position,import-error
from pickman import control
from pickman import ftest
from u_boot_pylib import test_util


def add_main_commands(subparsers):
    """Add main pickman commands to the argument parser

    Args:
        subparsers (ArgumentParser): ArgumentParser subparsers object
    """
    add_source = subparsers.add_parser('add-source',
                                        help='Add a source branch to track')
    add_source.add_argument('source', help='Source branch name')

    apply_cmd = subparsers.add_parser('apply',
                                       help='Apply next commits using Claude')
    apply_cmd.add_argument('source', help='Source branch name')
    apply_cmd.add_argument('-b', '--branch', help='Branch name to create')
    apply_cmd.add_argument('-p', '--push', action='store_true',
                           help='Push branch and create GitLab MR')
    apply_cmd.add_argument('-r', '--remote', default='ci',
                           help='Git remote for push (default: ci)')
    apply_cmd.add_argument('-t', '--target', default='master',
                           help='Target branch for MR (default: master)')

    check_cmd = subparsers.add_parser(
        'check',
        help='Check current branch for cherry-picks with large deltas')
    check_cmd.add_argument('-t', '--threshold', type=float, default=0.2,
                           help='Delta threshold as fraction (default: 0.2 = '
                                '20%%)')
    check_cmd.add_argument('-m', '--min-lines', type=int, default=10,
                           help='Minimum lines changed to check delta '
                                '(default: 10)')
    check_cmd.add_argument('-v', '--verbose', action='store_true',
                           help='Show detailed stats for all commits')
    check_cmd.add_argument('-d', '--diff', action='store_true',
                           help='Show source code diff for problem commits')

    check_gl = subparsers.add_parser('check-gitlab',
                                      help='Check GitLab permissions')
    check_gl.add_argument('-r', '--remote', default='ci',
                          help='Git remote (default: ci)')

    commit_src = subparsers.add_parser('commit-source',
                                        help='Update database with last commit')
    commit_src.add_argument('source', help='Source branch name')
    commit_src.add_argument('commit', help='Commit hash to record')

    subparsers.add_parser('compare', help='Compare branches')

    count_merges = subparsers.add_parser(
        'count-merges', help='Count remaining merges to process')
    count_merges.add_argument('source', help='Source branch name')

    drift_cmd = subparsers.add_parser(
        'drift', help='Report deltas from upstream which are not wanted')
    drift_cmd.add_argument('source', help='Source branch name')
    drift_cmd.add_argument('-b', '--branch', default='ci/master',
                           help='Downstream branch (default: ci/master)')
    drift_cmd.add_argument('-D', '--deep', action='store_true',
                           help='Also look inside files which downstream '
                                'commits touch (slower)')
    drift_cmd.add_argument('-d', '--diff', action='store_true',
                           help='Show the drift as a patch')
    drift_cmd.add_argument('-l', '--list', action='store_true',
                           help='List each file which has drift')

    drift_acc = subparsers.add_parser(
        'drift-accept', help='Record a delta from upstream as intentional')
    drift_acc.add_argument('path', help='File path, or glob to cover several')
    drift_acc.add_argument('-m', '--message', required=True, dest='message',
                           help='Reason the delta is wanted')
    drift_acc.add_argument('-u', '--hunk', default='*',
                           help="Hunk fingerprint (default: '*', the whole "
                                'file)')

    drift_fix = subparsers.add_parser(
        'drift-fix', help='Revert drift back to upstream and create MRs')
    drift_fix.add_argument('source', help='Source branch name')
    drift_fix.add_argument('-b', '--branch', default='ci/master',
                           help='Downstream branch (default: ci/master)')
    drift_fix.add_argument('-c', '--count', type=int, default=1,
                           help='Number of areas to fix at once (default: 1)')
    drift_fix.add_argument('-D', '--deep', action='store_true',
                           help='Also look inside files which downstream '
                                'commits touch (slower)')
    drift_fix.add_argument('-p', '--push', action='store_true',
                           help='Push branch and create GitLab MR')
    drift_fix.add_argument('-r', '--remote', default='ci',
                           help='Git remote for push (default: ci)')
    drift_fix.add_argument('-t', '--target', default='master',
                           help='Target branch for MR (default: master)')

    subparsers.add_parser('list-sources', help='List tracked source branches')

    next_merges = subparsers.add_parser(
        'next-merges', help='Show next N merges to be applied')
    next_merges.add_argument('source', help='Source branch name')
    next_merges.add_argument('-c', '--count', type=int, default=10,
                             help='Number of merges to show (default: 10)')

    next_set = subparsers.add_parser(
        'next-set', help='Show next set of commits to cherry-pick')
    next_set.add_argument('source', help='Source branch name')

    pick_cmd = subparsers.add_parser('pick',
                                      help='Cherry-pick commits ad-hoc')
    pick_cmd.add_argument('commits', help='Commit range (a..b) or merge commit')
    pick_cmd.add_argument('-b', '--branch', help='Branch name to create')
    pick_cmd.add_argument('-p', '--push', action='store_true',
                          help='Push branch and create GitLab MR')
    pick_cmd.add_argument('-r', '--remote', default='ci',
                          help='Git remote for push (default: ci)')
    pick_cmd.add_argument('-t', '--target', default='master',
                          help='Target branch for MR (default: master)')

    review_cmd = subparsers.add_parser(
        'review', help='Check open MRs and handle comments')
    review_cmd.add_argument('-r', '--remote', default='ci',
                            help='Git remote (default: ci)')

    rewind_cmd = subparsers.add_parser(
        'rewind', help='Rewind source position back by N merges')
    rewind_cmd.add_argument('source', help='Source branch name')
    rewind_cmd.add_argument('-c', '--count', type=int, default=1,
                            help='Number of merges to rewind (default: 1)')
    rewind_cmd.add_argument('-f', '--force', action='store_true',
                            help='Actually execute (default is dry run)')
    rewind_cmd.add_argument('-r', '--remote', default='ci',
                            help='Git remote for MR lookup (default: ci)')

    switch_src = subparsers.add_parser(
        'switch-source',
        help='Copy a source position to a new branch (e.g. us/next -> '
             'us/master)')
    switch_src.add_argument('old_source', help='Existing source branch name')
    switch_src.add_argument('new_source', help='New source branch name')
    switch_src.add_argument('--at',
                            help='Override start commit (default: inherit '
                                 'from old source)')
    switch_src.add_argument('-f', '--force', action='store_true',
                            help='Overwrite an existing new-source entry')

    step_cmd = subparsers.add_parser('step',
                                     help='Create MR if none pending')
    step_cmd.add_argument('source', help='Source branch name')
    step_cmd.add_argument('-F', '--fix-retries', type=int, default=3,
                          help='Max pipeline-fix attempts per MR '
                               '(0 to disable, default: 3)')
    step_cmd.add_argument('-m', '--max-mrs', type=int, default=5,
                          help='Max open MRs allowed (default: 5)')
    step_cmd.add_argument('-r', '--remote', default='ci',
                          help='Git remote (default: ci)')
    step_cmd.add_argument('-t', '--target', default='master',
                          help='Target branch for MR (default: master)')

    poll_cmd = subparsers.add_parser('poll',
                                     help='Run step repeatedly until stopped')
    poll_cmd.add_argument('source', help='Source branch name')
    poll_cmd.add_argument('-F', '--fix-retries', type=int, default=3,
                          help='Max pipeline-fix attempts per MR '
                               '(0 to disable, default: 3)')
    poll_cmd.add_argument('-i', '--interval', type=int, default=300,
                          help='Interval between steps in seconds '
                               '(default: 300)')
    poll_cmd.add_argument('-m', '--max-mrs', type=int, default=5,
                          help='Max open MRs allowed (default: 5)')
    poll_cmd.add_argument('-r', '--remote', default='ci',
                          help='Git remote (default: ci)')
    poll_cmd.add_argument('-t', '--target', default='master',
                          help='Target branch for MR (default: master)')

    push_cmd = subparsers.add_parser('push-branch',
                                     help='Push branch using GitLab API token')
    push_cmd.add_argument('branch', help='Branch name to push')
    push_cmd.add_argument('-r', '--remote', default='ci',
                          help='Git remote (default: ci)')
    push_cmd.add_argument('-f', '--force', action='store_true',
                          help='Force push (overwrite remote branch)')
    push_cmd.add_argument('--run-ci', action='store_true',
                          help='Run CI pipeline (default: skip for new MRs)')


def add_test_commands(subparsers):
    """Add test-related commands to the argument parser

    Args:
        subparsers (ArgumentParser): ArgumentParser subparsers object
    """
    test_cmd = subparsers.add_parser('test', help='Run tests')
    test_cmd.add_argument('-P', '--processes', type=int,
                          help='Number of processes to run tests '
                               '(default: all)')
    test_cmd.add_argument('-T', '--test-coverage', action='store_true',
                          help='Run tests and check for 100%% coverage')
    test_cmd.add_argument('-v', '--verbosity', type=int, default=1,
                          help='Verbosity level (0-4, default: 1)')
    test_cmd.add_argument('tests', nargs='*', help='Specific tests to run')


def parse_args(argv):
    """Parse command line arguments.

    Args:
        argv (list): Command line arguments

    Returns:
        Namespace: Parsed arguments
    """
    parser = argparse.ArgumentParser(description='Check commit differences')
    parser.add_argument('--no-colour', action='store_true',
                        help='Disable colour output')
    subparsers = parser.add_subparsers(dest='cmd', required=True)

    add_main_commands(subparsers)
    add_test_commands(subparsers)

    return parser.parse_args(argv)


def get_test_classes():
    """Get all test classes from the ftest module.

    Returns:
        list: List of test class objects
    """
    return [getattr(ftest, name) for name in dir(ftest)
            if name.startswith('Test') and
            isinstance(getattr(ftest, name), type) and
            issubclass(getattr(ftest, name), unittest.TestCase)]


def run_tests(processes, verbosity, test_name):
    """Run the pickman test suite.

    Args:
        processes (int): Number of processes for concurrent tests
        verbosity (int): Verbosity level (0-4)
        test_name (str): Specific test to run, or None for all

    Returns:
        int: 0 if tests passed, 1 otherwise
    """
    result = test_util.run_test_suites(
        'pickman', False, verbosity, False, False, processes,
        test_name, None, get_test_classes())

    return 0 if result.wasSuccessful() else 1


def run_test_coverage(args):
    """Run tests with coverage checking.

    Args:
        args (list): Specific tests to run, or None for all
    """
    # agent.py and gitlab_api.py require external services (Claude, GitLab)
    # so they can't achieve 100% coverage in unit tests; control.py is also
    # excluded as its remote-trigger paths cannot be exercised here
    test_util.run_test_coverage(
        'tools/pickman/pickman', None,
        ['*test*', '*__main__.py', 'tools/u_boot_pylib/*',
         'tools/pickman/agent.py', 'tools/pickman/gitlab_api.py',
         'tools/pickman/control.py'],
        None, extra_args=None, args=args)


def main(argv=None):
    """Main function to parse args and run commands.

    Args:
        argv (list): Command line arguments (None for sys.argv[1:])
    """
    args = parse_args(argv)

    if args.cmd == 'test':
        if args.test_coverage:
            run_test_coverage(args.tests or None)
            return 0
        test_name = args.tests[0] if args.tests else None
        return run_tests(args.processes, args.verbosity, test_name)

    return control.do_pickman(args)


if __name__ == '__main__':
    sys.exit(main())
