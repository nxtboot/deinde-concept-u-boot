# SPDX-License-Identifier: GPL-2.0+
#
# Copyright 2025 Canonical Ltd.
# Written by Simon Glass <simon.glass@canonical.com>
#
"""Agent module for pickman - uses Claude to automate cherry-picking."""

import asyncio
import os
import shutil
import sys
import tempfile

# Allow 'from pickman import xxx' to work via symlink
our_path = os.path.dirname(os.path.realpath(__file__))
sys.path.insert(0, os.path.join(our_path, '..'))

# pylint: disable=wrong-import-position,import-error
from u_boot_pylib import tools
from u_boot_pylib import tout

# Signal file for agent to communicate status back to pickman
SIGNAL_FILE = '.pickman-signal'

# Signal status codes
SIGNAL_SUCCESS = 'success'
SIGNAL_APPLIED = 'already_applied'
SIGNAL_CONFLICT = 'conflict'

# Import common Claude agent utilities from shared module
from u_boot_pylib.claude import (
    AGENT_AVAILABLE, MAX_BUFFER_SIZE, check_available, run_agent_collect,
)

ClaudeAgentOptions = None
if AGENT_AVAILABLE:
    from u_boot_pylib.claude import ClaudeAgentOptions  # pylint: disable=C0412

# Commits that need special handling (regenerate instead of cherry-pick)
# These run savedefconfig on all boards and depend on target branch
# Kconfig state
QCONFIG_SUBJECTS = [
    'configs: Resync with savedefconfig',
]


def is_qconfig_commit(subject):
    """Check if a commit subject indicates a qconfig resync commit

    Args:
        subject (str): Commit subject line

    Returns:
        bool: True if this is a qconfig resync commit
    """
    return any(subject.startswith(pat) for pat in QCONFIG_SUBJECTS)


