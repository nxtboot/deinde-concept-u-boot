# SPDX-License-Identifier: GPL-2.0+
#
# Copyright 2025 Canonical Ltd.
# Written by Simon Glass <simon.glass@canonical.com>
#

"""AI-powered patch review using Claude.

Fetches patches from patchwork, applies them to a local branch using
a Claude agent, and (in later stages) reviews each patch and creates
Gmail draft replies.
"""

import asyncio
import os
import tempfile

import aiohttp

from patman.database import Pcommit
from u_boot_pylib import gitutil
from u_boot_pylib import tout


async def fetch_mbox(pwork_url, link):
    """Download the series mbox file from patchwork

    Args:
        pwork_url (str): Patchwork server URL, e.g.
            'https://patchwork.ozlabs.org'
        link (str): Patchwork series link/ID

    Returns:
        str: Path to the downloaded mbox file

    Raises:
        ValueError: if the download fails
    """
    url = f'{pwork_url}/series/{link}/mbox/'
    tout.info(f'Downloading mbox from {url}')

    mbox_path = os.path.join(tempfile.gettempdir(),
                             f'patman_review_{link}.mbox')
    async with aiohttp.ClientSession() as client:
        async with client.get(url) as response:
            if response.status != 200:
                raise ValueError(
                    f'Failed to download mbox from {url}: '
                    f'HTTP {response.status}')
            content = await response.read()
            if not content:
                raise ValueError(f'Empty mbox downloaded from {url}')

    with open(mbox_path, 'wb') as fh:
        fh.write(content)
    tout.info(f'Downloaded {len(content)} bytes to {mbox_path}')
    return mbox_path


def _build_apply_prompt(mbox_path, branch_name, upstream_branch):
    """Build the Claude agent prompt for applying patches

    The agent will create a branch and apply the mbox using git am,
    handling any conflicts that arise.

    Args:
        mbox_path (str): Path to the downloaded mbox file
        branch_name (str): Name for the new branch
        upstream_branch (str): Branch to start from (e.g. 'origin/master')

    Returns:
        str: The prompt text for the agent
    """
    return f"""Apply a patch series from a patchwork mbox file to a new \
git branch.

TASK:
1. Create a new branch called '{branch_name}' from '{upstream_branch}':
   git checkout -b {branch_name} {upstream_branch}

2. Apply the patches from the mbox file:
   git am {mbox_path}

3. If git am fails with conflicts:
   - Inspect the conflict with 'git diff' and 'git status'
   - Try to resolve the conflicts sensibly
   - Run 'git add' on resolved files and 'git am --continue'
   - If a patch simply cannot be applied (e.g. already applied or
     completely incompatible), use 'git am --skip' and note which
     patch was skipped

4. After all patches are applied (or skipped), run:
   git log --oneline {upstream_branch}..HEAD

5. Report the result:
   - How many patches were applied successfully
   - Which patches (if any) were skipped and why
   - The branch name: {branch_name}

IMPORTANT:
- Do NOT modify the patches before applying
- Do NOT use 'git am --abort' unless all attempts to resolve fail
- If you skip a patch, continue with the remaining patches
- The mbox file is at: {mbox_path}
"""


async def apply_series(pwork_url, link, branch_name, upstream_branch,
                       repo_path):
    """Download and apply a patch series to a new local branch

    Uses the Claude agent to handle the git am process, including
    conflict resolution.

    Args:
        pwork_url (str): Patchwork server URL
        link (str): Patchwork series link/ID
        branch_name (str): Name for the new branch
        upstream_branch (str): Branch to base from
        repo_path (str): Path to the git repository

    Returns:
        tuple: (success, branch_name) where success is bool and
            branch_name is the name of the created branch
    """
    # pylint: disable=C0415,E0611
    from u_boot_pylib.claude import (check_available, run_agent_collect,
                                     MAX_BUFFER_SIZE)

    if not check_available():
        return False, None

    from u_boot_pylib.claude import ClaudeAgentOptions

    # Download the mbox
    mbox_path = await fetch_mbox(pwork_url, link)

    # Build the prompt and run the agent
    prompt = _build_apply_prompt(mbox_path, branch_name, upstream_branch)
    options = ClaudeAgentOptions(
        allowed_tools=['Bash', 'Read', 'Grep'],
        cwd=repo_path,
        max_buffer_size=MAX_BUFFER_SIZE,
    )

    tout.info(f'Applying series to branch {branch_name}...')
    success, _ = await run_agent_collect(prompt, options)

    # Clean up the mbox file
    try:
        os.unlink(mbox_path)
    except OSError:
        pass

    return success, branch_name


def apply_series_sync(pwork_url, link, branch_name, upstream_branch,
                      repo_path):
    """Synchronous wrapper for apply_series()

    Args:
        pwork_url (str): Patchwork server URL
        link (str): Patchwork series link/ID
        branch_name (str): Name for the new branch
        upstream_branch (str): Branch to base from
        repo_path (str): Path to the git repository

    Returns:
        tuple: (success, branch_name)
    """
    return asyncio.run(
        apply_series(pwork_url, link, branch_name, upstream_branch,
                     repo_path))


