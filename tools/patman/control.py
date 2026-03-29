# SPDX-License-Identifier: GPL-2.0+
#
# Copyright 2020 Google LLC
#
"""Handles the main control logic of patman

This module provides various functions called by the main program to implement
the features of patman.
"""

import re
import traceback

try:
    from importlib import resources
except ImportError:
    # for Python 3.6
    import importlib_resources as resources

from u_boot_pylib import gitutil
from u_boot_pylib import terminal
from u_boot_pylib import tools
from u_boot_pylib import tout
from patman import patchstream
from patman.patchwork import Patchwork
from patman import send
from patman import settings


def setup():
    """Do required setup before doing anything"""
    gitutil.setup()
    alias_fname = gitutil.get_alias_file()
    if alias_fname:
        settings.ReadGitAliases(alias_fname)


def do_send(args):
    """Create, check and send patches by email

    Args:
        args (argparse.Namespace): Arguments to patman
    """
    setup()
    send.send(args)


def patchwork_status(branch, count, start, end, dest_branch, force,
                     show_comments, url, single_thread=False):
    """Check the status of patches in patchwork

    This finds the series in patchwork using the Series-link tag, checks for new
    comments and review tags, displays then and creates a new branch with the
    review tags.

    Args:
        branch (str): Branch to create patches from (None = current)
        count (int): Number of patches to produce, or -1 to produce patches for
            the current branch back to the upstream commit
        start (int): Start partch to use (0=first / top of branch)
        end (int): End patch to use (0=last one in series, 1=one before that,
            etc.)
        dest_branch (str): Name of new branch to create with the updated tags
            (None to not create a branch)
        force (bool): With dest_branch, force overwriting an existing branch
        show_comments (bool): True to display snippets from the comments
            provided by reviewers
        url (str): URL of patchwork server, e.g. 'https://patchwork.ozlabs.org'.
            This is ignored if the series provides a Series-patchwork-url tag.

    Raises:
        ValueError: if the branch has no Series-link value
    """
    if not branch:
        branch = gitutil.get_branch()
    if count == -1:
        # Work out how many patches to send if we can
        count = gitutil.count_commits_to_branch(branch) - start

    series = patchstream.get_metadata(branch, start, count - end)
    warnings = 0
    for cmt in series.commits:
        if cmt.warn:
            print('%d warnings for %s:' % (len(cmt.warn), cmt.hash))
            for warn in cmt.warn:
                print('\t', warn)
                warnings += 1
            print
    if warnings:
        raise ValueError('Please fix warnings before running status')
    links = series.get('links')
    if not links:
        raise ValueError("Branch has no Series-links value")

    _, version = patchstream.split_name_version(branch)
    link = series.get_link_for_version(version, links)
    if not link:
        raise ValueError(f'Series-links has no link for v{version}')
    tout.debug(f"Link '{link}")

    # Allow the series to override the URL
    if 'patchwork_url' in series:
        url = series.patchwork_url
    pwork = Patchwork(url, single_thread=single_thread)

    # Import this here to avoid failing on other commands if the dependencies
    # are not present
    from patman import status
    pwork = Patchwork(url)
    status.check_and_show_status(series, link, branch, dest_branch, force,
                                 show_comments, False, pwork)