async def run(commits, source, branch_name, repo_path=None):  # pylint: disable=too-many-locals
    """Run the Claude agent to cherry-pick commits

    Args:
        commits (list): list of AgentCommit namedtuples with fields:
                       hash, chash, subject, applied_as
        source (str): source branch name
        branch_name (str): name for the new branch to create
        repo_path (str): path to repository (defaults to current directory)

    Returns:
        bool: True on success, False on failure
    """
    if not check_available():
        return False

    if repo_path is None:
        repo_path = os.getcwd()

    # Remove any stale signal file from previous runs
    signal_path = os.path.join(repo_path, SIGNAL_FILE)
    if os.path.exists(signal_path):
        os.remove(signal_path)

    # Build commit list for the prompt, marking potentially applied commits
    # and qconfig resync commits
    commit_entries = []
    has_applied = False
    has_qconfig = False
    for commit in commits:
        entry = f'  - {commit.chash}: {commit.subject}'
        if commit.applied_as:
            entry += f' (maybe already applied as {commit.applied_as})'
            has_applied = True
        if is_qconfig_commit(commit.subject):
            entry += ' [QCONFIG RESYNC]'
            has_qconfig = True
        commit_entries.append(entry)

    commit_list = '\n'.join(commit_entries)

    # Add note about checking for already applied commits
    applied_note = ''
    if has_applied:
        applied_note = '''

IMPORTANT: Some commits may already be applied. Before cherry-picking commits
marked as "maybe already applied as <hash>", verify they are truly the same commit:
1. Compare the actual changes between the original and found commits:
   - Use: git show --no-ext-diff <original-hash> > /tmp/orig.patch
   - Use: git show --no-ext-diff <found-hash> > /tmp/found.patch
   - Compare: diff /tmp/orig.patch /tmp/found.patch
2. If the patches are similar with only minor differences (like line numbers,
   context, or conflict resolutions), SKIP the cherry-pick and report that
   it was already applied.
3. If the patches differ significantly in the actual changes being made,
   proceed with the cherry-pick as they are different commits.
'''

    # Add note about qconfig resync commits
    qconfig_note = ''
    if has_qconfig:
        qconfig_note = '''

IMPORTANT: Commits marked [QCONFIG RESYNC] need special handling. These commits
run savedefconfig on all boards and cannot be cherry-picked directly because the
defconfig state depends on the target branch's Kconfig options.

For [QCONFIG RESYNC] commits, instead of cherry-picking:
1. Skip the cherry-pick for this commit
2. Run: ./tools/qconfig.py -s -Cy
   This regenerates the savedefconfig changes based on the current Kconfig state
3. The qconfig.py tool will create a commit automatically with the updated defconfigs
4. Continue with the remaining commits after qconfig.py completes
'''

    # Get full hash of last commit for signal file
    last_commit_hash = commits[-1].hash

    prompt = f"""Cherry-pick the following commits from {source} branch:

{commit_list}{applied_note}{qconfig_note}

Steps to follow:
1. First run 'git status' to check the repository state is clean
2. Create and checkout a new branch based on ci/master:
   git checkout -b {branch_name} ci/master
3. Cherry-pick each commit in order:
   - For regular commits: git cherry-pick -x <hash>
   - For merge commits (identified by "Merge" in subject): git cherry-pick -x -m 1 --allow-empty <hash>
   Cherry-pick one commit at a time to handle each appropriately.
   IMPORTANT: Always include merge commits even if they result in empty commits.
   The merge commit message is important for tracking history.
4. AFTER EACH SUCCESSFUL CHERRY-PICK:
   a) Check commit delta: 'git show --stat <cherry-picked-hash>' and 'git show --stat <original-hash>'
      Compare the changed files and line counts. If they differ significantly (>20% lines changed
      or different files modified), this indicates a problem with the cherry-pick.
   b) Build test: Run 'buildman -L --board sandbox -w -o /tmp/pickman' to verify the build passes
   c) If delta is too large:
      - Reset the commit: 'git reset --hard HEAD~1'
      - Try to manually apply just the changes from the original commit:
        'git show <original-hash> | git apply --3way'
      - If that succeeds, create a new commit with the original message
      - If fails, try to apply the patch manually.
      - If manual apply fails, create an empty commit to preserve the commit sequence:
        'git commit --allow-empty -m "<original-subject> [FAILED]"'
      - Continue to the next commit
   d) If build fails and you cannot resolve it, report the issue and abort
5. If there are conflicts:
   - Show the conflicting files
   - Try to resolve simple conflicts automatically
   - For complex conflicts, describe what needs manual resolution and abort
   - When fix-ups are needed, amend the commit to add a one-line note at the
     end of the commit message describing the changes made
6. After ALL cherry-picks complete, verify with
   'git log --oneline -n {len(commits) + 2}'
   Ensure all {len(commits)} commits are present.
7. Run final build: 'buildman -L --board sandbox -w -o /tmp/pickman' to verify
   everything still works together
8. Report the final status including:
   - Build result (ok or list of warnings/errors)
   - Any fix-ups that were made
   - Any commits with concerning deltas

The cherry-pick branch will be left ready for pushing. Do NOT merge it back to any other branch.

Important:
- Stop immediately if there's a conflict that cannot be auto-resolved
- Do not force push or modify history
- If cherry-pick fails, run 'git cherry-pick --abort'
- NEVER skip merge commits - always use --allow-empty to preserve them

CRITICAL - Detecting Already-Applied Commits:
If the FIRST cherry-pick fails with conflicts, BEFORE aborting, check if the commits
are already present in ci/master with different hashes. For each commit:
1. Search for the commit subject: git log --oneline ci/master --grep="<subject>" -1
2. If found, verify it's actually the same commit by comparing patches:
   - git show --no-ext-diff <original-hash> > /tmp/orig.patch
   - git show --no-ext-diff <found-hash> > /tmp/found.patch
   - diff /tmp/orig.patch /tmp/found.patch
3. Only consider it "already applied" if the patches are similar with minor
   differences (like line numbers, context, or conflict resolutions)
If ALL commits are verified as already applied (same content, different hashes),
this means the series was already applied via a different path. In this case:
1. Abort the cherry-pick: git cherry-pick --abort
2. Delete the branch: git branch -D {branch_name}
3. Write a signal file to indicate this status:
   echo "already_applied" > {SIGNAL_FILE}
   echo "{last_commit_hash}" >> {SIGNAL_FILE}
4. Report that all {len(commits)} commits are already present in ci/master
"""

    options = ClaudeAgentOptions(
        allowed_tools=['Bash', 'Read', 'Grep', 'Edit', 'Write'],
        cwd=repo_path,
        max_buffer_size=MAX_BUFFER_SIZE,
    )

    tout.info(f'Starting Claude agent to cherry-pick {len(commits)} commits...')
    tout.info('')

    return await run_agent_collect(prompt, options)


