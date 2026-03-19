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

## Rule 2 — Preserve existing trailers

When **amending** an existing commit (e.g., `git commit --amend`), check
whether the current commit message contains trailers (e.g., `Change-Id:`,
`Refs:`, `Closes:`, or any other `Key: value` tags at the end of the message).
You MUST preserve all existing trailers exactly as-is — do not modify, reorder,
or remove them.
