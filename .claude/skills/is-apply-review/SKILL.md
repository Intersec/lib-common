---
name: is-apply-review
description: Fetch Gerrit review comments on the current patch, interactively apply or reject each one, then post draft replies and amend the commit. Use when the user wants to apply review comments from Gerrit.
disable-model-invocation: true
---

# Apply Gerrit Review Comments

Interactively process review comments from Gerrit on the current commit:
for each unresolved comment, either apply a fix or record a reason for
skipping it, then post draft replies and update the local commit.

## Step 1 — Fetch review comments

Run the Gerrit helper to get all unresolved comments on the current commit:

```
.claude/scripts/gerrit_helper.py fetch-comments
```

Parse the JSON output. If there are no unresolved comments, inform the user
and stop.

Otherwise, tell the user how many unresolved comments were found and list
them briefly (path, line, author, first line of message).

## Step 2 — Process each comment

For **each** unresolved comment, in order:

1. **Show the comment** to the user: display the file path, line number,
   author, and full comment message.
2. Read the relevant section of the file around the commented line so you
   (and the user) have full context.
3. **Ask the user**: *"Do you want to apply this comment?"*
4. **If yes**:
   - Propose a concrete fix (show the code change you intend to make).
   - Wait for user approval, then apply the edit.
   - Record this comment as **applied**.
5. **If no**:
   - Ask the user to provide a reason for not applying it.
   - Record this comment as **rejected** with the user's reason.

Continue until every comment has been handled.

## Step 3 — Post draft replies on Gerrit

Build a JSON array with one entry per processed comment and pipe it to the
helper via stdin:

- For **applied** comments the reply is `"Done"`.
- For **rejected** comments the reply is the reason the user gave.

```
echo '<json>' | .claude/scripts/gerrit_helper.py post-drafts
```

Where `<json>` looks like:

```json
[
  {
    "id": "<comment-id>",
    "path": "<file-path>",
    "line": "<line-or-null>",
    "range": "<range-object-or-null>",
    "reply": "Done"
  },
  {
    "id": "<comment-id>",
    "path": "<file-path>",
    "line": "<line-or-null>",
    "range": "<range-object-or-null>",
    "reply": "<reason provided by the user>"
  }
]
```

**Important**: this only creates *draft* replies. It does **not** submit or
publish them — the user will do that manually in the Gerrit UI.

Inform the user that draft replies have been posted and that they need to
review and submit them on Gerrit.

## Step 4 — Amend the local commit

If any comments were applied (i.e., files were edited), amend the current
commit to include the fixes:

```
git add -u
git commit --amend --no-edit
```

If no comments were applied, skip this step.

Tell the user the final status: how many comments were applied, how many
were rejected, and remind them to publish the draft replies on Gerrit.