def read_signal_file(repo_path=None):
    """Read and remove the signal file if it exists

    Args:
        repo_path (str): path to repository (defaults to current directory)

    Returns:
        tuple: (status, last_commit) where status is the signal status code
            (e.g., 'already_applied') and last_commit is the commit hash,
            or (None, None) if no signal file exists
    """
    if repo_path is None:
        repo_path = os.getcwd()

    signal_path = os.path.join(repo_path, SIGNAL_FILE)
    if not os.path.exists(signal_path):
        return None, None

    try:
        data = tools.read_file(signal_path, binary=False)
        lines = data.strip().split('\n')
        status = lines[0] if lines else None
        last_commit = lines[1] if len(lines) > 1 else None

        # Remove the signal file after reading
        os.remove(signal_path)

        return status, last_commit
    except (IOError, OSError) as exc:
        tout.warning(f'Failed to read signal file: {exc}')
        return None, None


def cherry_pick_commits(commits, source, branch_name, repo_path=None):
    """Synchronous wrapper for running the cherry-pick agent

    Args:
        commits (list): list of AgentCommit namedtuples with fields:
                       hash, chash, subject, applied_as
        source (str): source branch name
        branch_name (str): name for the new branch to create
        repo_path (str): path to repository (defaults to current directory)

    Returns:
        tuple: (success, conversation_log) where success is bool and
            conversation_log is the agent's output text
    """
    return asyncio.run(run(commits, source, branch_name, repo_path))


def build_review_context(comments, mr_description, needs_rebase, remote,
                         target):
    """Build context sections for the review agent prompt

    Args:
        comments (list): List of comment dicts with 'author', 'body' keys
        mr_description (str): MR description with context from previous work
        needs_rebase (bool): Whether the MR needs rebasing
        remote (str): Git remote name
        target (str): Target branch for rebase operations

    Returns:
        tuple: (context_section, comment_section, rebase_section)
    """
    # Include MR description for context from previous work
    context_section = ''
    if mr_description:
        context_section = f'''
Context from MR description (includes previous work done on this MR):

{mr_description}

Use this context to understand what was done previously and respond appropriately.
'''

    # Format comments for the prompt (if any)
    comment_section = ''
    if comments:
        comment_text = '\n'.join(f'- [{c.author}]: {c.body}' for c in comments)
        comment_section = f'''
Review comments to address:

{comment_text}
'''

    # Build rebase instructions
    rebase_section = ''
    if needs_rebase:
        rebase_section = f'''
Rebase instructions:
- The MR is behind the target branch and needs rebasing
- Use: git rebase --keep-empty {remote}/{target}
- This preserves empty merge commits which are important for tracking
- AFTER EACH REBASED COMMIT: Check delta and build
  a) Compare commit delta before/after rebase using 'git show --stat'
  b) Run 'buildman -L --board sandbox -w -o /tmp/pickman' to verify build passes
  c) If delta is significantly different or build fails, report and abort
- If there are conflicts, try to resolve them automatically
- For complex conflicts that cannot be resolved, describe them and abort
'''

    return context_section, comment_section, rebase_section


