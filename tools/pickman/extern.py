# SPDX-License-Identifier: GPL-2.0+
#
# Copyright 2026 Canonical Ltd.
# Written by Simon Glass <simon.glass@canonical.com>
#
"""Port upstream changes to projects which have moved out of the tree

Some tools, such as binman and buildman, are now maintained as standalone
projects rather than in the U-Boot tree. Upstream commits which change them
cannot be cherry-picked here, since the files are no longer present. Instead,
pickman applies the part of each commit under the tool's old path to the
project's own repository and opens a pull request there.

Projects are listed in the pickman config file, one section each, e.g.::

    [external:binman]
    path = tools/binman
    repo = ~/dev/binman
    dest = binman

where 'path' is the tool's directory in the U-Boot tree, 'repo' is a local
clone of the project and 'dest' is where those files live in the project.
Optional 'remote' and 'branch' keys give the remote to push to (default
'origin') and the branch to base the work on and target with the pull request
(default 'master').
"""

from collections import namedtuple
import configparser
import os
import shutil
import tempfile

from u_boot_pylib import command
from u_boot_pylib import tools
from u_boot_pylib import tout

from pickman import agent
from pickman import gitlab_api

# Prefix of config sections which describe an external project
SECTION_PREFIX = 'external:'

# An external project
#
# name (str): Name of the project, e.g. 'binman'
# path (str): Directory of the tool in the U-Boot tree, e.g. 'tools/binman'
# repo (str): Path to a local clone of the project
# dest (str): Directory of those files in the project, e.g. 'binman', or ''
#     for the top level
# remote (str): Git remote to fetch from and push to
# branch (str): Branch to base the work on and target with the pull request
Project = namedtuple('Project', 'name path repo dest remote branch')

# The result of porting commits to external projects
#
# ok (bool): True if all the projects were handled successfully
# urls (list of str): URLs of the pull requests created
# paths (list of str): Paths of the external projects which the commits
#     change, whose changes must be dropped when cherry-picking
# tree_needed (bool): True if any commit changes files outside the external
#     projects, so still needs to be cherry-picked into the tree
PortResult = namedtuple('PortResult', 'ok urls paths tree_needed')


def get_projects(config_file=None):
    """Read the external projects from the pickman config file

    Args:
        config_file (str): Path to the config file, or None to use the
            standard one

    Returns:
        list of Project: Projects, sorted by name

    Raises:
        ValueError: A project is missing its 'path' or 'repo' setting
    """
    fname = config_file or gitlab_api.CONFIG_FILE
    if not os.path.exists(fname):
        return []
    config = configparser.ConfigParser()
    config.read(fname)

    projects = []
    for section in config.sections():
        if not section.startswith(SECTION_PREFIX):
            continue
        name = section[len(SECTION_PREFIX):]
        sect = config[section]
        if 'path' not in sect or 'repo' not in sect:
            raise ValueError(f"Config section '{section}' needs 'path' and "
                             "'repo' settings")
        projects.append(Project(
            name, sect['path'].strip('/'), os.path.expanduser(sect['repo']),
            sect.get('dest', name).strip('/'), sect.get('remote', 'origin'),
            sect.get('branch', 'master')))
    return sorted(projects)


def is_under(fname, path):
    """Check whether a file is within a directory

    Args:
        fname (str): Filename relative to the top of the tree
        path (str): Directory relative to the top of the tree

    Returns:
        bool: True if fname is path or is inside it
    """
    return fname == path or fname.startswith(path + '/')


def changed_files(chash):
    """Get the files changed by a commit

    Merge commits are reported as changing nothing, since their changes come
    from the commits they merge.

    Args:
        chash (str): Commit hash

    Returns:
        list of str: Filenames relative to the top of the tree
    """
    out = command.output('git', 'diff-tree', '--no-commit-id', '--name-only',
                         '-r', '--root', chash)
    return out.splitlines()


def split_commits(commits, projects):
    """Work out which commits change which external projects

    Args:
        commits (list): Commits to check, each with 'hash', 'chash' and
            'subject' attributes
        projects (list of Project): External projects

    Returns:
        tuple:
            dict: Commits for each project which changes it, keyed by project
                name, in the original order
            bool: True if any commit changes files outside the external
                projects
    """
    by_project = {}
    tree_needed = False
    for commit in commits:
        files = changed_files(commit.hash)
        touched = set()
        outside = False
        for fname in files:
            owner = next((proj.name for proj in projects
                          if is_under(fname, proj.path)), None)
            if owner:
                touched.add(owner)
            else:
                outside = True
        for proj in projects:
            if proj.name in touched:
                by_project.setdefault(proj.name, []).append(commit)
        if outside:
            tree_needed = True
    return by_project, tree_needed


def _git(repo, *args, raise_on_error=True):
    """Run a git command in a repository

    Args:
        repo (str): Path to the repository (or worktree)
        args (list of str): Arguments to git
        raise_on_error (bool): True to raise an exception on failure

    Returns:
        CommandResult: Result of the command
    """
    return command.run_one('git', '-C', repo, *args, capture=True,
                           capture_stderr=True, raise_on_error=raise_on_error)


def am_in_progress(worktree):
    """Check whether a 'git am' is still in progress

    Args:
        worktree (str): Path to the worktree

    Returns:
        bool: True if 'git am' has stopped partway through
    """
    path = _git(worktree, 'rev-parse', '--git-path', 'rebase-apply').stdout
    return os.path.exists(os.path.join(worktree, path.strip()))


