---
name: is-final-pass
description: Final quality pass on a finished feature or patch series. It proposes to run /simplify on the whole series and /is-review on each commit, inside one clean-context sub-agent. Load when the development of a series or feature ends, or when the user says the work is done, ready for review, or ready to push to Gerrit.
argument-hint: "[base-ref]"
---

# Final Pass on a Series

Run the two quality skills on a finished series without polluting the
current context. One sub-agent does the review work. This session only
presents the result and applies the fixes that the user accepts.

## Step 0 — Resolve the series

1. Base, in this order of preference:
   - `$ARGUMENTS` if given;
   - the commit the series started from, when this conversation
     implemented the series and you know it;
   - else `@{upstream}`. If it fails with `fatal: no upstream
     configured`, ask the user for the base. Do not guess it.
   Let `BASE` be the output of `git rev-parse <base>`.
2. Commits: `git log --oneline --reverse BASE..HEAD`. If the list is
   empty, tell the user and stop.
3. Working tree: `git status --porcelain --untracked-files=no` must
   print nothing. If it does, tell the user and stop. `simplify` works
   in the working tree, and its changes must be told apart from the
   ones already there.
4. Record `ORIG`, the output of `git rev-parse HEAD`.

## Step 1 — Propose, do not start

Never start the pass on your own. If the user did not type
`/is-final-pass`, ask first with the AskUserQuestion tool. Show the
base and the commit list, so that the user can correct the base:

> The series looks finished. Do you want the final pass now?
> Base: <BASE short sha> <subject>. Commits:
> <the list from step 0>
> It runs /simplify on the whole series, then /is-review on each
> commit, in a sub-agent.

If the answer is no, stop. Do not ask again in this conversation. If
the user gives another base, go back to step 0 with it.

## Step 2 — Launch one reviewer sub-agent

Launch exactly one `general-purpose` agent with the Agent tool and wait
for it. Do not run /simplify or /is-review in this context: the diffs
and the agent outputs belong in the sub-agent.

Pass the prompt below. Fill in `BASE`, `ORIG`, the repository path
and the commit list, everywhere they appear. A sub-agent inherits the
working directory of this session, which may not be the repository of
the series.

````
You are the final quality pass on a finished patch series in this
repository. Two skills do the work: `simplify` and `is-review`. Invoke
them with the Skill tool.

Token budget: `simplify` launches sub-agents of its own, and its own
rule sets how many. That rule wins: do not cap it here. In Part B,
`is-review` follows its own sub-agent rule; that one wins there.
Launch no agent beyond what these two skills ask for. A long
sequential pass is the expected cost.

Repository: <absolute path>. Run every git command there.
Series: BASE=<sha>  ORIG=<sha> (HEAD before this pass)
Commits, oldest first:
<git log --oneline --reverse BASE..HEAD>

## Part A — Collect the simplify proposals

Propose, never apply. The series must come out of this part exactly as
it went in. The user arbitrates, and this prompt has no user.

1. Invoke `simplify` with the argument `BASE...HEAD`. Let it apply its
   fixes to the working tree.
2. If `git status --porcelain` prints nothing, go to Part B. Count an
   untracked file too: `simplify` can create one.
3. List what `simplify` touched: `git status --porcelain`. Keep the
   modified files apart from the created ones. Step 6 restores both.
4. Number the fixes from 1, in the order `simplify` reports them, and
   save one patch each. Several fixes often share a file, so one
   `git diff` per file cannot separate them. Take them one at a time,
   each from the restored tree: apply that fix alone, run `git add -AN`
   so that a created file appears, then
   `git diff -- <its files> > "${TMPDIR:-/tmp}/fix-<n>.patch"`, then
   restore the tree as step 6 says before the next fix. Each patch
   must apply to the series on its own. If two fixes overlap the same
   lines and cannot be separated, save them as one fix and say so.