def _setup_patchwork(cser, pwork, ups, pw_url):
    """Set up a Patchwork instance from upstream and project settings

    Args:
        cser (Cseries): Open cseries instance
        pwork (Patchwork or None): Existing instance, or None to create
        ups (str or None): Upstream name
        pw_url (str or None): Patchwork URL override

    Returns:
        Patchwork: Configured instance

    Raises:
        ValueError: if the URL or project cannot be resolved
    """
    if pwork:
        return pwork
    tout.debug(f'_setup_patchwork: ups={ups!r} pw_url={pw_url!r}')
    if ups:
        ups_url = cser.db.upstream_get_patchwork_url(ups)
        if ups_url:
            if pw_url and pw_url != ups_url:
                tout.info(f'  Overriding {pw_url!r} with upstream'
                          f' {ups!r} URL {ups_url!r}')
            pw_url = ups_url
        tout.debug(f'  URL from upstream {ups!r}: {pw_url!r}')
    if not pw_url:
        raise ValueError(
            'No patchwork URL found; use -U/--upstream or '
            "configure with 'patman upstream add'")
    pwork = Patchwork(pw_url)
    proj = cser.project_get(ups)
    tout.debug(f'  project_get({ups!r}): {proj!r}')
    if not proj:
        proj = cser.project_get()
        tout.debug(f'  project_get(None) fallback: {proj!r}')
        if proj:
            tout.warning(f"No patchwork project for upstream '{ups}';"
                         f' using default project {proj[0]} (ID {proj[1]})')
    if not proj:
        raise ValueError(
            "Patchwork project not configured; use "
            "'patman patchwork set-project'")
    _, proj_id, link_name = proj
    pwork.project_set(proj_id, link_name)
    return pwork


def do_series(args, test_db=None, pwork=None, cser=None):
    """Process a series subcommand

    Args:
        args (Namespace): Arguments to process
        test_db (str or None): Directory containing the test database, None to
            use the normal one
        pwork (Patchwork): Patchwork object to use, None to create one if
            needed
        cser (Cseries): Cseries object to use, None to create one
    """
    from patman import cseries

    if not cser:
        cser = cseries.Cseries(test_db)
    needs_patchwork = [
        'autolink', 'autolink-all', 'open', 'send', 'status', 'gather',
        'gather-all'
        ]
    try:
        cser.open_database()
        if args.subcmd in needs_patchwork:
            ups = cser.get_series_upstream(args.series)
            tout.debug(f'Series upstream: {ups!r}')
            pwork = _setup_patchwork(
                cser, pwork, ups, args.patchwork_url)
        elif pwork and pwork is not True:
            raise ValueError(
                f"Internal error: command '{args.subcmd}' should not have patchwork")
        if args.subcmd == 'add':
            cser.add(args.series, args.desc, mark=args.mark,
                     allow_unmarked=args.allow_unmarked, end=args.upstream,
                     use_commit=args.use_first_commit,
                     ups=args.set_upstream, dry_run=args.dry_run)
        elif args.subcmd == 'archive':
            cser.archive(args.series)
        elif args.subcmd == 'autolink':
            cser.link_auto(pwork, args.series, args.version, args.update,
                           args.autolink_wait)
        elif args.subcmd == 'autolink-all':
            cser.link_auto_all(pwork, update_commit=args.update,
                               link_all_versions=args.link_all_versions,
                               replace_existing=args.replace_existing,
                               dry_run=args.dry_run, show_summary=True)
        elif args.subcmd == 'dec':
            cser.decrement(args.series, args.dry_run)
        elif args.subcmd == 'gather':
            cser.gather(pwork, args.series, args.version, args.show_comments,
                        args.show_cover_comments, args.gather_tags,
                        dry_run=args.dry_run)
        elif args.subcmd == 'gather-all':
            cser.gather_all(
                pwork, args.show_comments, args.show_cover_comments,
                args.gather_all_versions, args.gather_tags, args.dry_run)
        elif args.subcmd == 'get-link':
            link = cser.link_get(args.series, args.version)
            print(link)
        elif args.subcmd == 'inc':
            cser.increment(args.series, args.dry_run)
        elif args.subcmd == 'ls':
            cser.series_list(args.include_archived)
        elif args.subcmd == 'open':
            cser.open(pwork, args.series, args.version)
        elif args.subcmd == 'mark':
            cser.mark(args.series, args.allow_marked, dry_run=args.dry_run)
        elif args.subcmd == 'patches':
            cser.list_patches(args.series, args.version, args.commit,
                              args.patch)
        elif args.subcmd == 'progress':
            cser.progress(args.series, args.show_all_versions,
                          args.list_patches, args.include_archived)
        elif args.subcmd == 'rm':
            cser.remove(args.series, dry_run=args.dry_run)
        elif args.subcmd == 'rm-version':
            cser.version_remove(args.series, args.version, dry_run=args.dry_run)
        elif args.subcmd == 'rename':
            cser.rename(args.series, args.new_name, dry_run=args.dry_run)
        elif args.subcmd == 'set-upstream':
            cser.set_upstream(args.series, args.upstream_name,
                              dry_run=args.dry_run)
        elif args.subcmd == 'scan':
            cser.scan(args.series, mark=args.mark,
                      allow_unmarked=args.allow_unmarked, end=args.upstream,
                      dry_run=args.dry_run)
        elif args.subcmd == 'send':
            cser.send(pwork, args.series, args.autolink, args.autolink_wait,
                      args)
        elif args.subcmd == 'set-link':
            cser.link_set(args.series, args.version, args.link, args.update)
        elif args.subcmd == 'status':
            cser.status(pwork, args.series, args.version, args.show_comments,
                        args.show_cover_comments)
        elif args.subcmd == 'summary':
            cser.summary(args.series)
        elif args.subcmd == 'unarchive':
            cser.unarchive(args.series)
        elif args.subcmd == 'unmark':
            cser.unmark(args.series, args.allow_unmarked, dry_run=args.dry_run)
        elif args.subcmd == 'version-change':
            cser.version_change(args.series, args.version, args.new_version,
                                dry_run=args.dry_run)
        else:
            raise ValueError(f"Unknown series subcommand '{args.subcmd}'")
    finally:
        cser.close_database()