def apply_commit(proj, commit, worktree, tmpdir):
    """Apply the part of a commit under the project's path to the worktree

    This uses 'git am -3', calling on the agent to resolve any conflicts

    Args:
        proj (Project): Project to apply to
        commit (CommitInfo): Commit to apply
        worktree (str): Worktree of the project, on the branch to apply to
        tmpdir (str): Directory to hold the patch file

    Returns:
        bool: True if the commit was applied (or skipped by the agent as
            already present), False on failure
    """
    # Generate the patch with the project's paths, so that it applies as it
    # is and makes sense to anyone (or any agent) looking at it
    args = ['format-patch', '--stdout', '-1', f'--relative={proj.path}']
    if proj.dest:
        args += [f'--src-prefix=a/{proj.dest}/', f'--dst-prefix=b/{proj.dest}/']
    patch = command.output('git', *args, commit.hash)
    fname = os.path.join(tmpdir, f'{commit.chash}.patch')
    tools.write_file(fname, patch, binary=False)

    result = _git(worktree, 'am', '-3', fname, raise_on_error=False)
    if not result.return_code:
        return True

    tout.info(f'{proj.name}: {commit.chash} does not apply cleanly, invoking '
              'agent...')
    agent.resolve_external_conflict(proj, commit, fname, worktree)
    if am_in_progress(worktree):
        tout.error(f'{proj.name}: Could not apply {commit.chash} '
                   f'{commit.subject}')
        _git(worktree, 'am', '--abort', raise_on_error=False)
        return False
    return True


def create_pr(proj, worktree, branch_name, title, body):
    """Create a pull request for a branch, or find the existing one

    Args:
        proj (Project): Project to create the pull request in
        worktree (str): Worktree of the project
        branch_name (str): Branch to create the pull request from
        title (str): Title of the pull request
        body (str): Description of the pull request

    Returns:
        str: URL of the pull request, or None on failure
    """
    result = command.run_one(
        'gh', 'pr', 'create', '--base', proj.branch, '--head', branch_name,
        '--title', title, '--body', body, cwd=worktree, capture=True,
        capture_stderr=True, raise_on_error=False)
    if not result.return_code:
        return result.stdout.strip().splitlines()[-1]

    # The pull request may exist already, from an earlier attempt
    result = command.run_one(
        'gh', 'pr', 'view', branch_name, '--json', 'url', '--jq', '.url',
        cwd=worktree, capture=True, capture_stderr=True, raise_on_error=False)
    if not result.return_code and result.stdout.strip():
        return result.stdout.strip()
    tout.error(f'{proj.name}: Could not create a pull request for '
               f'{branch_name}')
    return None


def format_body(proj, source, commits):
    """Format the description of a pull request

    Args:
        proj (Project): Project the pull request is for
        source (str): Source branch the commits come from
        commits (list): Commits ported

    Returns:
        str: Description
    """
    commit_list = '\n'.join(f'- {c.chash} {c.subject}' for c in commits)
    return (f'Changes to `{proj.path}/` from U-Boot ({source}), ported by '
            f'pickman.\n\nCommits:\n{commit_list}\n')


def port_project(proj, commits, branch_name, source, push):  # pylint: disable=too-many-arguments
    """Port commits to an external project and open a pull request

    The work is done in a temporary worktree of the project, so the clone's
    own checkout is not touched. The branch is left in the clone afterwards.

    Args:
        proj (Project): Project to port to
        commits (list): Commits which change the project
        branch_name (str): Branch to create in the project
        source (str): Source branch the commits come from
        push (bool): True to push the branch and open a pull request

    Returns:
        tuple:
            bool: True on success
            str: URL of the pull request, or None if not pushed
    """
    if not os.path.isdir(proj.repo):
        tout.error(f'{proj.name}: No repository at {proj.repo}')
        return False, None
    tout.info(f'{proj.name}: Porting {len(commits)} commit(s) to '
              f'{proj.repo}')
    _git(proj.repo, 'fetch', proj.remote)

    tmpdir = tempfile.mkdtemp(prefix='pickman-ext.')
    worktree = os.path.join(tmpdir, 'wt')
    try:
        _git(proj.repo, 'worktree', 'add', '-B', branch_name, worktree,
             f'{proj.remote}/{proj.branch}')
        for commit in commits:
            if not apply_commit(proj, commit, worktree, tmpdir):
                return False, None

        url = None
        if push:
            _git(worktree, 'push', '--force', proj.remote,
                 f'{branch_name}:{branch_name}')
            url = create_pr(proj, worktree, branch_name,
                            f'[pickman] {commits[-1].subject}',
                            format_body(proj, source, commits))
            if not url:
                return False, None
            tout.info(f'{proj.name}: {url}')
        else:
            tout.info(f'{proj.name}: Branch {branch_name} is ready in '
                      f'{proj.repo}')
        return True, url
    finally:
        _git(proj.repo, 'worktree', 'remove', '--force', worktree,
             raise_on_error=False)
        shutil.rmtree(tmpdir, ignore_errors=True)


def port_commits(commits, branch_name, source, push, projects=None):
    """Port the external parts of a set of commits to their projects

    Args:
        commits (list): Commits being applied
        branch_name (str): Branch name to use in each project
        source (str): Source branch the commits come from
        push (bool): True to push the branches and open pull requests
        projects (list of Project): External projects, or None to read them
            from the config file

    Returns:
        PortResult: Result of porting
    """
    if projects is None:
        projects = get_projects()
    if not projects:
        return PortResult(True, [], [], True)

    by_project, tree_needed = split_commits(commits, projects)
    touched = [proj for proj in projects if proj.name in by_project]
    paths = [proj.path for proj in touched]
    urls = []
    for proj in touched:
        ok, url = port_project(proj, by_project[proj.name], branch_name,
                               source, push)
        if not ok:
            return PortResult(False, urls, paths, tree_needed)
        if url:
            urls.append(url)
    return PortResult(True, urls, paths, tree_needed)
