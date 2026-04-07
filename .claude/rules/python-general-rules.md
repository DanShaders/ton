---
paths:
  - "**/*.py"
---

# General Python Coding Rules

## Typing

Strict typing rules enforced by basedpyright (`uv run basedpyright`). You MUST maintain zero warnings and zero errors:

- **No `object` as type annotation** unless you truly need a top type. Use proper union types or `ABC` with `@abstractmethod`.
- **No `# type: ignore`, `# pyright: ignore`, `cast()`, or `typing.Any`** unless absolutely necessary. The only exception is working around wrong/incomplete type stubs in third-party libraries we cannot change. All other typing suppressions MUST be explicitly approved by the user. If you are stuck trying to placate basedpyright, look around the codebase for a similar pattern that is already typed.
- **`match` statements** for exhaustive isinstance dispatches (3+ branches). Two-branch if/else is fine.
- **`@override` on every method that overrides a parent.** basedpyright enforces `reportImplicitOverride`.
- **`@final` on concrete classes** that won't be subclassed. This removes the necessity to declare types of `self` parameters in constructors.
- **`@abstractmethod` on ABC methods**, not `raise NotImplementedError`.
- **`frozen=True` on immutable dataclasses.** Use `@functools.cached_property` for computed properties on frozen dataclasses.
- **Mixins must be `Protocol`s**, not plain classes. basedpyright requires structural typing for mixin patterns.
- **`eq=False` on mutable dataclasses** that should compare by identity.
- **No implicit string concatenation without explicit parentheses.** Use `(f"..." f"...")` not `f"..." + f"..."`. basedpyright requires the inner parens.
- **Assign `_ =` to unused return values** (e.g., `_ = builder.store_uint(...)`).
- **No `from __future__ import annotations`** -- not needed in Python 3.14+. PEP 695 `type X = Y | Z` for type aliases.
- **`Callable[[Args], Ret]`** from `collections.abc`.
- **Prefix unused parameters with `_`** (e.g., `_idx`, `_ti`) to avoid `reportUnusedParameter`. It is not required to do this for methods marked with `@abstractmethod`.
- **Function return types are rarely needed.** basedpyright can infer them correctly in almost all cases. Only include them if it is necessary to upcast a specific type to a protocol or a base class for a public-facing API or if they bring clarity.

## Workflow

- **Run `uv run basedpyright` continuously** -- after every substantial change, check the affected files. Typing requires constant attention, not just a final check at the end.
- **Before finishing** (i. e. after fixing a bug or implementing a requested feature), make sure `uv run ruff format` and `uv run ruff check` are clean.