def upstream(args, test_db=None):
    """Process an 'upstream' subcommand

    Args:
        args (Namespace): Arguments to process
        test_db (str or None): Directory containing the test database, None to
            use the normal one
    """
    from patman import cseries

    cser = cseries.Cseries(test_db)
    try:
        cser.open_database()
        if args.subcmd == 'add':
            cser.upstream_add(args.remote_name, args.url,
                              args.project_name,
                              patchwork_url=args.patchwork_url,
                              identity=args.identity,
                              series_to=args.series_to,
                              no_maintainers=args.no_maintainers,
                              no_tags=args.no_tags)
        elif args.subcmd == 'default':
            if args.unset:
                cser.upstream_set_default(None)
            elif args.remote_name:
                cser.upstream_set_default(args.remote_name)
            else:
                result = cser.upstream_get_default()
                print(result if result else 'unset')
        elif args.subcmd == 'delete':
            cser.upstream_delete(args.remote_name)
        elif args.subcmd == 'set':
            kwargs = {}
            if args.patchwork_url is not None:
                kwargs['patchwork_url'] = args.patchwork_url
            if args.identity is not None:
                kwargs['identity'] = args.identity
            if args.series_to is not None:
                kwargs['series_to'] = args.series_to
            if args.no_maintainers:
                kwargs['no_maintainers'] = True
            elif args.maintainers:
                kwargs['no_maintainers'] = False
            if args.no_tags:
                kwargs['no_tags'] = True
            elif args.tags:
                kwargs['no_tags'] = False
            if not kwargs:
                raise ValueError('No settings to update')
            cser.upstream_set(args.remote_name, **kwargs)
        elif args.subcmd == 'ls':
            cser.upstream_list()
        else:
            raise ValueError(f"Unknown upstream subcommand '{args.subcmd}'")
    finally:
        cser.close_database()


