# Proposal: `together` — combine a sum of fractions over a common denominator

Status: **implemented** in v0.5 (ROADMAP P1-1, second half — the companion to
`cancel`). The engine (`src/together.cpp`), the `together` verb (CLI / REPL /
wasm / web console), and the test battery ship as specified below.
Depends on nothing beyond the existing library; changes no existing behavior
of `simplify`, `solve`, `integrate`, or `cancel`.

This document is the contract for the feature, to the DESIGN.md standard.
Section references (§n) are to DESIGN.md unless prefixed.

---

## 1. Summary

Add a public function

```cpp
// simplify.hpp (§7 module)
Expr together(const Expr& e);
Equation together(const Equation& eq);   // apply to each side independently
```

that rewrites an additive expression of fractions as a **single** fraction
over the least common denominator:

```
1/x + 1/y            →  (x + y)/(x*y)
1/x + 1/x^2          →  (x + 1)/x^2
1/(x - 1) + 1/(x + 1)→  2*x/((x - 1)*(x + 1))
a + 1/x              →  (a*x + 1)/x
1/(x - 1) - 1/(x - 1)→  0
```

This is the exact inverse gesture of `apart` (partial fractions) and the
companion of `cancel`: `cancel` reduces one fraction, `together` merges a sum
of them. Unlike `cancel`, `together` needs no polynomial GCD or division — the
common denominator is assembled by taking the maximum power of each distinct
denominator factor — so it is **fully multivariate** and imposes no
single-symbol or rational-coefficient restriction.

`together` ships as an **explicit verb** (CLI subcommand, REPL command, wasm
binding, web console), not a `simplify` rule: forcing a common denominator is
a presentation choice, not a normal form, and the standing §7 conservatism
keeps sums additive (`1/x + 1/y` is already fully simplified).

## 2. Motivation (probed)

ROADMAP §1 lists common-denominator combination among the "first five
minutes" gaps. Verbatim, today (v0.5, `build/mathsolver`):

```
$ mathsolver simplify "1/x + 1/y"          -> 1/x + 1/y
$ mathsolver simplify "1/(x-1) + 1/(x+1)"  -> 1/(x - 1) + 1/(x + 1)
$ mathsolver simplify "a + 1/x"            -> a + 1/x
```

Every peer CAS combines these on request (SymPy `together`, Maple `normal`,
Mathematica `Together`).

## 3. Public contract

```cpp
/// Combine the additive terms of e (after simplify) into a single fraction
/// N/D over their least common denominator. D is the product of each
/// distinct denominator base raised to the maximum multiplicity seen across
/// terms (denominators identified by the sign of integer Number Pow
/// exponents, exactly like the integrator's partial-fractions split and
/// `cancel`). Each term's numerator is scaled by D divided by that term's
/// denominator; the scaled numerators are summed and the result is
/// simplify(N/D). Bases are compared with structurally_equal, so no
/// factoring or GCD is performed and the denominator is left in factored
/// form. Fully multivariate. Value-preserving wherever every original
/// denominator is nonzero (formal, same doctrine as x/x -> 1). Idempotent.
///
/// Returns simplify(e) unchanged when there is nothing to combine — no term
/// has a symbolic denominator (a lone fraction, or a sum with none), or a
/// 64-bit rational OverflowError occurs anywhere in the pipeline (never
/// throws). Purely-numeric denominators are already folded into rational
/// coefficients by simplify, so together deliberately targets *symbolic*
/// denominators (the actual gap): `x/2 + 1/3` stays `x/2 + 1/3`.
Expr together(const Expr& e);
Equation together(const Equation& eq);
```

### 3.2 CLI / REPL / wasm / web

- CLI: `mathsolver together "1/x + 1/y"`, `--latex` supported like every
  transform verb (§10). A no-op input prints the simplified input, exit 0
  (same doctrine as `factor`/`cancel`). Takes no variable argument (the
  operation is multivariate and needs none).
- REPL: `together <expr>`, listed in `help`.
- wasm: `ms_together` wrapping `transform_json` exactly like `ms_cancel`;
  registered as `emscripten::function("together", &ms_together)`.
- web: a `together` console verb (dispatch, reference entry, help line), like
  `cancel`.

## 4. Algorithm

Let `s = simplify(e)`. Wrap the whole pipeline in
`try { … } catch (const OverflowError&) { return s; }` (the §4.7 doctrine of
the cancel proposal).

