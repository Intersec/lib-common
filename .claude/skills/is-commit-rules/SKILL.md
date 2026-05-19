---
name: is-commit-rules
description: Commit message formatting rules. Apply whenever creating or amending a git commit.
---

# Commit Message Rules

Apply these rules **on top of** your usual commit message conventions
(imperative mood, meaningful subject, etc.). They add constraints; they
don't replace your defaults.

## Rule 1 — Explain *why*, keep it concise

The subject states *what* changed in imperative mood. The body MUST
explain *why* — the motivation, constraint, or trade-off that the diff
alone cannot reveal. Do NOT restate *what* or *how*: those are already
visible in the modification itself.

Keep the body short. A few lines is usually enough; add length only
when the *why* genuinely needs it (prior incident, hidden constraint,
non-obvious decision). A reviewer should grasp the motivation in
seconds, not paragraphs.

## Rule 2 — 72-column limit

Subject and every body line MUST wrap at 72 columns. Exception: in the
body, raw pasted content (logs, errors, stack traces, command output)
may exceed 72 when wrapping would hurt readability.

## Rule 3 — Do NOT generate a `Change-Id` trailer

Never add `Change-Id:` to a new commit; the project's git hook generates
it automatically.

## Rule 4 — Preserve existing trailers

When amending, keep all existing trailers (`Change-Id:`, `Refs:`,
`Closes:`, any `Key: value` tags at the end) exactly as-is — no
modification, reordering, or removal.

## Rule 5 — Keep the message up-to-date when amending

After amending, update the subject/body if the scope or intent changed
(still respecting Rules 1, 2, 3 and 4). Do not describe the changes
between patchsets; describe the final state.

## Rule 6 — Always include the `Co-Authored-By` trailer

Every commit MUST end with the `Co-Authored-By:` trailer from your
system prompt, regardless of repo style. When amending, combine with
Rule 4: keep an existing one, or add it if missing.

## Rule 7 — Redmine ticket trailers (`Refs` / `Closes`)

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
per Rule 4, and no new ones are added unless the user asks.

## Rule 8 — `RunTests:` for `@slow` Behave scenarios

When the diff includes Behave `.feature` files, check whether any
added or modified scenarios carry the `@slow` tag.  If so, a
`RunTests:` footer is required so those scenarios run during review
(they are otherwise excluded and run only in nightly campaigns).

Reference the scenario's identifying tag(s) in `RunTests:` (typically
`@redmine_XXXXX` but may be any tag that uniquely identifies the scenario).

- **1–3 impacted tags** — list them all: `RunTests: @redmine_A @redmine_B`
- **More than 3** — do not enumerate all tags; list the impacted
  scenarios, propose a dedicated grouping tag to add to those scenarios
  in the feature files, and ask the developer whether they want to add
  that tag to the commit.
