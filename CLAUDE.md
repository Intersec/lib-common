# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

lib-common is an Intersec C library extension targeting Linux environments. It provides core utilities (strings, containers, memory management), networking (HTTP, RPC, event loop), serialization (IOP - similar to Protocol Buffers), ASN.1, and cryptography. Licensed under Apache 2.0.

## Build System

lib-common uses WAF (https://waf.io/).

```bash
waf configure              # Configure (default profile)
waf                        # Build
waf list                   # List all targets
waf --targets=target1      # Build specific target
waf check                  # Run tests
waf fast-check             # Run fast subset of tests
```

**Profiles** (set via `P` env var): `default`, `debug`, `release`, `asan`, `tsan`, `coverage`, `fuzzing`

**Environment variables:**
- `P` - compilation profile
- `NOCHECK` - skip clang checks (faster builds)
- `NOASSERT` - disable assertions
- `FAKE_VERSIONS` - prevent re-linking on git revision changes

**Compiler:** Default is gcc. Use `CC=clang CXX=clang++ waf configure` for clang.

## Running Tests

```bash
waf check           # Tests for current directory and subdirectories
waf fast-check      # Faster subset
./static-checks.py  # Run all static checks (ruff, mypy, clippy)
```

Test binaries are in `tests/` (main test binary: `zchk`).

## Languages

- **C99** with GNU extensions and Apple's blocks extension
- **Rust** for performance-critical components (see `rust/` directory). Must follow `rust/coding-rules.md`
- **Python 3** for build tools and IOP bindings (Cython)

Files with `.blk` extension use the blocks extension and are preprocessed with `clang-rewrite-blocks`.

## Code Architecture

```
src/
├── core/           # Memory, macros, bitmasks, modules
├── container/      # Vectors (qvector), hash tables (qhash), heaps, trees, rings
├── asn1/           # ASN.1 DER/PER encoding
├── iop/            # IOP serialization framework
├── iopc/           # IOP compiler
├── net/            # HTTP, RPC, sockets, SCTP
├── el.h            # Asynchronous event loop
├── crypto/         # SSL/TLS, cryptographic functions
├── prometheus-client/  # Prometheus metrics
rust/               # Rust workspace (libcommon bindings, farchc compiler)
tests/              # Test suite (zchk test framework)
build/waftools/     # WAF build system extensions
```

## C Coding Style (Key Rules)

**Formatting:**
- 4 spaces indentation, 78 column width, no tabs
- Opening brace on same line for control flow, next line for functions
- Labels (case, goto) not indented from parent block
- Always use braces for control flow blocks

**Naming:**
- `_t` suffix for types, `_f` for function pointers, `_b` for block types
- Use verbs in function names
- Boolean functions must contain `is` or `has`

**Functions:**
- Return int (0/positive=success, negative=failure) for error handling
- Parameter order: this/context, input, inout, output
- String functions follow snprintf semantics (buffer, size, return written length)

**Constructor/Destructor pattern:**
```c
foo_t *foo_new(void);      // Allocate and init
foo_t *foo_init(foo_t *);  // Init existing memory
void foo_wipe(foo_t *);    // Clean up members
void foo_delete(foo_t **); // Wipe and free
```

**Memory:**
- Use `p_new`, `p_delete`, `p_dup` etc. Never use malloc/free directly
- Use `t_scope` for stack allocations with `t_new_raw`

**Forbidden:** `strncpy`, `strcpy`, `strcat`, `sprintf`, `gets`, `strtok`, `malloc`, `free`, `alloca`

**Comments:** Use `/* */` style, not `//`. Use `{{{ Section name` and `}}}` for vim folds.

**Blocks:**
- Put block code in `.blk` files
- Suffix block type names with `_b`, block variant functions with `_blk`
- Inline blocks in function calls when possible

## IOP (Intersec Object Packer)

IOP is a serialization framework similar to Protocol Buffers with TLV encoding. The IOP compiler (`iopc`) generates C structures from `.iop` files. IOP types use `__t` suffix when modified with `@ctype`.
