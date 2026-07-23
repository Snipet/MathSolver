# Changelog

Notable user-facing changes per release. Contracts live in DESIGN.md; the
per-feature specs are under docs/proposals/.

## Unreleased (v0.6)

### Fixed

- **Grapher — no more vertical lines through asymptotes.** A curve like `tan(x)`, `1/x`, or `sec(x)` no longer draws a spurious near-vertical connector straight through each pole: the sampler now breaks the polyline where two consecutive samples straddle zero, both sit well off-screen, and the jump dwarfs the visible span — the pen lifts at the asymptote instead. Applies to `y=f(x)` and `x=f(y)`.

### Added

- **Grapher — editable data tables with regression.** A new **＋ Add table**
  button drops an editable `(x, y)` grid into the expression list: type points
  (the grid auto-grows a blank row) and they plot as a scatter in the row's
  color. A **Fit** selector overlays a regression curve — **linear /
  quadratic / cubic** (solved *exactly* over the rationals by the CAS `fit`
  verb, so `(0,1) (1,2) (2,2) (3,4)` fits to `x²/4 + 3x/20 + 23/20`, not a
  rounded decimal) plus **exponential / power / logarithmic** numeric models —
  and shows the fitted equation with its R² and an "exact" badge. Tables
  persist to localStorage and ride along in share links.
- **Grapher — extrema, intersections & asymptote markers.** The CAS-powered
  points-of-interest layer (exact zeros + y-intercept) gains three more, all
  computed by the symbolic engine: **local extrema** — critical points from
  `solve(f'=0)`, classified max/min and labelled with the exact coordinate;
  **curve–curve intersections** — `solve(f−g=0)` across each pair of `y=f(x)`
  rows, marked in a neutral colour since a crossing belongs to both curves;
  and **horizontal asymptotes** — the finite limits at `x→±∞` (via `limit`,
  exact where the value is like `pi/2`), drawn as dashed lines. Every marker
  refreshes as you pan/zoom, hovering reveals its exact coordinate, and the
  whole layer is best-effort (the curve still draws if a solve/limit can't
  close a form). Bounds keep it responsive: extrema capped per curve,
  intersections computed only for ≤ 6 function rows.
- **Grapher — Taylor-series overlay operator.** The graph expression list now
  understands `series(f, center, order)` (and its alias `taylor(...)`) as an
  inline operator alongside the existing `diff(...)` / `integral(...)`: a row
  like `series(sin(x), 0, 5)` plots the degree-5 Taylor polynomial of `sin x`
  about 0, so you can drop the source curve on one row and its truncated
  expansion on the next and watch them agree near the center and peel apart
  away from it. `center` defaults to `0` and `order` to `6`, clamped to
  `[1, 12]`; the polynomial is computed by the CAS `series` verb, so it is
  exact, and free-symbol analysis (auto-sliders, classification) sees through
  the call to the source expression just as it does for `diff`/`integral`.
- **Grapher — tangent / normal line operators.** Two more inline operators:
  `tangent(f, a)` plots the tangent line to `f` at `x = a`, and `normal(f, a)`
  plots the perpendicular. Both are built exactly from the CAS —
  `tangent(sin(x), pi/3)` becomes `(x − π/3)/2 + √3/2`, not a rounded decimal —
  by taking `f(a)` and the slope `f′(a)` symbolically via `subs` on the
  `derivative`. `a` defaults to `0`. Where the curve is flat the normal line is
  vertical (undefined slope) and the row reports that rather than drawing a
  bogus line.
- **Grapher — exact points of interest.** A `y=f(x)` curve now shows hollow
  markers at its **zeros / x-intercepts** and its **y-intercept**, computed by
  the CAS rather than eyeballed: `solve` finds the roots over the visible
  window and hovering a marker reveals the *exact* coordinate — `(√2, 0)`,
  `(π/6, 0)` — where a numeric grapher can only show a rounded decimal. Zeros
  refresh as you pan/zoom; the curve still draws if the solve can't close a
  form (markers are best-effort). First slice of the CAS-powered
  points-of-interest suite (extrema, intersections, and asymptotes to follow).