1. **Terms.** `terms = s->args()` if `s` is an Add, else `{s}`.
2. **Split each term** into a numerator Expr and a list of
   `(base, exponent)` denominator factors, by classifying the term's factors
   exactly as `cancel` §4.1 does: a factor `Pow(b, k)` with `k` a negative
   integer Number contributes denominator factor `(b, -k)`; every other
   factor joins the numerator. The term's numerator is `make_mul(num_factors)`
   (empty → `1`).
3. **LCD.** Accumulate a list of distinct denominator bases (compared with
   `structurally_equal`), each carrying the **maximum** exponent seen for it
   across all terms. If the list is empty (no term has a symbolic
   denominator), return `s`. `D = Π baseᵢ^{max_expᵢ}`.
4. **Scaled numerators.** For each term, the multiplier is
   `Π baseᵢ^{max_expᵢ − term_expᵢ}` over the LCD bases (`term_expᵢ = 0` when
   the term lacks that base); the term contributes `term_numerator ×
   multiplier`. `N = Σ contributions`.
5. **Rebuild.** `return simplify(make_div(N, D))`. `D` is left factored
   (`make_mul`/`make_pow` do not expand a product), so the output denominator
   reads `(x - 1)*(x + 1)`, not `x^2 - 1`.

## 5. Invariants

- **I1 (value preservation, formal).** For every point where all original
  denominators are nonzero, `evaluate(together(e)) = evaluate(e)` exactly.
  At a removed singularity the two differ only in definedness — the §7/§12
  `x/x → 1` doctrine, no new unsoundness.
- **I2 (single fraction).** When it combines, the result is one fraction:
  either a polynomial (denominator folded away) or `Mul(N, Pow(D, -1))`.
- **I3 (idempotence).** `together(together(e))` is `structurally_equal` to
  `together(e)` — a single fraction has one additive term, so the second
  pass finds one denominator and reproduces the same `N/D`.
- **I4 (never throws).** No-op paths return `simplify(e)`; `OverflowError` is
  contained.
- **I5 (no behavioral change elsewhere).** Adds a function and bindings;
  `simplify`/`solve`/`integrate`/`cancel`/parser/printer untouched.

## 6. Worked examples

| input | `together` | why |
|---|---|---|
| `1/x + 1/y` | `(x + y)/(x*y)` | LCD `x*y` |
| `1/x + 1/x^2` | `(x + 1)/x^2` | LCD `x^2`; first term scaled by `x` |
| `1/(x - 1) + 1/(x + 1)` | `2*x/((x - 1)*(x + 1))` | numerators `(x+1)+(x-1)` |
| `a + 1/x` | `(a*x + 1)/x` | non-fraction term scaled onto the LCD |
| `1/(x-1) - 1/(x-1)` | `0` | numerators cancel to 0 |
| `2/x + 3/x` | `5/x` | shared denominator |
| `x/2 + 1/3` | `x/2 + 1/3` | numeric denominators already folded — no symbolic LCD |
| `1/x` | `1/x` | a single fraction — nothing to combine |
| `x + 1` | `x + 1` | no denominators |

## 7. Non-goals

- **Cancelling the combined fraction** (`(x^2-1)/(x-1)`-style reduction):
  that is `cancel`; users compose `cancel(together(e))`.
- **Expanding or factoring `N` or `D`** beyond what `simplify` does.
- **Combining purely-numeric denominators** (already folded by `simplify`).
- **Auto-combination inside `simplify`** — a presentation choice, not a
  normal form.

## 8. Testing (§11 conventions)

`tests/test_together.cpp`: every §6 row (structural, via factories +
`structurally_equal`); a differential value-preservation property at sample
points where the original is defined; idempotence over the corpus; the
Equation overload; a multivariate case; a combined-then-cancel composition
(`cancel(together(1/(x-1)+1/(x+1)))` unchanged, no common factor); and an
overflow-bail case. Acceptance `tog-01…` rows in `tests/acceptance/cases.tsv`
(cancelling, no-op, and the numeric-denominator no-op). One CLI end-to-end
case and one `tools/wasm_smoke.mjs` check. The round-trip fuzzer must stay
clean unchanged (no new printer shapes; `simplify` untouched).

## 9. Effort

**S–M.** No new numerics — assembly over the existing factories and the
cancel/integrator split. Risk low: every path is either a clean combination
or an explicit no-op/bail.
