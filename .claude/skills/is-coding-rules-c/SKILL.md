---
name: is-coding-rules-c
description: C/blk coding rules for the Intersec codebase. Load before writing or reviewing C or blk code.
---

# Intersec C Coding Rules

## Formatting

- 4 spaces, no tabs. **78-column limit.**
- Opening brace same line for control flow, next line for functions.
- Always use braces, even for single-statement blocks.
- Pointer asterisk on the variable: `char *ptr` not `char* ptr`.
- **Comments: `/* */` only — never `//`.**

## Error-handling macros (`core/macros.h`)

```c
RETHROW(e)             /* if (e < 0) return e;        */
RETHROW_P(e)           /* if (e == NULL) return NULL; */
THROW_ERR_UNLESS(cond) /* if (!(cond)) return -1;     */
THROW_FALSE_IF(cond)   /* if (cond) return false;     */
```

`RETHROW` / `RETHROW_P` are expressions that evaluate to `e` on success, so
use them inline: `x = RETHROW(foo());`.

Use these instead of explicit `if` checks when no cleanup is needed before
returning. When cleanup is required, use explicit `if` + `goto` cleanup,
or the `defer` macro (see `core/macros.h`).

## Forbidden functions

- Memory: `malloc`, `free`, `realloc`, `calloc`, `alloca` — use
  `p_new`/`p_delete`/`p_realloc` (heap) or the `mp_*` pool
  equivalents.
- Strings: `strncpy`, `strcpy`, `strcat`, `sprintf`, `gets`, `strtok`
  — no single replacement; pick per context: `sb_t` builder
  (`sb_add*`, `sb_addf`), `pstream_t` reader, or `snprintf` for
  bounded writes into a plain buffer.