# pylint: disable=too-many-arguments
def build_review_prompt(mr_iid, branch_name, task_desc, context_section,
                        comment_section, rebase_section, comments,
                        needs_rebase, remote, target):
    """Build the main prompt for the review agent

    Args:
        mr_iid (int): Merge request IID
        branch_name (str): Source branch name
        task_desc (str): Description of tasks to perform
        context_section (str): Context from MR description
        comment_section (str): Formatted review comments
        rebase_section (str): Rebase instructions
        comments (list): List of comments (to check if non-empty)
        needs_rebase (bool): Whether the MR needs rebasing
        remote (str): Git remote name
        target (str): Target branch for rebase operations

    Returns:
        str: The complete prompt for the agent
    """
    # Build step2 instruction based on whether rebase is needed
    if needs_rebase:
        step2 = f'Rebase onto {remote}/{target} first'
    else:
        step2 = 'Read and understand each comment'

    if needs_rebase and comments:
        step3 = 'After rebase, address any review comments'
    elif comments:
        step3 = 'For each actionable comment:'
    else:
        step3 = 'Verify the rebase completed successfully'

    comment_steps = ''
    if comments:
        comment_steps = """   - Make the requested changes to the code
   - Amend the relevant commit or create a fixup commit"""

    return f"""Task for merge request !{mr_iid} (branch: {branch_name}):
{task_desc}
{context_section}{comment_section}{rebase_section}
Steps to follow:
1. Checkout the branch: git checkout {branch_name}
2. {step2}
3. {step3}
{comment_steps}
4. Run 'buildman -L --board sandbox -w -o /tmp/pickman' to verify the build passes
5. Create a local branch with suffix '-v2' (or increment: -v3, -v4, etc.)
6. Force push to the ORIGINAL remote branch to update the MR:
   ./tools/pickman/pickman push-branch {branch_name} -r {remote} -f
   (GitLab automatically triggers an MR pipeline when the branch is updated)
7. Report what was done and what reply should be posted to the MR

Important:
- Keep changes minimal and focused
- If a comment is unclear or cannot be addressed, note this in your report
- Local branch: {branch_name}-v2 (or -v3, -v4 etc.)
- Remote push: always to '{branch_name}' to update the existing MR
- Do NOT update the MR title - it should remain as originally set
"""


# pylint: disable=too-many-arguments
def build_full_review_prompt(mr_iid, branch_name, comments, remote, target,
                             needs_rebase, has_conflicts, mr_description):
    """Build complete prompt and task description for the review agent

    Args:
        mr_iid (int): Merge request IID
        branch_name (str): Source branch name
        comments (list): List of comment dicts with 'author', 'body' keys
        remote (str): Git remote name
        target (str): Target branch for rebase operations
        needs_rebase (bool): Whether the MR needs rebasing
        has_conflicts (bool): Whether the MR has merge conflicts
        mr_description (str): MR description with context from previous work

    Returns:
        tuple: (prompt, task_desc) where prompt is the full agent prompt and
            task_desc is a short description of the tasks
    """
    # Build the task description
    tasks = []
    if needs_rebase:
        if has_conflicts:
            tasks.append('rebase and resolve merge conflicts')
        else:
            tasks.append('rebase onto latest target branch')
    if comments:
        tasks.append(f'address {len(comments)} review comment(s)')
    task_desc = ' and '.join(tasks)

    # Build context sections
    context_section, comment_section, rebase_section = build_review_context(
        comments, mr_description, needs_rebase, remote, target)

    # Build the prompt
    prompt = build_review_prompt(
        mr_iid, branch_name, task_desc, context_section, comment_section,
        rebase_section, comments, needs_rebase, remote, target)

    return prompt, task_desc


# pylint: disable=too-many-arguments,too-many-locals
async def run_review_agent(mr_iid, branch_name, comments, remote,
                           target='master', needs_rebase=False,
                           has_conflicts=False, mr_description='',
                           repo_path=None):
    """Run the Claude agent to handle MR comments and/or rebase

    Args:
        mr_iid (int): Merge request IID
        branch_name (str): Source branch name
        comments (list): List of comment dicts with 'author', 'body' keys
        remote (str): Git remote name
        target (str): Target branch for rebase operations
        needs_rebase (bool): Whether the MR needs rebasing
        has_conflicts (bool): Whether the MR has merge conflicts
        mr_description (str): MR description with context from previous work
        repo_path (str): Path to repository (defaults to current directory)

    Returns:
        bool: True on success, False on failure
    """
    if not check_available():
        return False

    if repo_path is None:
        repo_path = os.getcwd()

    prompt, task_desc = build_full_review_prompt(
        mr_iid, branch_name, comments, remote, target, needs_rebase,
        has_conflicts, mr_description)

    options = ClaudeAgentOptions(
        allowed_tools=['Bash', 'Read', 'Grep', 'Edit', 'Write'],
        cwd=repo_path,
        max_buffer_size=MAX_BUFFER_SIZE,
    )

    tout.info(f'Starting Claude agent to {task_desc}...')
    tout.info('')

    return await run_agent_collect(prompt, options)


