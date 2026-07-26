# exception-spec-divergence

**Default:** on · **Groups:** — · **Diagnostics:** `ExceptionSpec_Divergent`

Flags functions whose declaration sites disagree on whether the function
can throw: one header says `noexcept`, another (or the defining `.cpp`'s
own forward declaration) says nothing — or a single
`noexcept(SOME_MACRO)` declaration resolves differently under different
compile flags.

## Why no other tool catches this

All declarations of a function must have compatible exception
specifications (`[except.spec]`). **Within one TU the compiler enforces
this as a hard error** — which is exactly why every finding of this check
is real: a contradiction can only survive across TUs, through headers no
single TU includes together. And the linker can't help either, because
`noexcept` does not participate in name mangling for ordinary functions:
the contradictory program compiles and links clean. IFNDR.

## Example — the shim header

```cpp
// sdk.h (vendor)
int sdk_init();

// sdk_shim.hpp (added later, "it doesn't throw, and noexcept helps the optimizer")
int sdk_init() noexcept;
```

Half the TUs include the vendor header, half the shim:

```
sdk.h:2: Exception-spec divergence: 'sdk_init' is declared noexcept at
    shim.hpp:2 but potentially-throwing at sdk.h:2. No TU sees both declarations
    (the in-TU case is a compile error), so this contradiction is IFNDR: callers
    that saw noexcept elide unwind paths, and noexcept-dependent decisions
    (move_if_noexcept) fork per TU. Unify the declarations.
```

What actually goes wrong, in escalating order: callers that saw
`noexcept` compile without unwind/cleanup paths around the call (skipped
destructors or unwinder crashes if it ever throws); containers pick
copy-vs-move via `noexcept(T(T&&))` at instantiation time, so a
`vector<T>::resize` instantiated under each belief has a *different
body* — a vague-linkage ODR split the linker resolves arbitrarily; and
where `std::terminate` fires becomes build-dependent.

## The macro variant

Specs are compared **resolved** (`FunctionProtoType::canThrow`), not as
source text. So this is also caught, at its single site:

```cpp
// tune.hpp — FAST_PATH set per target in the build system
void tune() noexcept(FAST_PATH);
```

```
tune.hpp:2: Exception-spec divergence: 'tune' at tune.hpp:2 resolves to noexcept
    in some TUs and potentially-throwing in others — its specification depends
    on preprocessor state that differs between compile commands. ...
```

(This is the same disease `default-arg-divergence` structurally cannot
see for defaults — resolved-value comparison makes it visible here.)

## What is deliberately NOT flagged

- Different **overloads** with different specs (grouping is by
  spec-stripped signature, so `g(int) noexcept` vs `g(double)` never
  meet).
- **Internal-linkage** functions (per-TU entities; no cross-TU
  contradiction is possible).
- Declarations whose spec is **dependent or not yet computed**
  (templates, unevaluated implicit specs).
- System-header declarations.

## Related checks

[exception-escape](exception-escape.md) asks "can a throw *reach* a
noexcept frame?"; this check asks the prior question — "do the TUs even
*agree* which frames are noexcept?"

## Remediation

Keep the exception specification identical at every declaration site —
in practice: declare the function in exactly one header and include it
everywhere, including in the defining `.cpp`.
