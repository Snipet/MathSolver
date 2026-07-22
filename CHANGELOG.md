# Changelog

Notable user-facing changes per release. Contracts live in DESIGN.md; the
per-feature specs are under docs/proposals/.

## Unreleased (v0.5)

### Added

- **Wave system, Phase 2 — instrumentation** (docs/proposals/wave-system.md):
  the wave field can now be *measured*. A **probe** tool drops receivers on the
  field; each records `u(t)` and shows a live **FFT magnitude spectrum** (a
  self-contained radix-2 transform with linear detrend + Hann window) with a
  peak-frequency / period readout — a driven source's probe recovers its drive
  frequency exactly. A **Wave ⇄ Intensity** view toggle paints the
  time-averaged intensity `⟨u²⟩`, freezing diffraction and interference
  fringes into a quantitative heatmap. New sim/spectrum tests cover the ring
  buffer, the running-mean intensity, FFT round-trip, and end-to-end
  drive-frequency recovery. (Also fixes a Phase 1 wart: placing a probe — or
  otherwise a non-drag pointer-up — no longer clears a scene's driven source.)
- **Wave system, Phase 1 — structured media** (docs/proposals/wave-system.md):
  the interactive wave field gains a heterogeneous medium (per-cell wave speed
  `c(x,y)`) and solid reflecting obstacles, plus a **Scene** preset menu —
  double-slit and single-slit diffraction, a converging lens, a waveguide, and
  refraction at an interface — each self-demonstrating with a driven source.
  Slower regions are shaded and refract/shorten wavelengths; obstacles render
  as walls. The color scale is now a robust high-percentile of |u|, so a driven
  source's near-field no longer crushes the downstream pattern. Every scene
  stays unconditionally stable (slowness `cScale ∈ (0,1]`). New sim tests cover
  wall decoupling, variable-media stability, and the scene lifecycle.
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