# pylint: disable=too-many-arguments
def handle_mr_comments(mr_iid, branch_name, comments, remote, target='master',
                       needs_rebase=False, has_conflicts=False,
                       mr_description='', repo_path=None):
    """Synchronous wrapper for running the review agent

    Args:
        mr_iid (int): Merge request IID
        branch_name (str): Source branch name
        comments (list): List of comment dicts
        remote (str): Git remote name
        target (str): Target branch for rebase operations
        needs_rebase (bool): Whether the MR needs rebasing
        has_conflicts (bool): Whether the MR has merge conflicts
        mr_description (str): MR description with context from previous work
        repo_path (str): Path to repository (defaults to current directory)

    Returns:
        tuple: (success, conversation_log) where success is bool and
            conversation_log is the agent's output text
    """
    return asyncio.run(run_review_agent(mr_iid, branch_name, comments, remote,
                                        target, needs_rebase, has_conflicts,
                                        mr_description, repo_path))


# pylint: disable=too-many-arguments
def build_pipeline_fix_prompt(mr_iid, branch_name, failed_jobs, remote,
                               target, mr_description, attempt,
                               tempdir=None):
    """Build prompt and task description for the pipeline fix agent

    The CI job log tails and MR description are written to temporary files
    instead of being embedded in the prompt, to avoid exceeding the Linux
    MAX_ARG_STRLEN limit (128 KB) when the Claude Agent SDK passes the
    prompt as a command-line argument.

    Args:
        mr_iid (int): Merge request IID
        branch_name (str): Source branch name
        failed_jobs (list): List of FailedJob tuples
        remote (str): Git remote name
        target (str): Target branch
        mr_description (str): MR description with context
        attempt (int): Fix attempt number
        tempdir (str): Directory for temporary log files; when None
            the log tails are embedded in the prompt (legacy behaviour)

    Returns:
        tuple: (prompt, task_desc) where prompt is the full agent prompt and
            task_desc is a short description
    """
    task_desc = f'fix {len(failed_jobs)} failed pipeline job(s) (attempt {attempt})'

    # Format failed jobs - write log tails to temp files when tempdir is
    # provided, to keep the prompt small enough for execve()
    job_sections = []
    for job in failed_jobs:
        if tempdir and job.log_tail:
            safe_name = job.name.replace('/', '_').replace(':', '_')
            log_path = os.path.join(tempdir,
                                    f'job-{job.id}-{safe_name}.log')
            with open(log_path, 'w', encoding='utf-8') as fout:
                fout.write(job.log_tail)
            job_sections.append(
                f'### Job: {job.name} (stage: {job.stage})\n'
                f'URL: {job.web_url}\n'
                f'Log tail: read file {log_path}'
            )
        else:
            job_sections.append(
                f'### Job: {job.name} (stage: {job.stage})\n'
                f'URL: {job.web_url}\n'
                f'Log tail:\n```\n{job.log_tail}\n```'
            )
    jobs_text = '\n\n'.join(job_sections)

    # Include MR description for context - write to file when tempdir
    # is provided and the description is non-trivial
    context_section = ''
    if mr_description:
        if tempdir and len(mr_description) > 1024:
            desc_path = os.path.join(tempdir, 'mr-description.txt')
            with open(desc_path, 'w', encoding='utf-8') as fout:
                fout.write(mr_description)
            context_section = f'''
Context from MR description: read file {desc_path}
'''
        else:
            context_section = f'''
Context from MR description:

{mr_description}
'''

    # Extract board names from failed job names for targeted builds.
    # CI job names typically contain a board name (e.g. 'build:sandbox',
    # 'test:venice_gw7905', 'world build <board>').  Collect unique names
    # to pass to buildman so the agent can verify all affected boards.
    board_names = set()
    for job in failed_jobs:
        # Try common CI patterns: 'build:<board>', 'test:<board>',
        # or a board name token in the job name
        for part in job.name.replace(':', ' ').replace('/', ' ').split():
            # Skip generic tokens that are not board names
            if part.lower() in ('build', 'test', 'world', 'check', 'lint',
                                'ci', 'job', 'stage'):
                continue
            board_names.add(part)

    # Always include sandbox for a basic sanity check
    board_names.add('sandbox')
    boards_csv = ','.join(sorted(board_names))

    prompt = f"""Fix pipeline failures for merge request !{mr_iid} \
(branch: {branch_name}, attempt {attempt}).
{context_section}
Failed jobs:

{jobs_text}

Steps to follow:
1. Checkout the branch: git checkout {branch_name}
2. Diagnose the root cause from the job logs above
3. Identify which commit introduced the problem:
   - Use 'git log --oneline' to list the commits on the branch
   - Correlate the failing file/symbol with the commit that touched it
   - Use 'git log --oneline -- <file>' if needed
4. Apply the fix to the appropriate commit:
   - If you can identify the responsible commit, use uman's rebase helpers
     to amend it:
     a) 'rf N' to start an interactive rebase going back N commits from
        HEAD, stopping at the oldest (first) commit in the range
     b) Make your fix, then amend the commit with a 1-2 line note appended
        to the end of the commit message describing the fix, e.g.:
        git add <files>
        git commit --amend -m "$(git log -1 --format=%B)

        [pickman] Fix <short description of what was fixed>"
     c) 'rn' to advance to the next commit (or 'git rebase --continue'
        to finish)
   - If the cause spans multiple commits or cannot be pinpointed, add a new
     fixup commit on top of the branch
5. Build and verify:
   a) Quick sandbox check: um build sandbox
   b) Build all affected boards: \
buildman -o /tmp/pickman {boards_csv}
   Fix any build errors before proceeding.
6. Create a local branch: {branch_name}-fix{attempt}
7. Report what was fixed: which commit was responsible, what the root cause
   was, and what change was made.  Do NOT push the branch; the caller
   handles that.

Important:
- Keep changes minimal and focused on fixing the failures
- Prefer amending the responsible commit over adding a new commit, so the
  MR history stays clean
- If the failure is an infrastructure or transient issue (network timeout, \
runner problem, etc.), report this without making changes
- Do not modify unrelated code
- Use 'um build sandbox' for sandbox builds (fast, local)
- Use 'buildman -o /tmp/pickman <board1> <board2> ...' to build multiple
  boards in one go
- Leave the result on local branch {branch_name}-fix{attempt}
"""

    return prompt, task_desc


