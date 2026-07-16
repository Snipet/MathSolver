# Review-phase inputs (accumulated during the build)

Working notes for the final adversarial-review/verification phase. Not part
of the library docs.

## Ready-to-run verification assets

- `tools/run_fuzz.sh [seed] [count]` — round-trip + differential-eval fuzzer.
  FULL mode ran clean against the implemented printer/evaluator on
  2026-07-15: seeds 42/5000, 7/20000, 987654321/20000 — 45k expressions,
  0 failures. Re-run after any printer/evaluator/parser/factory change.
- `tests/acceptance/cases.tsv` — 120 CLI cases (all subcommands), math
  cross-checked in Python (129/129).
- `tests/acceptance/printer_cases.tsv` — 69 printer torture cases
  (division rendering, e^x precedence, parenthesization, \cdot digit rule).
- `tests/acceptance/eval_cases.tsv` — evaluator reference corpus with
  high-precision expected values (note: bindings like x=0.1 are exact 1/10).
- `tests/acceptance/README.md` — TSV format + tolerance conventions.
  A harness for these TSVs still needs to be written/wired (CLI stage or
  review phase).

## Confirmed fix items for the review/fix phase

1. **expand() performance (robustness, confirmed by measurement).**
   `distribute()` (src/simplify.cpp:681) materializes all q^n cross-terms
   before any like-term collection, and `apply_add_rules`
   (src/simplify.cpp:392) groups terms by linear scan with
   `structurally_equal` — O(raw·distinct) deep compares. Measured:
   `(x+y)^20` 3.3 s; `(a+..+j)^6` 19 s; `(a+..+j)^7` 448 s / 11,440 terms;
   one fuzz case ~15 min at 2.3 GB. The `kMaxExpandExponent = 512` cap
   (src/simplify.cpp:20) is not an effective resource guard. Fix: group via
   `hash_expr` buckets (near-linear) and/or collect like terms during
   distribution to bound memory; add perf regression tests
   ((x+y)^20 well under 1 s; (a+..+j)^7 in seconds, correct term count 11440).
2. **simplify() overflow-rollback stuck states (MANDATORY, confirmed by
   execution — found by the derivative differential fuzz 2026-07-15).**
   `guarded()` (src/simplify.cpp:52-60, used at :653-670) rolls back the
   ENTIRE node — discarding the children's completed simplifications —
   whenever an exact numeric fold of the rebuilt node throws OverflowError,
   identically every pass. Result: sticky, path-dependent stuck states where
   explicit §7 rules never fire; simplify stays idempotent and value-correct
   but non-confluent. One-line reproducer (verified):
   `simplify(4000000000*(3000000000+ln(e)))` returns its input VERBATIM —
   `ln(e)` is never folded to 1 because 4e9 × 3000000001 overflows int64
   downstream — while `3*(3000000000+ln(e))` folds fine to 9000000003.
   Fix: on rebuild overflow, fall back to a node rebuilt from the
   already-simplified children with the offending numeric fold suppressed,
   rather than reverting to the pre-pass subtree. Add the reproducer (assert
   the `ln(e)` inside is folded) plus a differing-association variant as
   regression tests.
3. Missed simplification (spec-consistent, optional): `(x^6)^(1/3)` stays
   unchanged; value-preserving `x^2` exists. §7's parity rule doesn't cover
   even-p/odd-q — consider extending rule + spec together if done at all.

## Independent verification verdicts (clean)

- simplify/expand differential fuzz: 40,000 random + 112 directed cases,
  0 idempotence / 0 round-trip / 0 genuine value-preservation failures;
  all 117 flagged diffs triaged to double-precision artifacts (exact folds
  beating floating-point residue). No OverflowError escapes in 40k inputs.
- derivative differential fuzz: 30,000 random cases + 24-case directed §8
  table (24/24 exact) — 0 numeric-agreement failures after triage, 0
  round-trip failures, 0 soft-contract (simplify(d)==d) failures, 0 genuine
  linearity failures. The single linearity flag traced to the simplify
  overflow-rollback defect (fix item 2 above), not to differentiate().
  Caller note: differentiate can propagate OverflowError/DivisionByZeroError
  from factory folds during construction (74 in 30k) — document in §8.

## Open spec tensions to resolve in review

1. DESIGN §5 Add display tie-break ("reverse compare_expr") contradicts the
   §5 worked example `x + (-2)*y -> x - 2*y` (tie at degree 1; reverse kind
   rank puts the Mul first, i.e. `-2*y + x`). printer_cases.tsv pins the
   worked example (pm-02) and tolerates either order elsewhere (po-06/07).
   Proposed fix: tie-break by compare_expr of the *coefficient-stripped*
   term, ascending (x vs y -> x first), matching the example. Align DESIGN +
   printer implementation + tests.
2. Whether the `latex` subcommand simplifies before printing is not pinned
   by DESIGN §10 (printer_cases inputs are simplify-fixpoints, so its cases
   don't depend on this — but pin it and document).
3. Two battery exit-code cases (err-06 missing --range HI, err-07 malformed
   binding x=abc) assume usage-error exit 2 — reconcile with the CLI
   implementation.

## Already fixed (regression-guarded in the suite; no review action)

- Four core overflow edge cases (spurious OverflowError family) — fixed in
  the Core Fixes stage with regression tests.
