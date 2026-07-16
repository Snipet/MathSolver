# Review & verification record

History of the adversarial-review/fix phase. Not part of the library docs;
kept as a project record of what was checked and repaired.

## Verification assets (reusable)

- `tools/run_fuzz.sh [seed] [count]` — round-trip + differential-eval fuzzer.
  Clean at 45k+ expressions across multiple seeds. Re-run after any
  printer/evaluator/parser/factory change.
- `tools/run_acceptance.py --binary <bin> <tsv...>` — runs the acceptance
  TSVs (exact/regex/contains/approx + exit-code checks) against a built
  binary; exits nonzero on any failure.
- `tests/acceptance/cases.tsv` (CLI cases, all subcommands),
  `printer_cases.tsv` (printer torture), `eval_cases.tsv` (evaluator
  reference, high-precision) — all Python-cross-checked.

## Independent fuzz verdicts (clean)

- printer/evaluator round-trip: 45k expressions, 0 failures.
- simplify/expand differential: 40,000 random + 112 directed, 0 idempotence /
  0 round-trip / 0 genuine value-preservation failures (117 flagged diffs all
  double-precision artifacts where the exact fold beats float residue).
- derivative differential: 30,000 random + 24 directed §8-table (24/24 exact),
  0 genuine failures. Caller note: `differentiate` can propagate
  OverflowError/DivisionByZeroError from factory folds during construction
  (74 in 30k) — inherent to the factory contract.

## Fixed (regression-guarded in the suite)

- **Core overflow edge cases** (spurious OverflowError family) — Core Fixes
  stage.
- **expand() performance** (was O(raw·distinct); `(a..j)^7` took 448 s) —
  regrouping made near-linear; now 0.09 s / 11,440 terms. Perf regression
  tests added.
- **simplify() overflow-rollback non-confluence** — `guarded()` no longer
  discards children's simplifications on a fold overflow;
  `simplify(4000000000*(3000000000+ln(e)))` folds `ln(e)` again. Regression
  tested.
- **Solver finding 1** — inverse-function range checks compare exact rational
  constants exactly (`abs(x) = -1e-12` → no real solutions, not phantom
  roots).
- **Solver finding 2** — numeric-identity detection (`sin(2x)=2sinxcosx` no
  longer dumps thousands of grid-point roots).
- **Simplify finding 3** — degree-accumulation overflow (UB) guarded before
  the add; `polynomial_coefficients`/`collect` no longer misclassify.
- **Simplify finding 4** — Pythagorean rule matches every sin²/cos² factor
  (no order-dependent miss).
- **Spec tension 1 (Add tie-break)** — DESIGN §5 corrected: descending
  display-degree, ties by *ascending* `compare_expr` of the whole term
  (matches the shipped printer; the coefficient-stripped proposal was wrong).
  No printer change.
- **Spec tension 2 (latex no-simplify)** — pinned in DESIGN §10.
- **Spec tension 3 (exit-code classification)** — pinned in DESIGN §10
  (usage=2, parse/math=1); battery err-06/err-07 already conformant.
- **Battery pd-05** — spec-gap resolved: DESIGN §5 now specifies only the
  first negative-Number-exponent factor moves below the bar (round-trip
  constraint); printer_cases.tsv expectation corrected to `x*z^(-1)/y`.
- **Battery sol-17** — wrong-expectation: cases.tsv now checks `+ 2*pi*n`
  (DESIGN §9's pinned family) instead of the never-pinned word "periodic".

## CLI/parser findings (in progress this session)

Finding 5 (recursion-depth segfault → clean exit 1), 6 (--range non-finite
bounds), 7 (unbindable eval names), 8 (UTF-8 in lexer errors), 9 (caret
alignment with tabs/newlines), 10 (`--` end-of-options). Being applied +
regression-tested against parser.cpp/main.cpp.

## Open / optional (not blocking)

(none — both former items closed:)

- **Generalized (u^a)^b power rule** — DESIGN §7 amended and implemented:
  fold when `a` is non-integer or odd; for even `a` fold to `u^(ab)` only
  when `ab` is an even integer, to `abs(u)^(ab)` when odd (subsumes
  sqrt-of-square), never otherwise. `(x^6)^(1/3) → x^2` now works; the old
  over-fire `(x^2)^(1/4) → abs(x)^(1/2)` is now correctly a guard.
  Verified by a 4,000-pair differential fuzz (0 failures) + 40k round-trip.
- **Tangency-root note** — numeric roots harvested with no sign change carry
  "tangency-type root: |f| has a near-zero minimum here; no sign change
  observed" (DESIGN §9.4); genuine double roots keep working (noted),
  sign-change roots unaffected. Regression-tested.