async def run_pipeline_fix_agent(mr_iid, branch_name, failed_jobs, remote,
                                  target='master', mr_description='',
                                  attempt=1, repo_path=None):
    """Run the Claude agent to fix pipeline failures

    Job log tails are written to temporary files so that the prompt
    passed to the agent stays well below the Linux MAX_ARG_STRLEN
    limit (128 KB).

    Args:
        mr_iid (int): Merge request IID
        branch_name (str): Source branch name
        failed_jobs (list): List of FailedJob tuples
        remote (str): Git remote name
        target (str): Target branch
        mr_description (str): MR description with context
        attempt (int): Fix attempt number
        repo_path (str): Path to repository (defaults to current directory)

    Returns:
        tuple: (success, conversation_log) where success is bool and
            conversation_log is the agent's output text
    """
    if not check_available():
        return False, ''

    if repo_path is None:
        repo_path = os.getcwd()

    tempdir = tempfile.mkdtemp(prefix='pickman-logs-')
    try:
        prompt, task_desc = build_pipeline_fix_prompt(
            mr_iid, branch_name, failed_jobs, remote, target,
            mr_description, attempt, tempdir=tempdir)

        options = ClaudeAgentOptions(
            allowed_tools=['Bash', 'Read', 'Grep', 'Edit', 'Write'],
            cwd=repo_path,
            max_buffer_size=MAX_BUFFER_SIZE,
        )

        tout.info(f'Starting Claude agent to {task_desc}...')
        tout.info('')

        return await run_agent_collect(prompt, options)
    finally:
        shutil.rmtree(tempdir, ignore_errors=True)


