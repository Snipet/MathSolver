# Proposal: Complex Numbers as a First-Class Domain (ℂ)

Status: **in progress** — Phase 1 shipped (this document tracks the whole
project). Size: **L–XL**. Flagship of the commercial-parity push
(docs/proposals/next-features.md, T2). Written to DESIGN.md's contract
standard: normative sections fold into DESIGN.md as each phase lands.

Goal: make ℂ a real domain rather than a solver special-case — exact complex
arithmetic (`(2+3i)/(1-i) → 1 + 2i`), complex evaluation (`eval "e^(i*pi)" →
-1`), the complex functions (`conj`, `Re`, `Im`, `arg`, `abs`), Euler's
formula, and all-degree complex roots. This is the single biggest CAS-parity
item and a force-multiplier for the plugins (AC impedance/phasors in `sys`,
complex frequency response in `dsp`, full Fourier/Laplace, residues).

---

## 1. Current state (probed 2026-07, `build-local/mathsolver`, v0.5.0)

The imaginary unit is **already a first-class constant** — not a symbol:

- `include/mathsolver/expr.hpp`: `enum class ConstantId { Pi, E, I };`
- `src/parser.cpp`: `i` lexes to `NameKind::Imaginary` → `push_constant(ConstantId::I)`.
- `src/simplify.cpp` (`apply_pow_rules`): integer powers of `i` cycle mod 4.
- `src/solver.cpp`: quadratics with negative discriminant report exact complex
  roots (`Status::SolvedComplex`, `PolyResult::Kind::ComplexRoots`).

What did **not** work before Phase 1:

```
simplify "1/(1+i)"        => 1/(i + 1)     # denominator not rationalized
simplify "(3+i)/(1-i)"    => (3+i)*(1+i)/2 # left half-folded
eval     "(2+3i)*(1-i)"   => error: 'i' … cannot be evaluated numerically
```

`expand` already distributed complex products correctly (`expand "(2+3i)*(1-i)"
→ i + 5`) because it distributes and then the `i^2` rule + Add collection fire;
`simplify`, which does not distribute, could not reach the same normal form
through a division.

---

## 2. Doctrine

DESIGN.md §6/§12 states a **real-domain** doctrine that the evaluator, the
solver's root verification (§9.5), and the integrator's differentiate-back
self-check all lean on. Complex support must not weaken those. The strategy:

- **Exact symbolic ℂ is additive and safe.** Folding complex *constants* to
  `a + b·i` is ordinary numeric constant-folding (like `2*3 → 6`); it changes
  no real-domain value and no verification path. Phase 1 lives entirely here.
- **Numeric ℂ evaluation is a *separate* entry point.** The real `evaluate`
  stays real-only (it still errors on `i`); a parallel `evaluate_complex`
  returns `std::complex<double>`. Downstream verification keeps calling the
  real one, so §9.5 is untouched. (Phase 2.)
- **The fuzzers are the gate.** Every phase must keep `ctest` green and
  `tools/run_fuzz.sh` clean, and preserve simplify idempotence. Because the
  new folding is *scoped to nodes containing `i`* and real expressions never
  contain `i` (it is not a symbol), the entire real path is provably untouched.

Non-goal (unchanged): `i` is the imaginary unit, never a variable. There is no
way to use `i` as an ordinary symbol; this is long-standing and intentional.

---

## 3. Phasing

### Phase 1 — exact complex normal form in `simplify` ✅ (this commit)

Gaussian-rational folding: any node built only from Numbers and `i` collapses
to canonical `a + b·i`, **including rationalized denominators**.

- `src/simplify.cpp`: `struct Gaussian`, `as_gaussian(Expr)` (exact
  interpretation, nullopt when any leaf is a symbol / `pi` / `e` / function /
  non-integer power), `gaussian_pow` (square-and-multiply, so `i^1000000` does
  not spin), `gaussian_inverse` (conjugate over modulus²), `gaussian_to_expr`
  (rebuilds through the same factories `expand` uses → one normal form), and
  `fold_complex`, hooked at the head of `apply_rules`.
- Scope guard: `fold_complex` fires only when the node contains `ConstantId::I`
  and `as_gaussian` succeeds, so real arithmetic and symbolic complex
  (`x + i`, `1/(x+i)`) are untouched. Overflow / zero-denominator → unfolded
  (DESIGN.md §3).

Now:

```
simplify "1/(1+i)"     => 1/2 - i/2
simplify "(3+i)/(1-i)" => 1 + 2*i
simplify "(1+i)*(1-i)" => 2
simplify "(1+i)^2"     => 2*i
simplify "1/(3+4i)"    => 3/25 - 4*i/25
```

Confluent with `expand` (both reach the same `a + b·i` via the same
factories); idempotent; tested in `tests/test_simplify.cpp` (folding, scope
guards, idempotence).

### Phase 2 — complex numeric evaluation ✅ (core + CLI)

`evaluate_complex(Expr, ComplexBindings) -> std::complex<double>`
(`src/evaluator.cpp`, `include/mathsolver/evaluator.hpp`) runs alongside the
real `evaluate`, which is untouched — so the solver/integrator verification
paths keep their real-domain guarantees. Elementary, hyperbolic, and inverse
functions use the `std::complex` overloads (principal branch); `abs` is the
modulus; the special functions with no complex form (gamma, digamma, erf/erfc,
fib, harmonic) fall back to the real impl when the argument is numerically
real and otherwise throw. `e^w` is evaluated as `exp(w)` for accuracy, so
Euler's formula comes out clean.

The CLI `eval` verb (`apps/main.cpp`) routes to it whenever the expression
contains `i` (detected via the new `contains_constant`), and prints `a + b*i`
with a chop that snaps rounding dust so `e^(i*pi) -> -1`:

```
eval "(2+3i)*(1-i)"  => 5 + i
eval "e^(i*pi)"      => -1
eval "1/(1+i)"       => 0.5 - 0.5*i
eval "abs(3+4i)"     => 5
eval "e^(i*pi/2)"    => i
```

Boundary (by design): an expression with no `i` stays on the real path, so
`eval "sqrt(-1)"` still reports the real-domain error rather than returning
`i`. Lifting that waits on Phase 3 (`sqrt`/`ln` of a real negative under an
explicit complex mode). Tests in `tests/test_evaluator.cpp` and
`tests/test_cli.cpp`.

**Remaining in Phase 2 (follow-up):** the wasm binding + web Evaluate tab
(the `EvaluateResult` shape currently carries only a real `value`).

### Phase 3 — complex functions as first-class ✅

New `FunctionId`s `Conj`, `Re`, `Im`, `Arg`:

- Parser: `conj`, `arg`, and the **capitalized** `Re`/`Im` are registered
  known names (`src/parser.cpp`) — capitalized so the lowercase products `r*e`
  and `i*m` keep their meaning (no existing input reparses).
- Printer: Plain `conj/Re/Im/arg`; LaTeX `\overline{z}`, `\operatorname{Re}`,
  `\operatorname{Im}`, `\arg`.
- Evaluators: real (`conj`/`Re` identity, `Im` = 0, `arg` ∈ {0, π}) and complex
  (`std::conj`/`real`/`imag`/`arg`).
- `simplify` folds numeric-complex arguments via the Phase 1 Gaussian machinery:
  `conj(2+3i) → 2-3i`, `Re/Im` extract, `abs(3+4i) → 5` (modulus, normalized by
  the radical rule). A **symbolic** argument stays unevaluated — a symbol's
  realness is not assumed (`conj(x)`, `Re(x+i)` are left as-is).
- Derivative: non-analytic → honest error (no wrong answer).
- Solver: not invertible → `IsoStatus::Fail`.

Tests in `tests/test_simplify.cpp` and `tests/test_evaluator.cpp`.

### Phase 4 — Euler's formula & transcendental simplification

`e^(i·θ) → cos θ + i·sin θ`, `e^(iπ) → -1`, `cos θ + i·sin θ → e^(iθ)` as an
explicit `expand`/`combine`-style verb (not inside `simplify`, per the §7
fixpoint doctrine). **M.**

### Phase 5 — all-degree complex roots

Extend `solve` beyond the quadratic special-case: report complex roots for
cubics/quartics and for the numeric fallback (Durand–Kerner / companion-matrix
eigenvalues over ℂ), gated by root-substitution verification. **L.**

### Phase 6 — plugin uplift

Once Phases 2–3 land: AC impedance/phasor helpers in `sys`, complex frequency
response in `dsp`, residues for `apart`/Laplace. **M, per plugin.**

---

## 4. Amendment list (folds into DESIGN.md as phases land)

- §7 rule inventory: add the Gaussian-fold rule (Phase 1, done).
- §6/§12: document the parallel complex evaluator and that the real path and
  its verification are unchanged (Phase 2).
- §2: new `FunctionId`s for `conj`/`Re`/`Im`/`arg` (Phase 3).
