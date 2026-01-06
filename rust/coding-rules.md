# Rust Coding Rules

This document defines the coding standards that **must** be followed in all Rust
code of this repository.

## Documentation

Code must be properly documented. This includes not only public APIs, but also
private items, especially when the logic is non-trivial.

## Clippy Compliance

All code **must** pass clippy without warnings. Run `cargo clippy --tests`
to verify.

## Formatting

Code formatting must be delegated to rustfmt. Run `cargo fmt` to format the
code.

## Folding

Vim folding markers must be used to organize code into sections. Use `// {{{`
and `// }}}` markers instead of decorative separators.

**Do not** use this style:
```rust
// ============================================================================
// Code section
// ============================================================================

code...
```

**Do** use this style:
```rust
// {{{ Code section

code...

// }}}
```

There should be no empty lines between consecutive folds:
```rust
// {{{ Code section 1

code...

// }}}
// {{{ Code section 2

code...

// }}}
```

Nested folds may be used when appropriate:
```rust
// {{{ Code section 1
// {{{ Code subsection 1

code...

// }}}
// {{{ Code subsection 2

code...

// }}}
// }}}
```

Except for very small files, all code should be inside folds. The following
elements must remain **outside** of folds:
- Copyright headers
- Module-level documentation comments (`//!`)
- Module declarations (`mod`)
- `use` statements
