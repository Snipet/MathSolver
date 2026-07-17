# Proposal: `cancel` — rational-expression cancellation via univariate polynomial GCD

Status: proposed (ROADMAP P1-1, first half — `together` is a separate proposal).
Target: v0.5. Depends on nothing beyond the existing library; changes no
existing behavior of `simplify`, `solve`, or `integrate`.

This document is written to the DESIGN.md standard: it is the contract for the
feature. An implementer should be able to build `cancel` from this document
alone. Section references (§n) are to DESIGN.md unless prefixed otherwise.

---

## 1. Summary

Add a public function

```cpp
// simplify.hpp (§7 module)
Expr cancel(const Expr& e);
Equation cancel(const Equation& eq);   // apply to each side independently
```

that removes the common polynomial factor of the numerator and denominator of
a rational expression, exactly, over 64-bit exact rationals:

```
(x^2 - 1)/(x - 1)        →  x + 1
(x^3 - 1)/(x - 1)        →  x^2 + x + 1
(x^2 + 2x + 1)/(x + 1)   →  x + 1
(x^2 - 1)/(x^2 - 3x + 2) →  (x + 1)/(x - 2)
```

The algorithm is the Euclidean algorithm on coefficient vectors obtained from
`polynomial_coefficients` (§7), with content/primitive-part normalization to
control coefficient growth, and the standing 64-bit overflow doctrine (§3,
§12): any `OverflowError` anywhere inside → return the input unchanged
(simplified), never throw, never wrap.

`cancel` ships as an **explicit verb** (CLI subcommand, REPL command, wasm
binding) — deliberately NOT as a `simplify` rule in v1. Section 7 argues why.

## 2. Motivation and current behavior (probed)