def patchwork(args, test_db=None, pwork=None):
    """Process a 'patchwork' subcommand
    Args:
        args (Namespace): Arguments to process
        test_db (str or None): Directory containing the test database, None to
            use the normal one
        pwork (Patchwork): Patchwork object to use
    """
    from patman import cseries

    cser = cseries.Cseries(test_db)
    try:
        cser.open_database()
        if args.subcmd == 'set-project':
            if not args.remote:
                raise ValueError('Please specify the remote name')
            if not pwork:
                pw_url = cser.db.upstream_get_patchwork_url(args.remote)
                if not pw_url:
                    pw_url = args.patchwork_url
                if not pw_url:
                    raise ValueError(
                        f"No patchwork URL for remote '{args.remote}'"
                        "; use 'patman upstream add' with -p"
                        ' or pass --patchwork-url')
                pwork = Patchwork(pw_url)
            cser.project_set(pwork, args.project_name,
                             ups=args.remote)
        elif args.subcmd == 'get-project':
            if not args.remote:
                raise ValueError('Please specify the remote name')
            ups = args.remote
            info = cser.project_get(ups)
            if not info:
                raise ValueError(
                    "Project has not been set; use "
                    "'patman patchwork set-project'")
            name, pwid, link_name = info
            msg = (f"Project '{name}' patchwork-ID {pwid} "
                   f"link-name '{link_name}'")
            if ups:
                msg += f" remote '{ups}'"
            print(msg)
        elif args.subcmd == 'rm':
            cser.db.patchwork_delete(args.remote)
            cser.commit()
            ups_str = f" for upstream '{args.remote}'" if args.remote else ''
            tout.info(f'Deleted patchwork project{ups_str}')
        elif args.subcmd == 'ls':
            cser.project_list()
        else:
            raise ValueError(f"Unknown patchwork subcommand '{args.subcmd}'")
    finally:
        cser.close_database()

def do_workflow(args, test_db=None):
    """Process a 'workflow' subcommand

    Args:
        args (Namespace): Arguments to process
        test_db (str or None): Directory containing the test database, None to
            use the normal one
    """
    from patman import cseries
    from patman import workflow

    cser = cseries.Cseries(test_db)
    try:
        cser.open_database()
        if args.subcmd == 'todo':
            if args.clear:
                workflow.todo_clear(cser, args.series)
            else:
                workflow.todo(cser, args.series, args.days)
        elif args.subcmd == 'todo-list':
            workflow.todo_list(cser, args.show_all)
        elif args.subcmd in ['list', 'wl', 'ls']:
            workflow.list_entries(cser, args.show_all)
        else:
            raise ValueError(f"Unknown workflow subcommand '{args.subcmd}'")
    finally:
        cser.close_database()


def do_patman(args, test_db=None, pwork=None, cser=None):
    """Process a patman command

    Args:
        args (Namespace): Arguments to process
        test_db (str or None): Directory containing the test database, None to
            use the normal one
        pwork (Patchwork): Patchwork object to use, or None to create one
        cser (Cseries): Cseries object to use when executing the command,
            or None to create one
    """
    if args.full_help:
        with resources.path('patman', 'README.rst') as readme:
            tools.print_full_help(str(readme))
        return 0
    if args.cmd == 'send':
        # Called from git with a patch filename as argument
        # Printout a list of additional CC recipients for this patch
        if args.cc_cmd:
            re_line = re.compile(r'(\S*) (.*)')
            with open(args.cc_cmd, 'r', encoding='utf-8') as inf:
                for line in inf.readlines():
                    match = re_line.match(line)
                    if match and match.group(1) == args.patchfiles[0]:
                        for cca in match.group(2).split('\0'):
                            cca = cca.strip()
                            if cca:
                                print(cca)
        else:
            # If we are not processing tags, no need to warning about bad ones
            if not args.process_tags:
                args.ignore_bad_tags = True
            do_send(args)
        return 0

    ret_code = 0
    try:
        # Check status of patches in patchwork
        if args.cmd == 'status':
            patchwork_status(args.branch, args.count, args.start, args.end,
                             args.dest_branch, args.force, args.show_comments,
                             args.patchwork_url)
        elif args.cmd == 'series':
            do_series(args, test_db, pwork, cser)
        elif args.cmd == 'upstream':
            upstream(args, test_db)
        elif args.cmd == 'patchwork':
            patchwork(args, test_db, pwork)
        elif args.cmd == 'workflow':
            do_workflow(args, test_db)
    except Exception as exc:
        terminal.tprint(f'patman: {type(exc).__name__}: {exc}',
                        colour=terminal.Color.RED)
        if args.debug:
            print()
            traceback.print_exc()
        ret_code = 1
    return ret_code
