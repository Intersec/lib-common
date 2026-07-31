---
name: is-coding-rules-style
description: Code style and comment rules (naming, structure, comments, corner cases). Load before writing or reviewing code, alongside is-coding-rules-c.
---

# Coding Rules — Style and Comments

Each rule is a **principle**, a **litmus test** and **carve-outs**. The
carve-outs are part of the rule: a rule applied without them produces
churn and false positives. These are a reading lens, not a linter.

## Rule 1 — Don't document what you can make obvious

A comment must answer a legitimate question a reader will ask at that
point. Before writing one, try to dissolve the question instead: a
better name, a dedicated helper, or a better design.

```c
/* Bad: the reader must decode the trick (and check it!) */
if (len & (len - 1)) { ... }

/* Good: no question left */
if (!is_powerof2(len)) { ... }
```

Extraction also **redirects review**: inline, the trick's correctness is
incidental and errors slip; in `is_powerof2()` its correctness is the
entire point of review, and it becomes unit-testable.

**Litmus:** would deleting this comment cost the reader information the
code doesn't carry? If yes, can a name or helper carry it instead?

**Carve-outs:**
- A comment that survives restructuring is the legitimate kind — writing
  it is a success, not a failure: cross-function protocols,
  pointer-invalidation warnings.
- Contract documentation on an exposed API is always legitimate.
- When dissolving duplication has a known, measured cost, the comment
  recording the trade-off IS the fix, not the refactor.
- Repetition whose instances differ only in data, not logic, is a
  declaration list, not duplication — leave it flat.

## Rule 2 — Comments live in the present

A comment describes what the code *does*, never what it *did*. A change
of behavior belongs in the commit message. `TODO`/`FIXME` may speak in
the future — and must be re-earned on every behavior change (a TODO
about removed code is a bug).

**Litmus:** is the comment still true and useful for someone who never
saw the old code?

**Carve-out:** the past as a **live constraint or justification** is
fine: format compatibility ("v2 archives store local time; keep parsing
both"), benchmark provenance, pointers to a reference implementation of
migrated code.

## Rule 3 — A function does one thing, and its signature tells the truth

Detach a function from its context when possible: clear contract — "I
take this, I do this, I return this". Passing a 20-field struct to use 2
fields hides the data flow: pass the 2 values. The rule is symmetric: 15
loose parameters of which 8 are loop-wide invariants fail it too —
bundle the invariants into a state struct. The real statement: *the
signature must make the actual inputs legible.*

**Carve-outs:**
- Struct-as-object: a function *about* the object takes the object
  (`qv_sort(vec)`, `db_flush(db)`).
- Uniform vtable/callback signatures: implementations legitimately
  ignore parameters; the uniform signature IS the contract.
- A state machine's "one thing" can be the whole state machine.

## Rule 4 — Names describe role, not type

```c
/* Bad: the role is "the object ids retained by the filter" */
qv_t(u64) vec;

/* Good */
qv_t(u64) matching_ids;

/* Fine: a scratch whose vectorness IS the point (sort/uniq pass) */
qv_t(u64) vec;
```

Also catches wrong-kind names: a `has_*` name announcing a boolean but
storing a count reads as nonsense at its use sites. Name the unit when
two units must share an expression (`payload_len` vs a word count);
prefer restructuring so they don't.

**Carve-out:** conventional idioms are roles by convention: `i`/`j`
cursors, `sb`, `ps`.

## Rule 5 — A block is a step

A function reads as a sequence of blocks; a block is one step,
summarizable in one sentence. Blank lines separate steps and never
appear inside one: **a blank line is an invitation to insert code**. A
40-line wall with no seams hides the structure just as badly.

Corollaries:
- A call and its error check are one step — nothing between them.
- Guards come before work.
- An assignment stays in the block that first uses it (declarations stay
  at the top of the function, per C convention; it is the *assignment*
  that must not drift).
- A block that needs a title comment is a sub-function candidate.
- Uniform block shape is load-bearing: in a run of same-shaped blocks,
  the one deviant block deserves the closest review — look-alike lines
  in uniform blocks are where copy-paste bugs live.

**Litmus:** if someone inserted code at this blank line, would the logic
still read correctly? Is this assignment more than one block away from
its first use, with nothing anchoring its position?

**Carve-outs:**
- Position-semantic assignments: timer starts, `errno = 0`, lock
  acquisitions, capture-before-mutation snapshots, out-params
  defensively initialized at entry, `ret = -1` feeding a `goto` chain.
- Whole-function accumulators sit at the top.
- Declaration clusters that are themselves a step.
- Moving work below a guard is only valid when it has no checking or
  observable effect: an `assert()` that must run on every call stays
  above the guard.

## Rule 6 — The reference goes second in a comparison

Put the moving value first, the reference second — it is how we speak:
`if (nb_tries < MAX_RETRIES)`, not `while (MAX_RETRIES > nb_tries)`; no
Yoda conditions (the compiler catches `=` typos).

**Exception — ranges read in number-line order**, the references
bracketing the moving value:

```c
if (4 < i && i < 10) { ... }        /* not: i > 4 && i < 10 */
if (0 <= idx && idx < len) { ... }
```

## Rule 7 — Corner-case hygiene

Every special-case branch is a test you owe. Prefer formulations where
the **general case swallows the corner cases** by construction:
half-open intervals `[begin, end)`; a `for` loop that handles `len == 0`
by itself needs no guard; a dummy list head absorbs insert-at-head;
`MIN(BATCH, total - pos)` makes the final partial batch the general
case. When special cases pile up, stop patching and look for the
formulation that absorbs them.

**Litmus:** before writing a guard, ask: is there a reformulation of the
general case that makes this guard unnecessary?

**Carve-outs:**
- Don't absorb what is semantically distinct to the caller.
- Don't let absorption silently produce garbage: fewer *branches*, never
  fewer *defined behaviors*.

## Rule 8 — Context lives at the call site

A function comment documents the **contract**, seen from the callee.
What the function is *for in the caller's algorithm* belongs at the
**call site** (or dissolves into the caller's naming). The misplaced
sentence is usually true and useful — the fix is a **move, not a
delete**.

**Litmus:** would the comment survive, unchanged, the arrival of a
second caller? Smell phrases: "Used by/on X", "passed to Y", "needed
because [the caller's architecture]", "The purpose of this function is
to".

**Carve-out:** a scope constraint phrased like caller context ("only
valid for X-style use, because...") is contract, not context.
