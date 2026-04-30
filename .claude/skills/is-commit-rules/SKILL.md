---
name: is-commit-rules
description: Commit message formatting rules. Apply whenever creating or amending a git commit.
---

# Commit Message Rules

Apply these rules **on top of** your usual commit message conventions
(imperative mood, meaningful subject, etc.). They add constraints; they
don't replace your defaults.

## Rule 1 — 72-column limit

Subject and every body line MUST wrap at 72 columns. Exception: in the
body, raw pasted content (logs, errors, stack traces, command output)
may exceed 72 when wrapping would hurt readability.

## Rule 2 — Do NOT generate a `Change-Id` trailer

Never add `Change-Id:` to a new commit; the project's git hook generates
it automatically.

## Rule 3 — Preserve existing trailers

When amending, keep all existing trailers (`Change-Id:`, `Refs:`,
`Closes:`, any `Key: value` tags at the end) exactly as-is — no
modification, reordering, or removal.

## Rule 4 — Keep the message up-to-date when amending

After amending, update the subject/body if the scope or intent changed
(still respecting Rules 1 and 3).

## Rule 5 — Always include the `Co-Authored-By` trailer

Every commit MUST end with the `Co-Authored-By:` trailer from your
system prompt, regardless of repo style. When amending, combine with
Rule 3: keep an existing one, or add it if missing.

## Rule 6 — Redmine ticket trailers (`Refs` / `Closes`)

This project links commits to Redmine tickets via:

- `Refs: #XXX #YYY` — related to those tickets, work not finished.
- `Closes: #XXX #YYY` — final commit for those tickets.

Syntax: `#` + numeric id, multiple ids space-separated on one line.

Applies **only to new commits**, not amends:

1. If the user named tickets (and Refs vs. Closes), set the trailer(s)
   without further prompting.
2. Otherwise, ASK before committing whether any tickets should be
   `Refs:`'d or `Closes:`'d, and wait for the answer.

When amending, do not invoke this rule: existing trailers are preserved
per Rule 3, and no new ones are added unless the user asks.