def search_series(pwork, title):
    """Search patchwork for a series by cover-letter title

    Queries the patchwork API and returns the link for the best match.
    If multiple matches are found, shows them and picks the most recent.

    Args:
        pwork (Patchwork): Configured patchwork instance
        title (str): Title text to search for

    Returns:
        str: Patchwork series link/ID

    Raises:
        ValueError: if no matching series is found
    """
    async def _query():
        if not pwork.proj_id:
            raise ValueError(
                'Patchwork project not configured; use '
                "'patman patchwork set-project' or provide -l <link>")
        async with aiohttp.ClientSession() as client:
            # pylint: disable=W0212
            return await pwork._query_series(client, title)

    loop = asyncio.get_event_loop()
    results = loop.run_until_complete(_query())

    if not results:
        raise ValueError(f"No series found matching '{title}'")

    if len(results) == 1:
        match = results[0]
        tout.notice(f"Found: {match['name']} (v{match['version']}, "
                  f"link {match['id']})")
        return str(match['id'])

    tout.notice(f"Found {len(results)} matching series:")
    for i, match in enumerate(results):
        tout.notice(f"  {i + 1}. [{match['id']}] {match['name']} "
                  f"(v{match['version']}, {match['date']})")

    best = max(results,
               key=lambda r: (r.get('version', 0), r.get('date', '')))
    tout.notice(f"Using most recent: {best['name']} (link {best['id']})")
    return str(best['id'])


def _clean_series_name(name):
    """Strip the [U-Boot,v2,0/4] prefix from a series name

    Args:
        name (str): Raw series name from patchwork

    Returns:
        str: Cleaned name
    """
    if name.startswith('['):
        bracket_end = name.find(']')
        if bracket_end != -1:
            return name[bracket_end + 1:].strip()
    return name


# pylint: disable=R0914
def _register_series(cser, clean_name, version, link, series_data):
    """Register a series in the database for review tracking

    Creates or finds the series record, adds a ser_ver entry and
    pcommit records for each patch.

    Args:
        cser (Cseries): Open cseries instance
        clean_name (str): Cleaned series name
        version (int): Series version number
        link (str): Patchwork series link/ID
        series_data (dict): Series data from patchwork

    Returns:
        tuple: (series_id, svid) or None if already reviewed
    """
    existing = cser.db.series_find_by_link(link)
    if existing:
        series_id, db_name, db_version, svid = existing
        tout.notice(
            f"Already reviewed: '{db_name}' v{db_version}")
        return None

    prev = cser.db.series_find_review_by_name(clean_name)
    if prev:
        series_id, db_name, prev_version = prev
        tout.notice(f"Previously reviewed '{db_name}' "
                  f"v{prev_version}; adding v{version}")
    else:
        desc = series_data.get('cover_letter', {})
        desc = desc.get('name', '') if desc else ''
        series_id = cser.db.series_add(clean_name, desc)
        cser.db.series_set_source(series_id, 'review')

    svid = cser.db.ser_ver_add(series_id, version,
                               link=str(link))

    patches = series_data.get('patches', [])
    pcommits = []
    for i, patch in enumerate(patches):
        pcommits.append(Pcommit(
            idnum=None, seq=i,
            subject=patch.get('name', ''),
            svid=svid, change_id=None, state=None,
            patch_id=patch.get('id'), num_comments=0))
    if pcommits:
        cser.db.pcommit_add_list(svid, pcommits)

    cser.commit()
    tout.notice(
        f"Added series '{clean_name}' v{version} to database")
    return series_id, svid


# pylint: disable=R0914
def do_review(args, pwork, cser):
    """Run the review command

    Downloads patches from patchwork, applies them to a local
    branch using a Claude agent, and (in later stages) reviews
    each patch.

    Args:
        args (Namespace): Command-line arguments
        pwork (Patchwork): Configured patchwork instance
        cser (Cseries): Open cseries instance
    """
    if not args.pw_link and not args.title:
        raise ValueError(
            "Please provide -l <link> or -t <title> to "
            "identify the series")

    link = args.pw_link
    if not link:
        link = search_series(pwork, args.title)

    # Fetch series metadata from patchwork
    async def _fetch():
        async with aiohttp.ClientSession() as client:
            return await pwork.get_series(client, link)

    loop = asyncio.get_event_loop()
    series_data = loop.run_until_complete(_fetch())

    series_name = series_data.get('name', f'series-{link}')
    version = series_data.get('version', 1)
    patch_count = series_data.get('received_total', 0)
    clean_name = _clean_series_name(series_name)

    tout.notice(f"Series: {clean_name}")
    tout.notice(
        f"Version: {version}, Patches: {patch_count}")

    result = _register_series(
        cser, clean_name, version, link, series_data)
    if result is None:
        return 0
    series_id, _ = result

    # Determine the upstream branch for applying patches
    ups = args.upstream
    if not ups:
        ups = cser.db.upstream_get_default()
    if ups:
        upstream_branch = f'{ups}/master'
    else:
        upstream_branch = 'origin/master'

    # Download and apply the patches
    branch_name = f'review{series_id}'
    repo_path = gitutil.get_top_level()
    success, branch_name = apply_series_sync(
        pwork.url, link, branch_name, upstream_branch,
        repo_path)

    if not success:
        tout.error('Failed to apply patches to branch')
        return 1
    tout.notice(f'Patches applied to branch: {branch_name}')

    if args.apply_only:
        tout.notice('Apply-only mode; skipping review')
        return 0

    # TODO: Stage 2 - AI review of each patch
    # TODO: Stage 3 - Gmail draft creation
    tout.notice(
        'Patch application complete. '
        'AI review not yet implemented.')

    return 0
