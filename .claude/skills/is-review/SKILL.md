---
name: is-review
description: Deep code review of a commit covering correctness, commit message quality, and English usage. Use when the user wants to review a commit.
argument-hint: "[commit-sha]"
disable-model-invocation: true
---

# Code Review Skill

Perform a deep review of a git commit.

## Target commit

- If `$ARGUMENTS` is provided, review that specific commit.
- If no argument is provided, review the **current HEAD commit** (i.e., `HEAD`).

Let `COMMIT` denote the resolved commit SHA.

## Step 1 — Gather context

Run these commands to collect all the information you need:

1. `git log -1 --format="%H%n%s%n%n%B" COMMIT` — full commit message
2. `git diff COMMIT~1..COMMIT` — the full diff
3. `git diff --stat COMMIT~1..COMMIT` — summary of changed files

Read every file touched by the commit so you have the full surrounding context, not just the diff hunks.

## Step 2 — Analyze

Perform a thorough analysis on three axes. For each axis, list concrete findings with file paths and line references.

### A. Commit message quality

- Does the subject line clearly summarize **what** the commit does?
- Does the body explain **why** the change is made?
- Is the message accurate with respect to the actual code changes?
- Flag vague, misleading, or missing information.

### B. Correctness

This is the most important axis. Go beyond the diff:

- **Logic bugs**: off-by-one, null/invalid pointer dereference, missing error checks, resource leaks, use-after-free, integer overflow, uninitialized variables.
- **API misuse**: verify that every function/macro called in the diff is used according to its contract (check parameter types, ownership semantics, return value conventions). Read headers or source if unsure.
- **Concurrency**: data races, missing locks, incorrect ordering.
- **Edge cases**: empty inputs, boundary values, error paths.
- **Consistency**: does the change follow the patterns established in the surrounding code and the project's coding style (see CLAUDE.md)?
- **Completeness**: are all necessary call sites updated? Are tests added or updated to cover the new behavior?

### C. English quality

Check **all** English text introduced or modified by the commit:

- Commit message (subject + body)
- Code comments
- Log/error messages and string literals
- Documentation, behave steps, test descriptions

Flag grammar mistakes, typos, unclear phrasing, or inconsistent terminology.

## Step 3 — Report

Present a clear, structured report with the following sections:

```
## Commit Review: <short subject>

### Commit message
<findings or "No issues.">

### Correctness
<findings or "No issues.">

### English
<findings or "No issues.">

### Summary
<one-line overall verdict: "Looks good", "Minor issues", or "Issues to address">
```

For each finding, include:
- The file and line (or commit message line) concerned
- What the problem is
- A suggested fix

## Step 4 — Offer to fix

If any findings were reported, ask the user:

> Would you like me to fix the issues listed above?

Wait for confirmation before making any changes. When fixing:
- For commit message issues, use `git commit --amend` to rewrite the message.
- For code issues, edit the files and then let the user decide whether to amend the commit or create a new one.
- For English issues in code, edit the files similarly.

Do **not** make any changes without explicit user approval.
