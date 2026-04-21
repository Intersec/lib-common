---
name: is-apply-review
description: Fetch Gerrit review comments on the current commit or a range of commits, interactively apply or reject each one, then post draft replies and update the local commit(s). Use when the user wants to apply review comments from Gerrit.
argument-hint: [<commit-or-range>]
disable-model-invocation: true
---

# Apply Gerrit Review Comments

Interactively process review comments from Gerrit: for each unresolved
comment, either apply a fix or record a reason for skipping it, then
post a draft reply and update the local commit.

## Argument

- **No argument or `HEAD`**: run the single-commit procedure below.
- **Single commit ref** (e.g. `HEAD~3`, a SHA): the ref MUST be an
  ancestor of HEAD (verify with `git merge-base --is-ancestor <ref>
  HEAD`; abort otherwise). Run `git rebase -i <ref>~` with an `edit`
  marker on `<ref>`, apply the single-commit procedure at the pause,
  then `git rebase --continue`.
- **Git range** (contains `..`, e.g. `HEAD~5..`): process every commit
  in the range that has unresolved comments. See "Range mode" below.

## Range mode

Walk through the commits with comments via `git rebase -i`, amending
each one in place:

1. For each commit in `git rev-list --reverse <range>`, run
   `fetch-comments --commit <sha>` and keep the ones that have
   unresolved comments. Collect them into a shell array `EDIT_SHAS`.
2. If `EDIT_SHAS` is empty, tell the user and stop.
3. Start an interactive rebase that stops at each flagged commit.
   Use `git rev-parse --short` per SHA so the pattern matches the
   abbreviation git actually writes to the rebase TODO (length
   auto-scales with repo size). Use array syntax (`"${EDIT_SHAS[@]}"`)
   so the loop works in both bash and zsh:
   ```
   ABBREV=$(for sha in "${EDIT_SHAS[@]}"; do git rev-parse --short "$sha"; done | paste -sd'|' -)
   GIT_SEQUENCE_EDITOR="sed -Ei 's/^pick ($ABBREV) /edit \\1 /'" \
       git rebase -i <base>
   ```
   where `<base>` is the part of the range before `..`.
   The `GIT_SEQUENCE_EDITOR` is non-interactive (a sed one-liner), so
   git sets up the rebase state on disk and returns control to the shell
   at the first `edit` stop. Subsequent `git rebase --continue` calls
   advance through each stop the same way.
4. At every pause, HEAD is the commit being edited. Run the
   **single-commit procedure** below, then:
   ```
   git rebase --continue
   ```

## Single-commit procedure

### Step 1 — Fetch review comments

```
.claude/scripts/gerrit_helper.py fetch-comments
```

Parse the JSON. If empty, skip this commit. Otherwise list the
comments briefly (path, line, author, first line of the message).

### Step 2 — Process each comment

For each unresolved comment:

1. Show the comment (path, line, author, full message).
   - If `path` is `/COMMIT_MSG`, the comment is on the commit message;
     fetch it with `git log -1 --format=%B HEAD` and locate the line.
   - Otherwise, read the file around the commented line.
2. Ask the user: *"Do you want to apply this comment?"*
3. If yes:
   - Propose a concrete fix (file edit, or a commit-message edit).
   - On approval, apply it.
   - Record as **applied**.
4. If no:
   - Ask the user for a reason.
   - Record as **rejected** with that reason.

### Step 3 — Post draft replies

Pipe one entry per processed comment to the helper:

- **Applied** → reply `"Done"`.
- **Rejected** → reply is the reason the user provided.

```
echo '<json>' | .claude/scripts/gerrit_helper.py post-drafts
```

```json
[
  {
    "id": "<comment-id>",
    "path": "<file-path>",
    "line": "<line-or-null>",
    "range": "<range-object-or-null>",
    "reply": "Done"
  }
]
```

Only drafts are created; the user publishes them manually in the
Gerrit UI.

### Step 4 — Amend the commit

Skip if nothing was applied.

- File edits only: `git add -u && git commit --amend --no-edit`
- Commit-message edit applied: pipe the new full message (preserving
  existing trailers like `Change-Id`, `Co-Authored-By`) on stdin to
  `git commit --amend -F -`. If file edits were also applied,
  `git add -u` first.

When called from range mode during a rebase, HEAD is always the commit
being edited, so amending HEAD is correct.

Finish by telling the user the tally (applied vs rejected) and
reminding them to publish the draft replies on Gerrit.
