---
name: is-coding-rules-python
description: Python coding rules for the Intersec codebase (uv, ruff, pyrefly, typing, IOP stubs). Load before writing or reviewing Python code, `wscript` or `wscript_build` files.
---

# Intersec Python Coding Rules

The reference for the environment and the IOP stubs is
`README-python3.adoc` at the root of lib-common. This skill keeps the
rules that you apply while you write code.

## Environment: uv

`uv` manages the Python environment. `pyproject.toml` lists the
packages, `uv.lock` pins them, and the environment lives in `.venv/`.

- Never install a package with `pip`. If you need a new package, add it
  to `pyproject.toml` with a comment that tells why. Prefer `~=` to
  `==`. Then run `uv lock` and `uv sync`, and commit `uv.lock` with the
  change.
- Put a package that only development uses in `[dependency-groups]`,
  not in `dependencies`.
- `waf` runs itself in the uv environment. To run a tool directly, put
  the `.venv/bin` of the tree first on `PATH`, or use `uv run <tool>`.
  The system `ruff` or `pyrefly` can have a different version.
- uv never downloads a Python interpreter. `requires-python` in
  `pyproject.toml` sets the supported versions.

## Python version

The code must run on the oldest version of `requires-python`
(Python 3.9 today).

- Use the builtin generics: `list[int]`, `dict[str, int]`.
- Write `X | None` only in annotations, with
  `from __future__ import annotations` at the top of the module. At
  runtime (`cast()`, `TypeAlias`, `isinstance()`), use `Optional` and
  `Union`.
- Do not use `match`: it starts at Python 3.10.
- Import `override`, `Self`, `Unpack`, `assert_type` and `ParamSpec`
  from `typing_extensions`.

**Carve-out:** some waf tools load during the waf bootstrap and must
stay Python 3.6 compatible. Such a file says so in its header. Keep its
`typing.List` style.

## Lint and format: ruff

The configuration is in the `[tool.ruff]` section of `pyproject.toml`.
It selects all the rules, then removes a list of them. A project that
uses lib-common as a sub-module extends this configuration.

- Use single quotes and lines of 78 characters at most.
- Before you deliver, run `waf ruff <files>` and
  `ruff format --check <files>`. `waf ruff-fix <files>` and
  `ruff format <files>` apply the fixes.
- If you must silence a rule, write `# noqa: <code>` on the line, never
  a bare `# noqa`. Fix the code first: a suppression is the last resort.

## Type checking: pyrefly

The configuration is in the `[tool.pyrefly]` section of
`pyproject.toml`. It is strict: each parameter and each return value
has an annotation, and an untyped import is an error.

- Run `waf pyrefly <files>`, not `pyrefly check <files>`. The waf
  command loads the right configuration and the excluded paths. In a
  project that uses lib-common as a sub-module, a direct
  `pyrefly check` reports false errors such as
  `Channel is not assignable to Channel`.
- If you must silence an error, write
  `# pyrefly: ignore[<code>]` on the line. A bare ignore hides each
  error of the line. An ignore that matches no error is an error
  (`unused-ignore`).
- Do not add untyped third-party code to the `search-path`. Each use
  of it becomes an error. Put the module in `ignore-missing-imports`
  instead.

## Avoid `Any`

`Any` turns off the type check for each value that it touches, and for
each value that comes from it. Use it only when no other type is
possible. Before you write `Any`, try these types in this order:

1. The exact type. For an IOP object, use the IOP stub type (see the
   next section).
2. A `TypedDict` for a dict with known keys, a `Protocol` for an object
   with known methods, a `TypeVar` for a function that returns its
   input type.
3. `object` for a value that the code only passes on, prints or
   compares. Unlike `Any`, `object` forbids each operation that the
   code did not check with `isinstance()`.

**Litmus:** if you can name one attribute or one method that the code
uses on the value, the type is not `Any`.

**Carve-outs:** `Any` is correct at the edge of an untyped library, and
in `**kwargs: Any` that the code forwards as is. Keep the `Any` at that
edge: convert the value to a real type, or `cast()` it, before it goes
further.

## IOP types: the Python stubs

IOPy creates the IOP classes at runtime, when it loads a DSO. pyrefly
cannot see them. IOPc generates one stub file (`<package>__iop.pyi`)
for each IOP package, and one for each IOP DSO plugin
(`<dso>__iop.pyi`). Use these stubs to type each IOP value.

### Generate the stubs

The stubs are build artifacts and git ignores them. On a new checkout,
or after a change to an `.iop` file, generate them again. If you do
not, pyrefly reports `missing-import` on each `*__iop` import.

To generate them, run `waf iop-pystubs` at the root of the project. It
builds only the IOP DSOs that have a `pystub_path=`, and writes each
stub in that directory.

### Use the stubs

The stubs exist only for the type checker. Import them under
`TYPE_CHECKING`, and write each stub type as a string (a forward
reference):

```python
from typing import TYPE_CHECKING, cast

import iopy

if TYPE_CHECKING:
    import test__iop
    import test_iop_plugin__iop


def load_plugin(path: str) -> 'test_iop_plugin__iop.Plugin':
    return cast('test_iop_plugin__iop.Plugin', iopy.Plugin(path))


def get_field1(obj: 'test__iop.ClassA') -> int:
    return obj.field1
```

- `iopy.Plugin()` and `connect()` return an untyped object. `cast()`
  the result to `<dso>__iop.Plugin` or `<dso>__iop.Channel` once, where
  you create it.
- Use `<pkg>__iop.Struct` for an IOP object, `Struct_DictType` for its
  dict form, and `Struct_ParamType` for each value that the constructor
  accepts. `README-python3.adoc` lists the types for the interfaces,
  the RPCs and the modules.
- An optional IOP field has a type as if it is always present. Before
  you read it, check it with `hasattr()`.
- If a dict literal for an IOP class gives an unclear `no-matching-overload`
  error, assign the dict to a variable typed `'<pkg>__iop.X_DictType'`
  first. pyrefly then names the wrong key.
