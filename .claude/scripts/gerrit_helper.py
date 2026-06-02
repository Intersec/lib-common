#!/usr/bin/env python3
###########################################################################
#                                                                         #
# Copyright 2026 INTERSEC SA                                              #
#                                                                         #
# Licensed under the Apache License, Version 2.0 (the "License");         #
# you may not use this file except in compliance with the License.        #
# You may obtain a copy of the License at                                 #
#                                                                         #
#     http://www.apache.org/licenses/LICENSE-2.0                          #
#                                                                         #
# Unless required by applicable law or agreed to in writing, software     #
# distributed under the License is distributed on an "AS IS" BASIS,       #
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or         #
# implied. See the License for the specific language governing            #
# permissions and limitations under the License.                          #
#                                                                         #
###########################################################################

"""
Gerrit REST API helper for Claude Code skills.

Usage:
    gerrit_helper.py fetch-comments [--commit <sha>] [--branch <branch>]
    gerrit_helper.py post-drafts [--commit <sha>] [--branch <branch>] < drafts.json

The target branch is detected automatically from the upstream tracking branch
or, failing that, by merge-base analysis against all origin/ refs.  Use
--branch to override when the automatic detection picks the wrong branch.

Authentication: set GERRIT_HTTP_USER / GERRIT_HTTP_PASSWORD environment
                variables.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from typing import Any

import requests
import urllib3
from gerrit import GerritClient

# Private Gerrit instance, bypass certificate checks
GERRIT_URL = 'https://git.corp'
urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)


# {{{ helpers


def _get_credentials() -> tuple[str, str]:
    """Return (user, password) for Gerrit HTTP auth."""
    user = os.environ.get('GERRIT_HTTP_USER')
    password = os.environ.get('GERRIT_HTTP_PASSWORD')
    if user and password:
        return user, password

    sys.exit(
        'Error: no credentials found for git.corp.\n'
        'Set GERRIT_HTTP_USER and GERRIT_HTTP_PASSWORD env vars.\n'
        '\n'
        'The HTTP password is generated in the Gerrit web UI at\n'
        'Settings > HTTP Credentials.'
    )


def _get_client() -> Any:
    """Create an authenticated GerritClient."""
    user, password = _get_credentials()
    session = requests.Session()
    session.verify = False
    return GerritClient(
        base_url=GERRIT_URL,
        username=user,
        password=password,
        ssl_verify=False,
        session=session,
    )


def _git(*args: str, stderr: int | None = None) -> str:
    """Run a git command and return stripped stdout."""
    return subprocess.check_output(
        ['git', *args], text=True, stderr=stderr
    ).strip()


def _get_change_id_from_commit(commit: str = 'HEAD') -> str:
    """Extract the Change-Id from the commit message."""
    msg = _git('log', '-1', '--format=%B', commit)
    for line in msg.splitlines():
        if line.startswith('Change-Id:'):
            return line.split(':', 1)[1].strip()
    sys.exit(f'Error: no Change-Id found in commit {commit}.')


def _get_project_name() -> str:
    """Derive the Gerrit project name from the git remote URL."""
    url = _git('remote', 'get-url', 'origin')
    # ssh://git.corp:29418/lib-common -> lib-common
    return url.rsplit('/', 1)[-1].removesuffix('.git')


def _is_close_ancestor(ref: str, max_depth: int = 100) -> bool:
    """True iff `ref` is an ancestor of HEAD within max_depth commits."""
    try:
        subprocess.check_call(
            ['git', 'merge-base', '--is-ancestor', ref, 'HEAD'],
            stderr=subprocess.DEVNULL,
        )
    except subprocess.CalledProcessError:
        return False
    try:
        depth = int(_git('rev-list', '--count', f'{ref}..HEAD'))
    except subprocess.CalledProcessError:
        return False
    return depth <= max_depth


def _get_branch(commit: str = 'HEAD') -> str:
    """Return the Gerrit target branch for the given commit."""
    # The fast paths below trust the current local branch (its upstream
    # and its name), which is reliable only when `commit` is HEAD or a
    # close ancestor — i.e. part of the user's current work. A cherry-
    # picked commit from an unrelated branch falls through to the
    # merge-base fallback so it can be located from its own history.
    trust_local = commit == 'HEAD' or _is_close_ancestor(commit)

    # Query the current local branch once; reused by every fast path.
    local: str | None = None
    if trust_local:
        try:
            candidate = _git('rev-parse', '--abbrev-ref', 'HEAD')
            if candidate != 'HEAD':  # not detached
                local = candidate
        except subprocess.CalledProcessError:
            pass

    # Fast path 1: upstream of the current branch.
    if local:
        try:
            upstream = _git(
                'rev-parse',
                '--abbrev-ref',
                '--symbolic-full-name',
                '@{u}',
                stderr=subprocess.DEVNULL,
            )
            if upstream.startswith('origin/'):
                return upstream.removeprefix('origin/')
        except subprocess.CalledProcessError:
            pass

    refs = _git('branch', '-r', '--format=%(refname:short)').splitlines()
    branches = [
        r for r in refs if r.startswith('origin/') and r != 'origin/HEAD'
    ]
    if not branches:
        sys.exit('Error: no origin/ branches found.')
    shorts = [r.removeprefix('origin/') for r in branches]

    # Fast paths 2 & 3: match the local branch name against origin/*.
    if local:
        # Exact match: local branch name is an origin branch name.
        if local in shorts:
            return local
        # Substring match: an origin branch name appears in the local
        # branch name; pick the longest (most specific) match.
        matches = [s for s in shorts if s in local]
        if matches:
            return max(matches, key=len)

    # Fallback: find closest origin/* branch by merge-base distance.
    def _score(ref: str) -> int | None:
        try:
            base = _git('merge-base', commit, ref, stderr=subprocess.DEVNULL)
            ahead = int(_git('rev-list', '--count', f'{base}..{commit}'))
            behind = int(_git('rev-list', '--count', f'{base}..{ref}'))
            return ahead + behind
        except subprocess.CalledProcessError:
            return None

    best, best_score = None, float('inf')
    for ref in branches:
        score = _score(ref)
        if score is not None and score < best_score:
            best_score = score
            best = ref.removeprefix('origin/')

    if not best:
        sys.exit('Error: could not determine target branch.')
    return best


def _get_change(
    client: Any, commit: str = 'HEAD', branch: str | None = None
) -> Any:
    """Get the GerritChange object for the given commit."""
    change_id = _get_change_id_from_commit(commit)
    project = _get_project_name()
    if branch is None:
        branch = _get_branch(commit)
    return client.changes.get(f'{project}~{branch}~{change_id}')


# }}}
# {{{ fetch-comments


def cmd_fetch_comments(args: argparse.Namespace) -> None:
    """
    Fetch unresolved review comments for the current commit.

    Prints JSON to stdout:
    [
      {
        "id": "<comment-id>",
        "path": "<file-path>",
        "line": <line-number-or-null>,
        "author": "<display-name>",
        "message": "<comment-text>",
        "updated": "<timestamp>"
      },
      ...
    ]
    """
    client = _get_client()
    change = _get_change(client, args.commit, args.branch)
    revision = change.get_revision('current')
    comments = revision.comments.list()

    # Collect comment IDs that already have a resolving draft reply,
    # so we don't re-process comments addressed in a previous run.
    resolved_by_draft: set[str] = set()
    for d in revision.drafts.list():
        if not d.get('unresolved', True) and d.get('in_reply_to'):
            resolved_by_draft.add(d['in_reply_to'])

    result = []
    for c in comments:
        if not c.get('unresolved', True):
            continue
        if c['id'] in resolved_by_draft:
            continue
        entry: dict[str, Any] = {
            'id': c['id'],
            'path': c.get('path', ''),
            'line': c.get('line'),
            'range': c.get('range'),
            'author': c.get('author', {}).get('name', 'unknown'),
            'message': c.get('message', ''),
            'updated': c.get('updated', ''),
        }
        result.append(entry)

    result.sort(key=lambda x: (x['path'], x['line'] or 0))
    json.dump(result, sys.stdout, indent=2)
    print()


# }}}
# {{{ post-drafts


def cmd_post_drafts(args: argparse.Namespace) -> None:
    """
    Post draft replies to review comments.

    Reads JSON from stdin:
    [
      {
        "id": "<comment-id>",
        "path": "<file-path>",
        "line": <line-number-or-null>,
        "range": <range-object-or-null>,
        "reply": "<reply-message>"
      },
      ...
    ]
    """
    client = _get_client()
    change = _get_change(client, args.commit, args.branch)
    revision = change.get_revision('current')

    drafts = json.load(sys.stdin)

    for draft in drafts:
        payload: dict[str, Any] = {
            'in_reply_to': draft['id'],
            'path': draft['path'],
            'message': draft['reply'],
            'unresolved': False,
        }
        if draft.get('line') is not None:
            payload['line'] = draft['line']
        if draft.get('range') is not None:
            payload['range'] = draft['range']
        revision.drafts.create(payload)

    print(
        f'Posted {len(drafts)} draft reply(ies).',
        file=sys.stderr,
    )


# }}}
# {{{ main


def main() -> None:
    parser = argparse.ArgumentParser(
        description='Gerrit REST API helper for Claude Code.'
    )
    sub = parser.add_subparsers(dest='command', required=True)

    p_fetch = sub.add_parser(
        'fetch-comments',
        help='Fetch unresolved review comments for a commit.',
    )
    p_fetch.add_argument(
        '--commit',
        default='HEAD',
        help='Git commit to inspect (default: HEAD).',
    )
    p_fetch.add_argument(
        '--branch',
        default=None,
        help='Override the auto-detected Gerrit target branch.',
    )
    p_fetch.set_defaults(func=cmd_fetch_comments)

    p_post = sub.add_parser(
        'post-drafts',
        help='Post draft replies to review comments (reads JSON from stdin).',
    )
    p_post.add_argument(
        '--commit',
        default='HEAD',
        help='Git commit to inspect (default: HEAD).',
    )
    p_post.add_argument(
        '--branch',
        default=None,
        help='Override the auto-detected Gerrit target branch.',
    )
    p_post.set_defaults(func=cmd_post_drafts)

    args = parser.parse_args()
    args.func(args)


# }}}


if __name__ == '__main__':
    main()
