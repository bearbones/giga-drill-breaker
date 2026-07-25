# header-static-duplication

**Default:** on · **Groups:** — · **Diagnostics:** `HeaderStatic_Duplicated`

Flags **mutable** internal-linkage (`static` / anonymous-namespace)
variables defined in a header that two or more translation units have
each materialized — every including TU owns a private copy, so writes in
one TU are invisible in the others and the variable's address differs per
TU.

## Why no other tool catches this

A single-TU tool can see the *pattern* ("static variable in a header")
but not the *duplication*: whether any other TU actually includes that
header. Pattern-level warnings drown in the benign case — `static const`
tables and tags in headers are idiomatic and harmless — so they get
disabled. anneal records the defining site together with the TU that
materialized it, and reports only when:

- the variable is **mutable** (const/constexpr copies are identical and
  immutable per TU — silent), and
- **two or more distinct TUs** are proven to own a copy (listed in the
  diagnostic).

The classic symptom this heads off: a counter, cache, or registration
flag in a header that "works in the unit test" (one TU) and silently
forks in production (many TUs), with each TU seeing only its own
mutations.

## Example

```cpp
// state.hpp
static int gCounter = 0;                         // flagged when >= 2 TUs include this
inline int bump() { return ++gCounter; }         // each TU bumps ITS OWN counter
```

```
state.hpp:2: Header-static duplication: 'gCounter' is defined static in
    state.hpp:2, and 2 TUs (a.cpp, b.cpp) each materialized their OWN copy —
    writes in one TU are invisible in the others, and its address differs per
    TU. Use 'inline' (C++17) for one shared object, extern with a single
    definition, or constexpr if it is meant to be a constant.
```

Note the `bump()` combination: an inline function mutating a header
static is *also* an ODR violation (each TU's `bump` has a different body
referencing a different variable) — [odr-violations](odr-violations.md)
sees that flavor, this check names the root cause.

## What is deliberately NOT flagged

- `static const` / `constexpr` in headers — identical immutable copies,
  the common intended usage.
- **Pointer-to-const** (`static const char *kName = "..."`): the pointer
  itself is technically mutable, but the idiom is overwhelmingly used as
  a constant — flagging it would drown the signal. A genuinely reassigned
  const-pointee pointer is therefore missed (documented recall
  tradeoff).
- Header statics included by only **one** TU — no duplication realized
  (the diagnostic would be speculative).
- Statics in the TU's **main file** — the normal, intended meaning of
  `static`.
- `inline` variables (C++17) and `extern` declarations — external
  linkage, one program-wide copy.

## Remediation

`inline int gCounter = 0;` (C++17) for one shared mutable object;
`extern` in the header plus a single `.cpp` definition pre-C++17; or
`constexpr` if it was meant to be a constant all along.