5. Give each fix an owner, by its lines and not by its file. For each
   hunk take the lines it removes or replaces, in pre-image numbers,
   and run `git log -L <start>,<end>:<file> --format=%h BASE..HEAD`.
   Narrow the range to those lines: the `@@` header also counts the
   context around them, which often belongs to another commit. A hunk
   that only adds lines has nothing to narrow to; use the `@@` range
   and mark the owner a guess.
   Ask the committed history, never the working tree: the fixes are
   uncommitted there, and blame would answer `00000000 (Not Committed
   Yet` for the very lines to attribute.
   The newest commit returned owns the hunk. One owner across every
   hunk owns the fix. Two or more leave it ambiguous: report them all
   and let the user choose. None means the series never touched those
   lines: report the fix as ownerless.
6. Restore the working tree: `git reset` to drop the entries that
   `git add -AN` created, `git checkout --` the modified files, and
   delete the created ones. Never run `git clean`: it destroys
   untracked files that are not yours. Then check the restore twice:
   `git status --porcelain --untracked-files=no` must print nothing,
   and no file that step 3 listed as created must still exist. That
   status ignores untracked files, so it cannot see one left behind.

## Part B — Review each commit

Read the series: `git log --format=%h --reverse BASE..HEAD`. For each
commit, oldest first, invoke `is-review` with the commit SHA as
argument. Run it in this context, one commit after the other. You have
no user to answer, so skip its "Offer to fix" step and keep the report.

## Part C — Compare the two parts

A Part A fix and a Part B finding can contradict each other: they
touch the same lines and ask for opposite changes, or one deletes what
the other wants documented. Only the user can arbitrate, so do not
choose a side.

List every pair that touches the same file and the same lines, and
mark each pair "agree" or "conflict". Give the fix number, the finding
as `<short sha>#<number>`, and one line on what they ask for.
`is-review` restarts its numbering at 1 for each commit, so a bare
number names three findings. If no pair overlaps, say so in one line.

## Part D — Report

Return one report and nothing else:

- Series: BASE and HEAD. This pass leaves them untouched.
- Part A: one numbered item per fix — its files, what it changes, why,
  the owner commit or the ambiguous candidates, and its patch path.
  Then the findings that `simplify` skipped.
- Part B: one `## Commit Review` block per commit, in the shape
  `is-review` gives, with the short SHA added to the header. Findings
  only. Step 3.4 of this skill needs that SHA.
- Part C: the pairs, each marked "agree" or "conflict".
- Verdict for the series: "Looks good", "Minor issues", or
  "Issues to address".
````

## Step 3 — Present and arbitrate

1. Show the user the full report. Do not shorten the findings.
2. Say that the series is still untouched: nothing is applied until
   the user picks.
3. Ask which items to apply, with the AskUserQuestion tool. Offer the
   Part A fixes, the Part B findings, and the Part C conflicts. A
   conflict needs one answer: which side wins.
4. Fold each accepted item into its commit, then run one autosquash:
   - Part A fix: `git apply "${TMPDIR:-/tmp}/fix-<n>.patch"`, then
     `git add <its files>`, `git commit --fixup=<owner>`.
   - Part B code fix: edit the code, `git add <files>`, then
     `git commit --fixup=<sha>`.
   - Message fix: load `is-commit-rules`, write the full new message
     in a file with the first line `amend! <old subject>`, then
     `GIT_EDITOR="cp <file>" git commit --allow-empty --fixup=amend:<sha>`.
   - Ambiguous owner: ask the user which commit takes the fix. If the
     file sits outside the series, leave the fix in the working tree,
     `git stash push -u -- <its files>` before the rebase, and
     `git stash pop` after it.
   - Then: `GIT_SEQUENCE_EDITOR=true git rebase -i --autosquash BASE`.
5. `git reset --hard ORIG` undoes every commit this step made. Give
   the user that command, and warn that it also destroys a fix left
   uncommitted in the working tree. That patch stays under
   `${TMPDIR:-/tmp}`, so the user can apply it again.
6. Do not push. The user pushes to Gerrit.