ROADMAP §1 classifies rational-expression cancellation as MISSING and §2 lists
it among the "first five minutes" failures vs SymPy/Symbolab ("Asks to cancel
`(x^2-1)/(x-1)` — nothing happens"). Verbatim probes against
`build/mathsolver` (v0.3.0, 2026-07-16):

```
$ mathsolver simplify "(x^2 - 1)/(x - 1)"
(x^2 - 1)/(x - 1)
$ mathsolver simplify "(x^3 - 1)/(x - 1)"
(x^3 - 1)/(x - 1)
$ mathsolver simplify "(x^2 + 2x + 1)/(x + 1)"
(x^2 + 2*x + 1)/(x + 1)
$ mathsolver simplify "(x^2 - 1)/(x^2 - 3x + 2)"
(x^2 - 1)/(x^2 - 3*x + 2)
$ mathsolver simplify "(x^2 - 1)/(x - 1)^2"
(x^2 - 1)/(x - 1)^2
$ mathsolver simplify "2/(2x - 2)"
2/(2*x - 2)
$ mathsolver cancel "(x^2-1)/(x-1)"
usage error: unknown command 'cancel' (run 'mathsolver --help' for usage)
```

What already works today — and defines the doctrine we extend:

```
$ mathsolver simplify "(x - 1)/(x - 1)"
1
$ mathsolver simplify "((x-1)*(x+1))/(x-1)"
x + 1
```

The §7 like-factor rule already cancels **structurally equal** factors
(`x/x → 1`, "formal; assumes x ≠ 0", documented in §12). `cancel` extends the
same formal-cancellation doctrine from structural equality to **algebraic**
common factors that only become visible after expansion and GCD. Nothing about
the domain caveat is new — only the reach.

## 3. Public contract

### 3.1 Library API

```cpp
/// Best-effort rational-expression cancellation. Splits e (after simplify)
/// into numerator N and denominator D by the sign of integer Number Pow
/// exponents; if both are univariate polynomials in the same single symbol
/// with rational Number coefficients (degree <= kMaxCancelDegree each),
/// divides both by their polynomial GCD (content included) and returns the
/// normalized quotient N'/D'. In every other case — no denominator,
/// non-polynomial parts, symbolic coefficients, more than one symbol,
/// degree over the cap, or 64-bit rational overflow anywhere — returns
/// simplify(e) unchanged. Never throws OverflowError; value-preserving at
/// every point where the original denominator is nonzero (formal
/// cancellation, same doctrine as x/x -> 1). Idempotent.
Expr cancel(const Expr& e);

/// Cancel each side independently.
Equation cancel(const Equation& eq);
```

Error behavior: `cancel` itself throws nothing on any input reachable from the
parser. `OverflowError` is caught internally (bail-to-unchanged, §3.7 below).
`DivisionByZeroError` is unreachable by construction (all divisors are
verified-nonzero leading coefficients); if it or any other `Error` escapes,
that is an implementation bug, not contract behavior.

### 3.2 CLI

```
mathsolver cancel "(x^2 - 1)/(x - 1)" [x]      # variable optional when unique
```

Plain-style output by default, `--latex` supported like every other transform
verb (§10). The optional variable argument follows the `diff`/`collect`
convention: optional when the input has exactly one free symbol; in v1 the
engine only ever cancels single-symbol inputs (§8), so the argument is
accepted, validated (naming a symbol not free in the input is a usage error,
exit 2), and forward-compatible with the multivariate v2. No-op inputs print
the simplified input and exit 0 — "nothing cancels" is an answer, not an
error (same doctrine as `factor`, §7, and `integrate`'s honest `Unsolved`,
§8b).

Help text: add one usage line `mathsolver cancel "(x^2-1)/(x-1)"` to the §10
block in `apps/main.cpp` and DESIGN §10.

### 3.3 REPL

`cancel <expr>` (comma-separated optional variable: `cancel (x^2-1)/(x-1), x`),
listed in `help`. Same output as the CLI.

### 3.4 WASM / web

`wasm/bindings.cpp`: `ms_cancel` wrapping `transform_json` exactly like
`ms_simplify`/`ms_factor` (returns the standard `{ok, plain, latex}` envelope —
`TransformResult` in `web/src/lib/engine/types.ts` needs **no change**);
register as `emscripten::function("cancel", &ms_cancel)`. Web UI: optional in
this proposal — a "Cancel" example chip or tab entry in
`web/src/lib/tabs.ts` can follow; the binding itself is required so the smoke
gate covers it.

## 4. Algorithm specification

All arithmetic below is exact `Rational` (§3) and therefore overflow-checked.
Read §4.7 first if in doubt: any `OverflowError` at any step aborts the whole
attempt and returns `simplify(e)`.

### 4.1 Numerator/denominator split

Let `s = simplify(e)`. Classify the factors of `s` (if `s` is a Mul, its args;
otherwise `s` itself is the single factor) exactly as the integrator's
partial-fractions stage already does (`try_partial_fractions`,
src/integrate.cpp — "Split e into numerator/denominator by the sign of integer
Pow exponents"):

- a factor `Pow(b, k)` with `k` a **negative integer** Number contributes
  `Pow(b, -k)` to the denominator factor list;
- every other factor (including `Pow` with non-integer negative exponent,
  e.g. `(x-1)^(-1/2)` — that is not a polynomial denominator) goes to the
  numerator list.

`N = make_mul(num_factors)` (empty list → `1`), `D = make_mul(den_factors)`.
If the denominator list is empty, or `D` contains no symbol, return `s`
(nothing to cancel; a pure-number denominator is already a folded rational
coefficient by the §2 factories). Note this split correctly handles repeated
factors: `(x^2-1)/(x-1)^2` yields `D = (x-1)^2`, whose expanded coefficient
vector is `x^2 - 2x + 1`.

### 4.2 Variable selection and the v1 coefficient restriction

Let `V = free_symbols(N) ∪ free_symbols(D)`. **v1 requires |V| = 1**; call the
symbol `x`. Extract coefficient vectors with `polynomial_coefficients(N, x)`
and `polynomial_coefficients(D, x)` (§7: expand first; index = degree; `c[n]`
nonzero; zero polynomial = `{0}`), then require every coefficient to be a
`Number` node — the `rational_poly_coeffs` pattern already used at
src/integrate.cpp:577. Any failure (non-polynomial shape such as `sin(x)/x`,
fractional powers, symbolic coefficient) → return `s` unchanged.

Why single-symbol only, and why rational coefficients only, is §8 — the
short version: Euclidean division over a coefficient *field* of symbolic
expressions requires deciding whether a symbolic leading coefficient is zero,
and "simplify-to-zero" is not a decision procedure here. v1 refuses rather
than risking a silently wrong quotient — the `Unsolved`-style honest no-op,
the same shape of honesty as §8b's integrator.

### 4.3 Content and primitive part

For a nonzero coefficient vector `P = [c_0 … c_n]` over `Rational`:

- `cont(P)` (a positive rational) = `gcd(|num(c_0)|, …, |num(c_n)|) /
  lcm(den(c_0), …, den(c_n))`, computed with overflow-checked `long long`
  gcd/lcm (`lcm` via checked multiply — see `find_rational_root`'s
  `checked_mul_ll` usage, src/integrate.cpp:610).
- `pp(P) = P / cont(P)`, then negated if its leading coefficient is negative.
  Invariant of `pp`: integer coefficients, collective gcd 1, positive leading
  coefficient.

`gcd_Q(a/b, c/d) = gcd(a, c) / lcm(b, d)` extends gcd to positive rationals
(both arguments in lowest terms; the result of cancelling by it keeps both
contents integral and coprime).

Primitive parts are what keep the Euclidean remainders' coefficients from
exploding: every remainder is re-normalized to its primitive part before the
next division step (§4.4), so growth is bounded by the true size of the
subresultants rather than the naive fraction blow-up. With the 64-bit ceiling
this does not *prevent* overflow — genuine coefficient growth can still
exceed 64 bits — it makes overflow rare on human-scale inputs; when it does
happen, §4.7 applies.

### 4.4 Euclidean GCD

Inputs: `a = pp(N_coeffs)`, `b = pp(D_coeffs)`, with `deg a`, `deg b` each
`≤ kMaxCancelDegree` (§4.6). Ensure `deg a ≥ deg b` (swap otherwise). Loop:

```
while b is not the zero polynomial:
    (_, r) = poly_divide(a, b)      # exact rational long division; the
                                    # quotient is discarded, only r is used
    a = b
    b = (r == [0]) ? zero : pp(r)   # primitive-part normalization each step
g_pp = pp(a)
```

`poly_divide` is the exact long division already implemented (twice) in the
codebase: src/integrate.cpp:659 (`num = quot*den + rem, deg rem < deg den`)
and the deflation path in src/solver.cpp — reuse the integrate.cpp shape. The
loop terminates because `deg r < deg b` strictly at every step; at most
`kMaxCancelDegree` iterations.

Full gcd including content:

```
g = gcd_Q(cont(N_coeffs), cont(D_coeffs)) · g_pp
```

If `deg g = 0` **and** `g = 1`: nothing cancels — return `s` unchanged. (A
constant `g ≠ 1` is a genuine content cancellation and proceeds: see the
`2/(2x-2)` and `(2x+2)/(4x-2)` examples in §6.)

### 4.5 Quotients, normalization, verification, rebuild

- `N' = poly_divide(N_coeffs, g).quot`, `D' = poly_divide(D_coeffs, g).quot`.
- **Mandatory internal verification** (the §8b self-verification doctrine
  applied here, exactly and cheaply): both remainders must be exactly the
  zero polynomial, and — belt and braces — the exact products `N'·g` and
  `D'·g` must equal the input vectors coefficient-wise. Any mismatch is an
  internal bug: return `s` unchanged, never publish an unverified
  cancellation.
- Sign normalization: if `lead(D')` is negative, negate every coefficient of
  both `N'` and `D'`. (Combined with §4.3–4.4 this fixes a unique normal
  form: `pp(N')` and `pp(D')` are coprime, their contents are coprime as
  rationals, and `lead(D') > 0` — which is what makes `cancel` idempotent,
  invariant I3.)
- Rebuild Exprs with the `poly_from_coeffs` pattern (src/integrate.cpp:738):
  `Σ make_mul(make_num(c_k), make_pow(x, k))` through the §2 factories.
  Result:
  - `deg D' = 0`: `result = make_mul(make_num(1/D'_0), N'_expr)` — a plain
    polynomial (this is the `(x^2-1)/(x-1) → x+1` case; `D'_0 > 0` by the
    sign rule, and `D'_0` may be a non-unit rational, e.g. `(6x^2-6)/(4x-4)
    → (3x+3)/2`).
  - otherwise: `result = make_mul(N'_expr, make_pow(D'_expr, -1))`.
- Return `simplify(result)` (re-simplification keeps the §7 fixpoint contract
  and cannot reintroduce the factor — no §7 rule multiplies numerator and
  denominator by a common polynomial).

### 4.6 Caps

```cpp
constexpr std::size_t kMaxCancelDegree = 32;   // per side, after expansion
```

- Degree cap: if `deg N > 32` or `deg D > 32` after coefficient extraction →
  return `s` unchanged. Rationale: the Euclid loop is O(deg²) rational ops
  and utterly cheap at 32, but the *expansion* feeding
  `polynomial_coefficients` and the 64-bit coefficient ceiling both make
  higher degrees overflow-dominated noise; 32 comfortably covers every
  textbook and engineering input (the integrator caps its rational-function
  stage at denominator degree 6 for comparison — cancellation is much
  cheaper than factoring, hence the higher cap).
- No separate size cap is needed: `polynomial_coefficients` already bounds
  the shape (finite non-negative integer powers), and expansion overflow
  surfaces as `OverflowError` → §4.7.

### 4.7 Overflow policy (the 64-bit doctrine)

Per §3, every `Rational` operation is overflow-checked and throws
`OverflowError`; per §12 the arithmetic "throws, never wraps". `cancel` wraps
its entire pipeline — split, expansion inside `polynomial_coefficients`,
content computation, Euclid, division, verification, rebuild — in one
`try { … } catch (const OverflowError&) { return s; }`. This is the same
containment contract as §9b's `solve_system` ("an OverflowError never escapes
solve_system") and the same bail-to-unchanged shape as §7's radical rule ("on
64-bit overflow of `b^m` the node is left unchanged"). The user sees their
input back, unmodified and correct — never a wrapped or partial answer.

## 5. Invariants

- **I1 (value preservation, formal).** For every real `t` with `D(t) ≠ 0`
  (the *original* denominator), `evaluate(cancel(e), {x=t}) =
  evaluate(e, {x=t})` exactly. At roots of the cancelled factor `g` the two
  sides differ in *definedness* only: `e` is undefined there, `cancel(e)`
  may be defined. This is precisely the §7/§12 formal-cancellation doctrine
  (`x/x → 1` "assumes x ≠ 0") — no new class of unsoundness is introduced,
  and §12 gets one added sentence documenting that `cancel` inherits it.
- **I2 (canonical form).** The result is an ordinary §2 canonical Expr built
  through the factories; the §5 printer round-trip invariant holds with no
  printer change (the output shapes — polynomial, and
  `Mul(poly, Pow(poly, -1))` — are shapes the round-trip fuzzer already
  exercises today).
- **I3 (idempotence).** `cancel(cancel(e))` is `structurally_equal` to
  `cancel(e)`. Proof sketch: after one successful pass, `pp(N')` and
  `pp(D')` are coprime (Euclid), the contents are coprime rationals
  (divided by their `gcd_Q`), and the sign normal form is fixed — so the
  second pass computes `g = 1` and no-ops. Tested property, same as §7's
  simplify idempotence.
- **I4 (never throws, never guesses).** All no-op paths return
  `simplify(e)`; `OverflowError` is contained (§4.7); an unverifiable
  quotient is never published (§4.5).
- **I5 (no behavioral change elsewhere).** v1 adds a function and three
  bindings; `simplify`, `solve`, `integrate`, the parser, and the printer are
  byte-for-byte untouched. Every existing test and fuzz verdict must remain
  green *without re-baselining*.

## 6. Worked examples

Expected `mathsolver cancel "<input>"` output (Plain style; "today" columns
are verbatim v0.3.0 `simplify` probes from §2).

Cancelling:

| input | today (simplify) | `cancel` | why |
|---|---|---|---|
| `(x^2 - 1)/(x - 1)` | unchanged | `x + 1` | g = x−1 |
| `(x^3 - 1)/(x - 1)` | unchanged | `x^2 + x + 1` | rem(x³−1, x−1) = 0 → g = x−1 |
| `(x^2 + 2x + 1)/(x + 1)` | unchanged | `x + 1` | g = x+1 (double root; one factor cancels) |
| `(x^2 - 1)/(x^2 - 3x + 2)` | unchanged | `(x + 1)/(x - 2)` | two-step Euclid, see below |
| `(x^2 - 1)/(x - 1)^2` | unchanged | `(x + 1)/(x - 1)` | §4.1 split expands the squared factor |
| `2/(2x - 2)` | unchanged | `1/(x - 1)` | content-only: g = 2 (g_pp = 1) |
| `(2x + 2)/(4x - 2)` | unchanged | `(x + 1)/(2*x - 1)` | content gcd 2; SymPy `cancel` normalizes identically |
| `(6x^2 - 6)/(4x - 4)` | unchanged | `(3*x + 3)/2` | g = 2·(x−1); deg D' = 0 with non-unit constant |

The two-step Euclid, worked: `a = x² − 1`, `b = x² − 3x + 2`.
`r₁ = a − b = 3x − 3`, `pp(r₁) = x − 1`; `rem(x² − 3x + 2, x − 1) = 0`
(evaluate at 1) → `g_pp = x − 1`. Contents are 1 and 1 → `g = x − 1`.
`N' = x + 1`, `D' = x − 2` → `(x + 1)/(x - 2)`. The `3x − 3 → x − 1`
step is the primitive-part normalization of §4.3 doing its job.

Non-cancelling guards (must return the simplified input unchanged, exit 0):

| input | `cancel` | why |
|---|---|---|
| `(x^2 - 1)/(x - 2)` | `(x^2 - 1)/(x - 2)` | rem = 3 ≠ 0 → g = 1 |
| `(x^2 + 1)/(x + 1)` | `(x^2 + 1)/(x + 1)` | rem = 2 ≠ 0 → g = 1 |
| `sin(x)/x` | `sin(x)/x` | numerator not polynomial |
| `x^2 + 3x` | `x^2 + 3*x` | no denominator |
| `1/(x - 1)^(1/2)` | `1/sqrt(x - 1)` | exponent −1/2 not an integer: not a polynomial denominator |

Symbolic-coefficient / multivariate refusals (v1 — see §8):

| input | `cancel` | why refused |
|---|---|---|
| `(a*x^2 - a)/(x - 1)` | `(a*x^2 - a)/(x - 1)` | coefficient `a` is not a Number (mathematically a·(x+1) — v2 material) |
| `(x*y + x*z)/x` | `(x*y + x*z)/x` | two+ symbols; coefficients of x are symbolic |
| `(x^2 - y^2)/(x - y)` | `(x^2 - y^2)/(x - y)` | multivariate (v2 material) |

Note the factored forms of the last group already cancel **today** through
§7's structural like-factor rule — probed: `((x-1)*(x+1))/(x-1)` → `x + 1` —
so users holding a factored form are already served; `cancel` v1 adds the
expanded-form, single-variable, rational-coefficient class, which is the
overwhelmingly common textbook case.

## 7. Why a verb first, and not a `simplify` rule

ROADMAP P1-1 already takes this position ("Ship as an explicit command
**not** inside `simplify` (domain change; §7 conservatism), exactly like
SymPy separates `cancel` from `simplify`"). This section makes the argument
concrete enough to survive future re-litigation.

1. **Domain change at scale.** §7's rule inventory is "safe — value-preserving
   on the reals *except formal cancellations*", and §12 confines the
   documented exception to the structurally obvious case (`x/x → 1`). A GCD
   cancellation silently erases removable singularities the user *wrote
   down*: `(x - 1)/(x - 1)` is undefined at `x = 1`; so is
   `(x^2 - 1)/(x - 1)`. Making that the default behavior of `simplify` —
   which runs inside every pipeline — changes what every downstream consumer
   sees.
2. **Concrete downstream hazard, probed today.**
   ```
   $ mathsolver solve "(x^2 - 1)/(x - 1) = 2"
   x ≈ 1.00000000000001
   method: numeric (Newton/bisection)
   ```
   The residual `(x²-1)/(x-1) - 2` equals `x - 1` away from 1, so the numeric
   fallback converges to the one point where the original equation is
   *undefined*. Today that answer at least arrives as an approximate,
   numeric-method result. If `simplify` auto-cancelled, the §9 polynomial
   path would return **exact** `x = 1` — and §9.5's verification would
   evaluate the residual precisely at the singular point, hit
   DivisionByZeroError/EvalError, and per the drop policy *keep* the
   spurious root with only a soft "may be valid only under domain
   conditions" warning. Auto-cancel upgrades a fuzzy numeric artifact into a
   confident exact wrong answer. Similarly, §8b's definite-integral FTC path
   grid-checks the *integrand* for domain gaps; cancelling inside `simplify`
   hides the gap at `x = 1` before the check runs.
3. **Fixpoint cost and confluence.** §7 rules run bottom-up to a bounded
   fixpoint; a rule requiring `expand` + coefficient extraction + Euclid on
   every Mul-with-negative-power node at every pass is a different cost
   class, and its interaction with the like-factor rule and (pending P0-5)
   radical normal form would need a fresh confluence/idempotence argument.
4. **Precedent.** SymPy keeps `cancel` a separate function; its `simplify`
   only calls it as part of a heuristic, opt-in-by-calling-simplify bundle —
   and SymPy's simplify is explicitly *not* contractually conservative,
   whereas ours is (§7).

**Later phase (explicitly out of this proposal):** revisiting auto-cancel as
a `simplify` post-pass may be reasonable once (a) an assumptions mechanism
(ROADMAP P1-6) can record "denominator ≠ 0" caveats, or (b) solve/integrate
learn to consult the pre-simplify form for domain checks. Either way it is a
DESIGN §7 amendment with its own fuzz campaign, not a follow-up commit.

## 8. Multivariate scope and the coefficient-field problem (v1 honesty)

The natural generalization treats one **main variable** `x` and regards all
other symbols as opaque coefficients, running the same Euclid over the
coefficient field `F = Q(a, b, …)`. The blocker is fundamental, not
incidental: Euclidean division repeatedly divides by the divisor's leading
coefficient and must decide **whether intermediate coefficients are zero** —
a wrong "nonzero" verdict makes `poly_divide` produce a garbage quotient
*silently* (no exception, no verification failure at the coefficient level;
only the §4.5 product check would catch some cases, and only when the zero
test failed in one direction). Deciding zero for elements of `Q(a, b, …)`
requires a canonical form for multivariate rational functions — which is
exactly the machinery this feature is supposed to introduce, i.e. a
bootstrapping problem. `simplify`-to-zero is a *semi*-decision at best: §7's
rule set is deliberately conservative and routinely leaves zero-valued
expressions structurally nonzero (probed: `simplify` leaves
`sin(2x) - 2*sin(x)*cos(x)` alone, per ROADMAP §1).

v1 therefore restricts to coefficients that are `Number` nodes — where
zero-testing is exact and free — and returns the input unchanged otherwise
(the `Unsolved`-style honest no-op; same doctrine as §8b stage 6:
"symbolic-parameter coefficients → Unsolved"). The refusal is silent by
design at the library level; the CLI simply prints the input back. v2
directions, for the record, in ascending ambition: (a) symbolic coefficients
with zero-testing delegated to `simplify` + numeric sampling *plus* the §4.5
exact product verification promoted to a hard gate (reject on any
non-identically-zero residual, checked symbolically); (b) content extraction
of monomials in the non-main variables first (would catch
`(x*y + x*z)/x`); (c) true multivariate GCD (subresultant PRS) — L-effort,
needs its own proposal.

## 9. Non-goals

- **`together` / common-denominator combination** (`1/x + 1/y → (x+y)/(x*y)`):
  the other half of ROADMAP P1-1, separate proposal, separate verb.
- **Auto-cancellation inside `simplify`** — §7 of this document; a future
  DESIGN amendment at most.
- **`partfrac`, poly GCD/div/degree as user verbs** (ROADMAP P1-4 and the
  "poly GCD as verbs" tail of P1-1): this proposal builds the machinery but
  exposes only `cancel`.
- **Multivariate or symbolic-coefficient cancellation** — §8.
- **Factoring improvements** (ROADMAP P1-2): `cancel` needs no factorization,
  only GCD.
- **Big rationals** (ROADMAP P2-4): the 64-bit bail-to-unchanged doctrine
  stands; `cancel` must not become the reason to break it.
- **Simplifying the cancelled form further than `simplify` does** (no
  re-factoring of `N'`/`D'`; users can compose `factor(cancel(e))`).

## 10. Implementation notes

New/changed files (one new source file keeps the heavily-in-flux
`simplify.cpp` untouched and mirrors how `integrate` owns its own helpers):

| File | Change |
|---|---|
| `include/mathsolver/simplify.hpp` | declare `cancel(Expr)`, `cancel(Equation)` (§3.1 doc comments) |
| `src/cancel.cpp` (NEW) | the §4 pipeline |
| `CMakeLists.txt` | add `src/cancel.cpp` to `mathsolver_core` |
| `apps/main.cpp` | `cancel` subcommand + REPL command + help text |
| `wasm/bindings.cpp` | `ms_cancel` + registration |
| `tests/test_cancel.cpp` (NEW) + `tests/CMakeLists.txt` | unit/property tests (§11) |
| `tests/acceptance/cases.tsv` | `can-XX` rows (§11) |
| `tools/wasm_smoke.mjs` | one `cancel` check |
| `DESIGN.md` | §1 module-map row (or file note under simplify), §7 `cancel` contract paragraph, §10 usage line, §12 one sentence extending the formal-cancellation note |
| `docs/GRAMMAR.md` | none (no grammar change) |

Prior art to lift (currently duplicated between src/solver.cpp:711-806 and
src/integrate.cpp:577-682): `to_rational_coeffs`/`rational_poly_coeffs`,
`poly_divide`, `poly_from_coeffs`, `checked_mul_ll`. v1 may follow the
existing precedent of file-local copies in `src/cancel.cpp` (~80 lines);
consolidating all three copies into an internal shared header
(`src/poly_detail.hpp`, not a public API — no DESIGN module) is a
recommended but optional refactor, best done as its own commit so the
integrator's and solver's test coverage vouches for the move.

## 11. Testing and verification

Gating assets (the standing gate per ROADMAP §3 preamble and
REVIEW-NOTES.md — all must be green before and after):

1. **Unit tests** (`tests/test_cancel.cpp`, Catch2 v3, §11 conventions —
   factories + `structurally_equal`, `INFO(debug_string(e))`): every row of
   the §6 tables, both directions (cancelling and guard rows), the Equation
   overload, deg-cap refusal (e.g. `(x^33 + …)/(x - 1)` shaped input), and
   an overflow-bail case (coefficients near 2^62 so Euclid's cross-products
   overflow → output structurally equal to `simplify(input)`).
2. **Differential value-preservation (the core property).** For each test
   expression and ~7 sample points `t ∈ {-2.7, -1.3, -0.51, 0.37, 1.9, 3.1,
   4.25}`: skip points where `|evaluate(D, t)|` < 1e-6·scale (near cancelled
   or genuine roots of the original denominator — I1 promises nothing
   there); elsewhere require `evaluate(cancel(e), t)` ≈ `evaluate(e, t)` to
   1e-9 relative. This is the §8b/§9.5 numeric-verification doctrine applied
   to a transform verb.
3. **Idempotence property** (I3): `cancel(cancel(e))` structurally equal to
   `cancel(e)` over the whole unit corpus, mirroring the §7 simplify
   idempotence property test.
4. **Constructive fuzz** (new, cheap, lives beside the unit tests or as a
   `tools/` harness like `fuzz_roundtrip.cpp`): generate random primitive
   polynomials `A, B, G` (degrees ≤ 5, integer coefficients in [-9, 9],
   `B, G` nonzero); build `e = expand(A·G) / expand(B·G)`; require
   `cancel(e)` to be value-equal to `A/B` at the rule-2 sample points (not
   structural equality — `gcd(A,B)` may be nontrivial too) and idempotent.
   OverflowError during generation → skip the case. 10k cases in the same
   spirit as the standing 40k differential fuzz verdicts in REVIEW-NOTES.md.
5. **Round-trip fuzzer** (`tools/run_fuzz.sh`, round-trip + differential-eval,
   clean at 45k+ per ROADMAP): must stay clean unchanged — `cancel` adds no
   printer shapes and does not touch `simplify`, so any new failure here is
   a regression alarm, not an expected re-baseline.
6. **Acceptance battery** (`tools/run_acceptance.py` +
   `tests/acceptance/cases.tsv`): new `can-01…can-1x` rows covering §6 —
   e.g. `can-01 cancel (x^2 - 1)/(x - 1) - exact x + 1 0`, guard rows
   asserting exact-unchanged output with exit 0, a usage-error row
   (`cancel` with a wrong variable name, expects_exit 2).
7. **Browser/WASM suite** (`tools/wasm_smoke.mjs`, `tools/wasm_acceptance.mjs`):
   one smoke check (`ms.cancel("(x^2-1)/(x-1)")` → `ok && plain === "x + 1"`);
   the acceptance runner picks up the TSV rows for the wasm build where
   applicable.
8. **CLI end-to-end** (`tests/test_cli.cpp` per §10's end-to-end convention):
   one `cancel` invocation each for CLI success, no-op, and usage error.

## 12. Effort estimate

**M — 2 to 4 agent-days** (ROADMAP P1-1 says M; this is the cancel-only half).

| Piece | Effort |
|---|---|
| §4 engine (`src/cancel.cpp`, reusing/lifting `poly_divide` etc.) | 1 day |
| Verbs: CLI + REPL + wasm + smoke | 0.5 day |
| Tests: unit + properties + constructive fuzz + acceptance rows | 1 day |
| DESIGN/docs amendments, gate runs (fuzz 45k, acceptance, wasm) | 0.5 day |
| Buffer: normal-form edge cases (signs, contents, deg-0 denominators) | 0.5–1 day |

Risk: **low-medium** (ROADMAP concurs). The algorithm is classical and every
sharp edge — coefficient growth, symbolic coefficients, domain semantics —
is fenced by an explicit refuse-or-bail rule rather than best-effort cleverness.