- **Console cookbook.** The console side panel gains a **Cookbook** tab beside
  the flat **Commands** reference: a set of curated, worked recipes — grouped
  by topic (getting started, algebra, equations, calculus, series & discrete,
  ODEs & transforms, complex numbers, variables & notebooks, plotting & vector
  calculus, and the DSP / control / linear-algebra / PDE plugins) — that show
  how to *combine* commands into real workflows, not just what each verb does.
  Every recipe carries a short rationale and one or more numbered console
  lines; clicking any line drops it into the prompt. Available on desktop (the
  console sidebar) and mobile (the collapsible "Commands & Cookbook" panel).

### Changed

- **Version bumped to 0.6.0.**
- **Wave lab — higher maximum simulation resolution.** The **Detail** slider
  (grid resolution) now reaches **512** columns, up from 300 — roughly triple
  the cell count for a much finer field on capable machines. Stability is
  unaffected (the scheme fixes `dx = dt = 1`, so the wave speed enters purely
  as the Courant number and the CFL margin is resolution-independent), and the
  share-link decoder's clamp was widened to match. The slider's tooltip notes
  that high detail is heavier on the CPU, so weaker machines can simply leave
  it lower.
- **Web UI — sleek modern restyle.** Replaced the warm-paper / serif
  "scholarly worksheet" theme with a cool, modern, scientific one: an all-sans
  UI (Inter / system stack; the wordmark, tagline, tabs, and headings drop the
  serif — KaTeX math stays serif), cool neutral surfaces with crisp white/slate
  panels, a clean blue accent (replacing the pine green), and tighter neutral
  shadows. Driven almost entirely by the central `app.css` tokens, so every
  view (workbench, console, graph, wave lab) restyles in both themes. A
  design-review pass then refined it: darker light-mode muted text for WCAG AA,
  smaller medium-weight tabs, result cards as soft-tinted output cells (no heavy
  accent bar), a unified soft-tint "selected" state on segmented controls
  (solid accent reserved for primary actions), an even Boundary grid, and
  higher-contrast slider tracks.

### Added

- **`fit` — least-squares regression, exact for polynomials.** A new verb that
  fits `x,y` data. Polynomial models (`linear`, `quadratic`, `cubic`,
  `quartic`, or `poly <degree>`) are solved **exactly over the rationals**: the
  normal equations `XᵀX·c = Xᵀy` reduce to exact power sums, and Gaussian
  elimination over `Q` returns exact coefficients — so `0,1; 1,2; 2,2; 3,4`
  fits to `9*x/10 + 9/10`, not a rounded decimal, and perfectly fittable data
  recovers its generating polynomial (`x^2`, `x^3`). It falls back to double
  precision on 64-bit overflow or non-rational data. `exp` (`a·e^{bx}`),
  `power` (`a·x^b`), and `log` (`a + b·ln x`) fit their linearized numeric
  models. Each fit reports the model, whether it is exact, and the coefficient
  of determination R². The fitted expression is a plottable function of `x`.
  Shipped across the CLI (`fit "0,0; 1,1; 2,4" quadratic`), REPL
  (`fit … | <model>`), the `fit` wasm binding, and the web console (with a
  reference-panel entry).
- **`together` — combine a sum of fractions over a common denominator**
  (docs/proposals/together.md): the companion to `cancel`, closing the other
  half of the "combine and reduce fractions" gap. `1/x + 1/y` now becomes
  `(x + y)/(x*y)`, `1/(x-1) + 1/(x+1)` becomes `2*x/((x-1)*(x+1))`, and
  `a + 1/x` becomes `(a*x + 1)/x`. It assembles the least common denominator
  as the product of each distinct denominator base at its maximum power,
  scales and sums the numerators — no GCD or factoring, so it is fully
  multivariate — and keeps the denominator factored (`(x*y)`, not a
  distributed `y^-1·x^-1`). Nothing symbolic to combine (or a 64-bit
  overflow) returns the input unchanged, never throwing. Shipped as an
  explicit verb (CLI `together "1/x + 1/y"`, REPL, wasm, web console),
  deliberately not folded into `simplify`. Pairs with `cancel` via
  `cancel(together(e))`.