def fix_pipeline(mr_iid, branch_name, failed_jobs, remote, target='master',
                 mr_description='', attempt=1, repo_path=None):
    """Synchronous wrapper for running the pipeline fix agent

    Args:
        mr_iid (int): Merge request IID
        branch_name (str): Source branch name
        failed_jobs (list): List of FailedJob tuples
        remote (str): Git remote name
        target (str): Target branch
        mr_description (str): MR description with context
        attempt (int): Fix attempt number
        repo_path (str): Path to repository (defaults to current directory)

    Returns:
        tuple: (success, conversation_log) where success is bool and
            conversation_log is the agent's output text
    """
    return asyncio.run(run_pipeline_fix_agent(
        mr_iid, branch_name, failed_jobs, remote, target,
        mr_description, attempt, repo_path))


def build_subtree_conflict_prompt(name, tag, subtree_path):
    """Build a prompt for resolving subtree merge conflicts

    Args:
        name (str): Subtree name ('dts', 'mbedtls', 'lwip')
        tag (str): Tag being pulled (e.g. 'v6.15-dts')
        subtree_path (str): Path to the subtree (e.g. 'dts/upstream')

    Returns:
        str: The prompt for the agent
    """
    return f"""Resolve merge conflicts from a subtree pull.

Context: A 'git subtree pull --prefix {subtree_path}' for the '{name}' \
subtree (tag {tag}) has produced merge conflicts. Git is currently in a \
merge state with MERGE_HEAD set.

Resolution strategy - prefer the upstream version:
1. Run 'git status' to see conflicted files
2. For each conflicted file:
   - If the file exists in MERGE_HEAD (content conflict or add/add):
     git checkout MERGE_HEAD -- <file>
   - If the file is a modify/delete conflict (deleted upstream):
     git rm <file>
3. After resolving all conflicts, stage everything:
   git add -A
4. Complete the merge:
   git commit --no-edit
5. Verify with 'git status' that the working tree is clean

If you cannot resolve the conflicts, abort with:
   git merge --abort

Important:
- Always prefer the upstream (MERGE_HEAD) version of conflicted files
- The subtree path is '{subtree_path}' - most conflicts will be there
- Do NOT modify files outside the subtree path
- Do NOT use 'git merge --continue'; use 'git commit --no-edit'
"""


async def run_subtree_conflict_agent(name, tag, subtree_path,
                                     repo_path=None):
    """Run the Claude agent to resolve subtree merge conflicts

    Args:
        name (str): Subtree name ('dts', 'mbedtls', 'lwip')
        tag (str): Tag being pulled (e.g. 'v6.15-dts')
        subtree_path (str): Path to the subtree (e.g. 'dts/upstream')
        repo_path (str): Path to repository (defaults to current directory)

    Returns:
        tuple: (success, conversation_log) where success is bool and
            conversation_log is the agent's output text
    """
    if not check_available():
        return False, ''

    if repo_path is None:
        repo_path = os.getcwd()

    prompt = build_subtree_conflict_prompt(name, tag, subtree_path)

    options = ClaudeAgentOptions(
        allowed_tools=['Bash', 'Read', 'Grep', 'Edit', 'Write'],
        cwd=repo_path,
        max_buffer_size=MAX_BUFFER_SIZE,
    )

    tout.info(f'Starting Claude agent to resolve {name} subtree '
              f'conflicts...')
    tout.info('')

    return await run_agent_collect(prompt, options)


def resolve_subtree_conflicts(name, tag, subtree_path, repo_path=None):
    """Synchronous wrapper for running the subtree conflict agent

    Args:
        name (str): Subtree name ('dts', 'mbedtls', 'lwip')
        tag (str): Tag being pulled (e.g. 'v6.15-dts')
        subtree_path (str): Path to the subtree (e.g. 'dts/upstream')
        repo_path (str): Path to repository (defaults to current directory)

    Returns:
        tuple: (success, conversation_log) where success is bool and
            conversation_log is the agent's output text
    """
    return asyncio.run(
        run_subtree_conflict_agent(name, tag, subtree_path, repo_path))
