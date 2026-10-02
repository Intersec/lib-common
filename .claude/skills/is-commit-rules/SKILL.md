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

**Default shape: subject + one short paragraph.** More than that is
the exception and must earn itself. Calibrate against the repository,
not against this file: read the last few commits touching the same
area and match their length. Removals are the shortest of all — name
what the dead thing did and why nothing needs it now (its successor,
the SHA that killed the last caller, or the tool that found it), and
stop. A commit whose subject *is* a test says what the test locks down
and where a violation would surface, never how the test is built.

**Then cut.** After drafting, delete every sentence that does not
change what the reviewer does:

- reassurance about the checks you ran ("all suites stay green") —
  that belongs in the review comment, not in permanent history;
- a snapshot of your investigation ("the test passes, so the bug is
  downstream") — true today, false after the next fix, and useless to
  a reader who never watched the hunt;
- what the commit does *not* touch — one sentence at most, and only
  when a reviewer would otherwise think the change is incomplete;
- narration of test edits — tests dying with the code they cover, or
  following a renamed API, is the expected default;
- the roadmap — an inventory of what a later patch will remove or
  fix, which the series itself already shows. This is not a ban on
  the future tense: where a commit only makes sense because of what
  comes next, that *is* its why and belongs in the first sentence —
  a preparatory rename, a deliberately temporary state, a helper
  whose caller arrives later. **Litmus: delete the sentence; if the
  commit still makes sense, it was inventory.** Keep too the form
  that defends this commit's scope ("migrating those tests is its own
  patch"), which answers a review comment before it is written;
- context an earlier commit of the same series already gave — say it
  once, where it belongs, and let the reader walk the series;
- anything the subject already said.

**When two rules both apply, the shorter message wins.** The rules
below say what a message must contain *when it contains anything*;
none of them is a reason to grow past what the reviewer needs.

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

## Rule 9 — A fix names its origin

A bug-fix commit message cites the SHA1 of the commit that
**introduced** the defect. When a first commit created the fragile
situation and a later one turned it into a bug (or widened its blast
radius), cite **both**. State where the origin sits relative to the
release branches ("predates all live branches", "reached r2022 via
merge X"), so the reviewer can decide the backport target **from the
message alone**.

**One line by default:** "Bug brought with merge commit <SHA1> in
r2022." Expand only when the extra detail changes the reviewer's
verdict. **Optional for non-bugs:** a harmless inefficiency poses no
backport question — a minimal "Comes from <SHA1>" if already
investigated.

**How to investigate:** `git log -S'<buggy line>' -- <file>` (pickaxe),
`git blame -L`, `git merge-base --is-ancestor <sha> <branch>`; beware
file moves (`--follow`). **Pickaxe trap:** `-S` finds the last commit
that *rewrote the string*, not necessarily the one that created the
problem — check its diff; for an order-of-code problem the origin is
the commit that created the *order*.

## Rule 10 — Tell the story without the diff

The reader of a commit message usually does not open the diff. The
message must be understandable on its own; the diff should *deepen*
comprehension, never be required for basic sense.

"Understandable on its own" means the reader can decide whether to
open the diff and what to look for — not that they could reconstruct
it from the message. Where this rule and rule 11's "do not paraphrase
the diff" both bite, rule 11 wins: cut the explanation rather than
grow it.

- **No unpresented characters.** Cheapest fix first: do not name the
  thing at all — name a symbol only when the reader must grep for it.
  Otherwise introduce it by its role in one clause, or replace it with
  the role ("the descriptor of the backup directory", not "dfd_dst").
- **Prefer the general principle to the specifics.** Readers get
  principles better than domain nouns: "an object was built before the
  early return that makes it useless — build it just before use" beats
  naming the tree, the guard and the query type.
- **Open with the TL;DR.** First sentence = the failure at its most
  general level; mechanics and fix follow.
- **Tense separates the old code from the new.** Describe how the code
  behaved before the commit in the past tense. Describe the change in
  the imperative. Keep the present tense for facts that the commit does
  not change. A reader takes each sentence in the present tense as true
  after the commit.

      Bad:  The proxy paths call the post hook only when they can
            forward the answer.
      Good: The proxy paths called the post hook only when they could
            forward the answer. Make them call it first.
      Good: Channel ids are never reused.   (still true after the fix)
- **No diff-relative references** ("the line", "the check just above").
- **A deep explanation carries its own context.** When quoting code,
  separate it from the prose as an indented block and annotate it
  (ASCII pointers under the offending tokens) rather than weaving the
  expression into a sentence.

**Litmus:** read the message with the diff hidden — does every sentence
refer to something the reader can picture?

## Rule 11 — Proportionality; the code is the truth

- **The message is the commit's architecture, not its details.** Its
  three jobs: (1) what the commit does *globally* — the subject
  summarizes, the body completes; (2) *why* — context and story are
  welcome when they contextualize the problem; (3) *what parts of the
  code are modified*, when the diff reaches modules the subject does
  not imply: the diff's file order is random, the message tells the
  reader what to expect. What it must NOT do is paraphrase the diff:
  the reader reads the diff for the details. (This refines rule 1:
  the *global* what belongs in the message; the diff-level what/how
  still does not.)
- **Budget: see rule 1.** A body past ~15 lines is not merely long, it
  is a paraphrase of the diff; the fix is the cut pass, not tighter
  wording.
- **Never omit that the commit fixes a bug.**
- **Name the error class when it is a common one** (copy-paste fail,
  typo, inverted condition, off-by-one): the class IS the story, and
  naming it *replaces* the narration.
- **A verified consequence earns one sentence only when it changes the
  reviewer's decision** — the backport target, whether to block, what
  to re-test. Verification you did for your own confidence goes in the
  review comment.
- **Calibrate "obvious" to the house reader:** never explain the
  behavior of basic in-house APIs.
- **One idea per paragraph:** nature of the issue, consequence, origin —
  each its own sentence or one-line paragraph, never glued by a colon.
- **The code is the truth:** if reading the *code* raises a question,
  the answer belongs in the code, not in the message.
- **Scaffold only when length is earned** — a non-obvious bug, or a
  decision a reviewer will contest: Context / Issue / Fix.

## Rule 12 — When a change needs its own commit

- **Code moves / file renames** get a dedicated commit containing no
  change beyond what the move strictly requires: the reviewer can then
  verify "pure move" at a glance, and real changes are never hidden
  inside relocation noise.
- **A properly defined feature** is one commit carrying everything it
  needs (implementation, tests, and the rest): reviewable and
  revertable as a unit.

## Before you commit

1. Count the body lines. Past ~8, justify every paragraph; past ~15,
   cut again (rule 1).
2. Every paragraph must name something the reviewer does with it. If
   it only shows that you were thorough, cut it.
3. No sentence about what you verified, what you did not touch, or
   what the tests now look like. A pointer to what comes next stays
   only when it is the reason this commit exists.
4. 72 columns; trailers in one block; `Change-Id` untouched.
5. Each sentence about the code before the commit is in the past
   tense. Each sentence in the present tense is still true after it.
