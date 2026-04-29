---
name: is-commit-rules
description: Commit message formatting rules. Apply whenever creating or amending a git commit.
---

# Commit Message Rules

Apply these rules **on top of** the standard commit message conventions you
already follow (imperative mood, meaningful subject, explanatory body, etc.).
Do not change your usual behavior except where these rules add constraints.

## Rule 1 — 72-column limit

- The **subject line** (first line) MUST be at most 72 characters.
- Every line in the **commit body** MUST also wrap at 72 columns.
- **Exception**: in the body only, raw pasted content (logs, error messages,
  stack traces, command output) may exceed 72 columns when wrapping would
  reduce readability.

## Rule 2 — Do NOT generate a `Change-Id` trailer

When creating a **new** commit, never add a `Change-Id:` trailer yourself.
The project's git commit hook generates it automatically.

## Rule 3 — Preserve existing trailers

When **amending** an existing commit (e.g., `git commit --amend`), check
whether the current commit message contains trailers (e.g., `Change-Id:`,
`Refs:`, `Closes:`, or any other `Key: value` tags at the end of the message).
You MUST preserve all existing trailers exactly as-is — do not modify, reorder,
or remove them.

## Rule 4 — Keep the commit message up-to-date when amending

When **amending** a commit, review whether the existing subject and body
still accurately describe the changes after the amendment. If the scope
or intent of the commit has changed, update the message accordingly
(while still following Rules 1 and 3).

## Rule 5 — Always include the `Co-Authored-By` trailer

Every commit message you create MUST end with the `Co-Authored-By:`
trailer specified by your system prompt, even if no other commit in
this repository uses it. Do not omit it on the grounds of matching the
repository's existing commit style — the trailer is mandatory.

When **amending**, this rule combines with Rule 3: keep any existing
`Co-Authored-By` trailer in place; if none is present, add one.
