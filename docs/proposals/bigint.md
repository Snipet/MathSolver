# Proposal: Arbitrary-Precision Integers (remove the int64 ceiling)

Status: **in progress** — Phase 1 landed the `BigInt` type; **Phase 2 (this
PR)** makes the combinatorial sequence verbs arbitrary precision; Phase 3
(Rational-on-BigInt, the symbolic tower) follows. Size: **L**. Written to
DESIGN.md's contract standard: normative sections fold into DESIGN.md as each
phase lands.

Goal: make the exact-integer/rational tower **arbitrary precision**, so
`factorial 100`, `catalan 200`, `bernoulli 40`, `pell 200`, big binomials, and
exact polynomial arithmetic over large coefficients all work instead of
throwing `OverflowError` at the 64-bit boundary. Every exact result already
crosses the WASM/web boundary as a decimal *string* (`plain`/`latex`), so once
the core produces big values they render unchanged — the ceiling is purely an
internal representation limit.

---

## 1. Current state (probed 2026-07, `origin/dev`)

Exact numbers funnel through one type:

- `include/mathsolver/rational.hpp` — `Rational` holds two `long long` fields
  (`num_`, `den_`), invariants `den() > 0`, `gcd(|num|, den) == 1`.
- `src/rational.cpp` — every op (`+ - * /`, `pow`, `<=>`, ctor normalization,
  `from_decimal_string`) is overflow-checked with `__int128` intermediates and
  throws `OverflowError` rather than wrapping.
- ~20 number verbs (`factorial`, `catalan`, `bell`, `bernoulli`, `pell`,
  `tribonacci`, `lucas`, `motzkin`, `euler`, `primorial`, `derangement`,
  `stirling2`, `partitions`, …) each carry a hand-computed int64 boundary and
  throw past it.
- `.num()`/`.den()` are consumed at **91 sites across 19 files** — mostly gcd /
  parity / small-modulus reductions, denominator-clearing when integerizing
  polynomial coefficients, and reading small **symbolic exponents**.

## 2. Design

### Phase 1 — `BigInt` (this PR)

A hand-rolled signed big integer, in the project's exact/exhaustively-tested
style (no third-party dependency — must build clean under gcc-14 C++23 **and**
emscripten):

- **Representation**: sign-magnitude. `bool neg_` (true only when the magnitude
  is nonzero) + `std::vector<uint32_t> mag_`, base 2³², little-endian, no
  trailing zero limbs. Zero is `{neg_=false, mag_={}}`.
- **API**: construction from `long long` (implicit) and decimal string; `+ - *`
  (schoolbook), truncated `/` and `%` plus `divmod` (Knuth Algorithm D with
  limb normalization); comparison (`==`, `<=>`); `abs`, unary `-`, `gcd`,
  `pow(exp)`; `to_string` (decimal), `to_double`, `to_ll`/`fits_ll` (checked),
  `is_zero`/`is_negative`/`sign`; a `std::hash<BigInt>` specialization (for the
  Expr number-node hash in Phase 2).
- **No overflow**: operations grow the magnitude; they never throw
  `OverflowError`. Division/modulo by zero throw `DivisionByZeroError`.
- **Verification**: `tests/test_bigint.cpp` cross-checks `+ - * / % <=> gcd`
  against `__int128` over exhaustive edge values and a large randomized sweep
  (deterministic PRNG), plus known big values (factorials, `2^k`, `10^100`,
  round-trip `to_string`/parse).

### Phase 2 — `Rational` on `BigInt`

`Rational` swaps its two `long long` fields for `BigInt`. `num()`/`den()` return
`const BigInt&`. Arithmetic drops the `__int128` overflow dance (BigInt grows),
so the operators get *simpler*. The 91 consumer sites migrate: comparisons work
through operators; gcd/mod use `BigInt::gcd`/`%`; the handful that need a native
`int` (symbolic exponents, small moduli) use `.to_ll()` with a guard where a
genuinely-huge value would be meaningless (`x^(10^100)` is not a real request).
`OverflowError` is removed from the rational layer.

### Phase 3 — lift the verb ceilings

The number verbs stop range-checking against `INT64_MAX` and return `BigInt`
values; the CLI/WASM/web string envelopes carry them unchanged. Tests and
`wasm_smoke` flip from "errors past the boundary" to "exact well beyond it"
(e.g. `factorial 50`, `catalan 100`). Docs updated.

## 3. Non-goals

- No change to floating/numeric (`double`) paths — only the exact tower.
- No arbitrary-precision *floats* (bigfloat) in this arc.
- No performance tuning beyond schoolbook algorithms; our operands are small
  enough (a few hundred limbs) that O(n²) is ample.