- **`cancel` — rational-expression cancellation** (docs/proposals/cancel-poly-gcd.md):
  a new verb that removes the common polynomial factor of a rational
  expression's numerator and denominator, exactly, over 64-bit rationals —
  the "first five minutes" gap where `(x^2-1)/(x-1)` used to come back
  untouched now cancels to `x+1`, `(x^2-1)/(x^2-3x+2) → (x+1)/(x-2)`, and
  `2/(2x-2) → 1/(x-1)`. It splits the expression into `N/D`, and when both are
  single-variable polynomials with rational coefficients, divides by their GCD
  (Euclid over `Q[x]` with primitive-part normalization, content folded in),
  internally verifying the quotient before publishing it. Anything outside
  that class — no denominator, non-polynomial parts, symbolic coefficients,
  more than one symbol, or 64-bit overflow — comes back unchanged, never
  throwing. Shipped as an explicit verb (CLI `cancel "(x^2-1)/(x-1)" [x]`,
  REPL, and the `cancel` wasm binding), deliberately **not** folded into
  `simplify` (it erases removable singularities, a domain change; see the
  proposal §7). Formal cancellation, same doctrine as `x/x → 1`.
- **Wave system, Phase 4 — authoring & analytics** (docs/proposals/wave-system.md):
  the wave lab can now be **saved, shared, and driven by the CAS**. A **Share
  link** button encodes the whole setup — scene, boundary, every knob, the
  physics model, appearance, and the initial condition — into a URL that
  reproduces the experiment on load (validated and clamped on decode). An
  **Initial condition** input seeds `u(x,y,0)=f(x,y)` from any CAS expression,
  sampled onto the grid. A new **Drumhead** scene rings a circular-membrane
  **Bessel eigenmode**, and a numeric ⇄ analytic **verification bridge** in the
  tests confirms the FDTD matches the continuous rectangular-membrane frequency
  (<0.01%), the Bessel eigenfrequency, and the exact discrete d'Alembert
  travelling wave. (The wave overhaul — structured media, instrumentation,
  physics packs, redesign, and authoring — is now complete.)
- **Wave system — control surface redesign.** The Workbench "Wave" tab moves
  from a dense two-row toolbar to a grouped **left control rail** beside a
  hero canvas: a prominent transport with a live Running/Paused status and
  energy readout, and labelled sections — Simulation, Source, Scene, Physics,
  Measure, Appearance — with value-readout filled sliders, refined segmented
  controls, and styled dropdowns. The inline console `wave` cell keeps a slim
  single-row toolbar. No behavior change — same controls, clearer hierarchy.
- **Wave system, Phase 3 — physics packs** (docs/proposals/wave-system.md): a
  **field-model** selector changes the PDE the field obeys. **Klein–Gordon**
  adds a mass term `−m²u` — a dispersive medium where short waves outrun long
  ones, so a pulse spreads into trailing ripples and gains a rest frequency
  `√m`. **sine-Gordon** adds the nonlinear `−m²sin(u)`, whose 2π twists are
  **kink solitons**; one click *Seed kink* glides a soliton across the field
  without spreading. A **9-point isotropic Laplacian** toggle roughly halves the
  grid anisotropy (rounder wavefronts). The Courant clamp folds the stencil and
  the mass into the CFL, so the linear and Klein–Gordon fields stay stable at
  any slider (sine-Gordon's coupling is capped, honest about the nonlinear
  blow-up an explicit scheme can't avoid). New tests cover the exact dispersion
  relation, the rest frequency, kink translation, stencil isotropy, and the
  stability envelope.
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
- **Console verb suggestions**: when a console line names no command, a quiet
  "try:" row of chips appears beside the parsed-math preview, offering the
  verbs worth running on it, tailored via `analyze` — `factor`/`expand` and
  `solve = 0` for a polynomial, `apart` for a rational function, `series` for a
  transcendental one, `diff`/`integrate` for anything with a variable, and
  `eval` for a pure number, and `dsolve` for an ODE-shaped line (whose prime
  notation otherwise reads as a parse error — the chip is the rescue). After
  running a bare expression, the same picks reappear as a "next:" row under its
  result, so you can keep going without retyping. Clicking a chip runs `<verb>
  <line>` (with a ` = 0` suffix for `solve`); **Tab** fills the first
  suggestion. Suggestions stay hidden once a verb is typed.
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
