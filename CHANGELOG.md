# Changelog

Notable user-facing changes per release. Contracts live in DESIGN.md; the
per-feature specs are under docs/proposals/.

## Unreleased (v0.5)

### Added

- **Complex domain, Phase 3** (docs/proposals/complex-domain.md): the complex
  accessor functions `conj`, `Re`, `Im`, `arg` (`Re`/`Im` capitalized so
  lowercase `r*e`/`i*m` products are unaffected). `simplify` folds numeric
  arguments — `conj(2+3i) → 2-3i`, `Re(2+3i) → 2`, `abs(3+4i) → 5` — while a
  symbolic argument stays unevaluated. Both evaluators compute them (`eval
  "arg(i)" → 1.5708`); LaTeX renders `conj` as `\overline{·}`.
- **Complex domain, Phase 2** (docs/proposals/complex-domain.md): a complex
  numeric evaluator (`evaluate_complex`) running alongside the real one, which
  is unchanged. The CLI `eval` verb evaluates any expression containing `i`
  over ℂ and prints `a + b*i`, chopping rounding dust — `eval "e^(i*pi)" → -1`,
  `eval "(2+3i)*(1-i)" → 5 + i`, `eval "1/(1+i)" → 0.5 - 0.5*i`, `eval
  "abs(3+4i)" → 5`. Principal branch for the multivalued functions.
- **Complex domain, Phase 1** (docs/proposals/complex-domain.md): `simplify`
  now folds exact complex (Gaussian-rational) constants to canonical `a + b·i`,
  including rationalized denominators — `1/(1+i) → 1/2 - i/2`, `(3+i)/(1-i) →
  1 + 2i`, `(1+i)*(1-i) → 2`, `1/(3+4i) → 3/25 - 4i/25`. Confluent with
  `expand`; scoped to expressions containing `i`, so real arithmetic and
  symbolic complex (`x + i`, `1/(x+i)`) are untouched. First slice of the
  flagship ℂ project (see docs/proposals/next-features.md).
- **Variable assignment** (docs/proposals/variable-assignment.md): a session
  environment of `name := value` bindings in the REPL and the web app.
  - Values are expressions or equations (`E_1 := x + y = 3`), stored as
    parsed ASTs and resolved **lazily** at each use; redefinition updates
    dependents; cycles are rejected at definition time with the offending
    path.
  - The environment applies to every computing verb; `solve`/`diff`/
    `integrate`/`collect` exclude their designated variable with a warning,
    `eval`/`subs` explicit bindings shadow with a note, and `latex`/`debug`
    never resolve. A bare equation resolves first, then solves for the single
    remaining symbol or evaluates the truth (exact folds say
    identity/contradiction; inexact numeric comparisons answer with an
    explicit `≈` caveat instead of a certainty).
  - REPL management commands: `vars`, `unset <name>` (accepts the displayed
    spelling, e.g. `unset x_{max}`), `clear`.
  - Web: a Variables panel (add/edit/delete/clear, live validation, KaTeX
    previews, inactive rows for invalid/cyclic values), indicator chips under
    the input, a "computed from" resolved-input line on result cards, and
    `localStorage` persistence (versioned schema, 32-row cap enforced at
    write time).
  - The engine stays pure: no `src/`/`include/` changes; resolution composes
    the existing `substitute()`/`subs` machinery. `ms_subs` (wasm) gains a
    `simplifyResult` flag and equation support to serve the web resolver.

## 0.4.0 — "Forgiving Input"

- Parser: unicode input, scientific notation, `|x|` bars, and the
  multi-letter word guard; simplify upgrades; new `subs` and `collect` verbs
  (CLI, REPL, wasm).

## 0.3.0

- Web workbench (Svelte 5 + wasm worker), linear-system solve, plotting.

## Earlier

- 0.1–0.2: core CAS engine (parse/print/simplify/diff/integrate/solve), CLI
  and REPL.
