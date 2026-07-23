# Changelog

Notable user-facing changes per release. Contracts live in DESIGN.md; the
per-feature specs are under docs/proposals/.

## Unreleased (v0.6)

### Fixed

- **Grapher — no more vertical lines through asymptotes.** A curve like `tan(x)`, `1/x`, or `sec(x)` no longer draws a spurious near-vertical connector straight through each pole: the sampler now breaks the polyline where two consecutive samples straddle zero, both sit well off-screen, and the jump dwarfs the visible span — the pen lifts at the asymptote instead. Applies to `y=f(x)` and `x=f(y)`.

### Added

- **User-defined functions (grapher & console).** Define a function with
  `f(x) = x^2` and use it anywhere: plot `y = f(x) + 1`, its derivative `f'(x)`
  (or `f''(x)`), a value `f(3)`, a composition `g(f(x))`, or multi-argument
  forms `h(x, y) = x^2 + y^2`. In the grapher a bare `f(x) = …` row plots as
  `y = f(x)`; in the **console**, `f(x) = x^2` then `f(3)` returns `6`, and
  `funcs` lists your definitions. Functions are beta-reduced before the engine
  parses the input (so `f(x+1)` correctly becomes `(x+1)^2`, capture-safe even
  for `h(a,b)=a/b` called `h(b,a)`), free variables in a body still become
  sliders (the `a` in `f(x) = a*x^2`) while parameters never do, recursion and
  names that shadow a built-in (`sin`, `pi`, …) are rejected with a clear
  message, and a bare `f` without arguments reminds you to write `f(x)`.

- **Exact real-root counting & isolation (`rootcount`, `isolate`).** By Sturm's
  theorem over exact rational arithmetic: `rootcount x^5 - 3x + 1` → `3 distinct
  real roots`; `rootcount x^2 - 2, x, 0, 5` → `1 distinct real root in (0, 5]`.
  `isolate x^3 - x - 1` brackets the sole real root (the plastic number
  `≈ 1.3247`) in a rational interval, and `isolate 2x^2 - 3x + 1` reports the
  exact rational roots `x = 1/2`, `x = 1`. Multiplicity never inflates the count
  (a double root counts once), rational roots are pulled out exactly (rational-
  root theorem) and irrational ones bracketed then refined, and both verbs
  accept an equation form (`isolate x^2 = 2`). Symbolic coefficients are
  rejected with a clear message — Sturm needs concrete signs.

- **Padé approximants (`pade`).** `pade exp(x), 2, 2` →
  `(x^2/12 + x/2 + 1)/(x^2/12 - x/2 + 1)`; `pade sin(x), 3, 2` →
  `(-7*x^3/60 + x)/(x^2/20 + 1)`. The [m/n] Padé approximant is the rational
  function P(x)/Q(x) whose Maclaurin series matches the input through order
  m + n — often tracking a function well past the reach of its Taylor
  polynomial. Built from the exact Taylor coefficients (via `series`) by
  solving the denominator's defining linear system over exact arithmetic;
  coefficients stay symbolic, so `pade exp(a*x), 1, 1, x` keeps the parameter
  `a`. A rational input is reproduced exactly (`pade 1/(1-x), 2, 2` →
  `1/(1-x)`), including defective (non-normal) table entries where the
  denominator collapses to lower degree.

- **Grapher — definite-integral area shading.** A row written as
  `integral(f, a, b)` shades the signed area between `y = f(x)` and the x-axis
  over `[a, b]` (the band flips below the axis wherever `f < 0`) and labels the
  exact ∫ value straight from the CAS — `integral(sin(x), 0, pi)` fills the arch
  and reads `∫ = 2`, not a rounded decimal. The bounds accept expressions and
  session variables, so `integral(f, 0, b)` sweeps the region live as the `b`
  slider moves. The one- and two-argument forms (`integral(f)`,
  `integral(f, t)`) are unchanged — they still plot the antiderivative curve.

- **Polynomial resultant (`resultant`).** `resultant x^2 - 1, x - 2` → `3`;
  `resultant x^2 - 1, x - 1` → `0` (they share the root `x = 1`). The resultant
  of two polynomials is zero exactly when they have a common factor; it's
  computed via the Euclidean recursion over the polynomial remainder (no
  Sylvester matrix), so symbolic coefficients stay symbolic —
  `resultant x^2 + b*x + c, 2x + b` recovers the discriminant `-(b² - 4c)`. In
  the CLI, REPL, and web console.
- **Logarithm expansion & combination (`logexpand`, `logcombine`).** The log
  analog of the trig-rewrite pair. `logexpand ln(x*y)` → `ln(x) + ln(y)`,
  `ln(x^3)` → `3·ln(x)`, `ln(x/y)` → `ln(x) - ln(y)`; `logcombine` inverts it,
  collecting a sum of logs into one — `ln(x) + ln(y)` → `ln(x·y)`, `3·ln(x)` →
  `ln(x^3)`, `ln(x) - ln(y)` → `ln(x/y)`. Formal rewrites (valid for positive
  arguments); atoms and non-log terms are left alone. In the CLI, REPL, and web
  console.
- **Polynomial GCD & LCM (`polygcd`, `polylcm`).** `polygcd x^2 - 1, x^3 - 1`
  → `x - 1`; `polylcm x - 1, x + 1` → `x² - 1`. The GCD runs the Euclidean
  algorithm over the polynomial remainder (building on `polydiv`) and returns a
  monic result — `polygcd (x-1)²(x+2), (x-1)(x+2)²` → `x² + x - 2` — and the LCM
  is `a·b / gcd`. In the CLI, REPL, and web console.
- **Polynomial long division (`polydiv`).** A new verb divides one polynomial
  by another, reporting the **quotient** and **remainder** exactly (symbolic
  coefficients kept symbolic): `polydiv x^3 - 1, x - 1` → quotient `x² + x + 1`,
  remainder `0`; `polydiv x^3 + 2x + 1, x^2 + 1` → quotient `x`, remainder
  `x + 1`. In the CLI, REPL, and web console.
- **Trigonometric reduction (`trigreduce`).** The inverse of `trigexpand`:
  rewrite products and powers of sines and cosines into a linear combination of
  sines and cosines of multiple angles. `sin(x)²` → `1/2 - cos(2x)/2`,
  `2 sin(x) cos(x)` → `sin(2x)`, `cos(x)⁴` → `cos(4x)/8 + cos(2x)/2 + 3/8`, and
  `sin(x) sin(y)` → `cos(x - y)/2 - cos(x + y)/2`. Exact, via the
  complex-exponential form; non-trig factors ride along as coefficients. In the
  CLI, REPL, and web console.
- **Trigonometric expansion (`trigexpand`).** A new verb expands trig of sums
  and integer multiples into single-angle products and powers, then
  simplifies: `sin(a + b)` → `sin(a)cos(b) + cos(a)sin(b)`, `cos(2x)` →
  `cos(x)² - sin(x)²`, `sin(3x)` its cubic form, and `tan(a + b)` in terms of
  sines and cosines. Special-value angles collapse (`sin(x + π/2)` → `cos(x)`);
  single angles and non-integer/symbolic multiples (`sin(x/2)`, `sin(ax)`) are
  left untouched. In the CLI, REPL, and web console.
- **Polynomial discriminant (`discriminant`).** A new verb returns the exact
  discriminant of a degree 2–4 polynomial from its closed-form formula, with
  **symbolic coefficients kept symbolic**: `discriminant a*x^2 + b*x + c, x` →
  `b^2 - 4*a*c`, and the cubic/quartic forms too. When the coefficients are
  numeric it also classifies the roots from the discriminant's sign —
  `x^2 - 5x + 6` → `1` ("two distinct real roots"), `x^2 + 1` → `-4` ("two
  complex-conjugate roots"), `(x-1)^2` → `0` ("one repeated real root"). In
  the CLI, REPL, and web console.
- **Terminal app (Ink).** A new, experimental second frontend in `apps/ink/`,
  built with [Ink](https://github.com/vadimdemedes/ink) (React for the command
  line). It loads the same WebAssembly engine as the web app and drives it with
  the classic REPL grammar — a bare expression simplifies, a bare equation
  solves, and verbs (`solve`, `diff`, `integrate`, `limit`, `dsolve`, `sum`,
  `fit`, `grad`, `gcd`, …) take comma-separated arguments — rendering structured
  results (values, `method:`, warnings, caret-underlined parse errors) and a
  live "as parsed" preview. Runs interactively, one-shot, or over piped stdin.
  The classic C++ REPL/one-shot CLI (`apps/main.cpp`) is unchanged and remains
  the reference; this is a parallel experiment that may grow into a richer
  terminal experience. Session assignments (`:=`) are not yet supported there.
- **Inequality solving.** `solve` now accepts inequalities and returns a
  solution set of intervals instead of only equations: `solve x^2 < 4` →
  `x ∈ (-2, 2)`, `solve x^2 >= 4` → `x ∈ (-∞, -2] ∪ [2, ∞)`. It combines the
  two sides over a common denominator, takes the real roots of the numerator
  (zeros) and denominator (poles) as breakpoints, sign-tests each interval,
  and assembles the answer with **exact endpoints** (radicals like `√2` are
  kept) and correct open/closed brackets — poles are excluded (`1/x > 0` →
  `(0, ∞)`), removable holes stay excluded, and `≤`/`≥` roots are included.
  Degenerate cases are handled: `x^2 >= 0` → all reals, `x^2 < 0` → no
  solution, `x^2 <= 0` → the single point `{0}`. Works in the CLI, REPL, and
  web console (`<`, `>`, `<=`, `>=`, and the Unicode `≤`/`≥`).
- **Modular arithmetic (`mod`, `powmod`, `modinv`, `crt`).** Rounds out the
  number-theory suite. **`powmod b, e, m`** does modular exponentiation with
  square-and-multiply over 128-bit products, so `powmod 7, 1000000, 13`
  returns instantly where ordinary evaluation would overflow. **`modinv a, m`**
  gives the modular inverse via the extended Euclidean algorithm (and reports
  when `a` isn't invertible). **`crt r1, r2, …; m1, m2, …`** solves a system of
  congruences by the Chinese remainder theorem, allowing non-coprime moduli
  (`crt 2,3,2; 3,5,7` → `23 (mod 105)`). **`mod a, m`** is the Euclidean
  remainder in `[0, m)`. In the CLI, REPL, and web console.
- **Continued fractions (`cfrac`).** A new verb expands a value into its
  continued fraction `[a0; a1, a2, …]` and lists the **convergents** — the
  successive best rational approximations. Exact rationals give a **finite**
  expansion (`cfrac 355/113` → `[3; 7, 16]`); **`sqrt(n)`** gets its exact
  **periodic** expansion (`sqrt(2)` → `[1; (2)]`, with the period overlined in
  the console); anything else falls back to a numeric expansion. The
  convergents surface the famous approximations — `cfrac pi` walks
  `3, 22/7, 333/106, 355/113`, and feeding a decimal like `3.14159`
  recovers them exactly. Available in the CLI, REPL, and web console.
- **Number theory over the integers.** A new pack of exact integer verbs —
  available in the CLI, the REPL, and the web console. **`factor`** now
  prime-factorizes a bare integer (`factor 360` → `2^3 · 3^2 · 5`) instead of
  echoing it, while still factoring polynomials as before. New verbs:
  **`gcd`** / **`lcm`** of a list, **`isprime`** (deterministic Miller–Rabin
  valid across the whole 64-bit range — it isn't fooled by Carmichael
  numbers), **`nextprime`**, **`divisors`**, and Euler's **`totient`**.
  Factorization uses trial division plus Pollard's rho, so it handles large
  semiprimes, and every result is exact. In the console they render like any
  other result and appear under a new **Number theory** group in the Commands
  reference.
- **Grapher — contour maps of z = f(x, y).** A bare expression in **both** `x`
  and `y` — e.g. `x^2 + y^2`, `sin(x) + cos(y)`, or `x*y` — is no longer a
  broken 1-D curve: it now renders as a **contour map**, the level sets of the
  surface `z = f(x, y)`. The field is sampled once on a grid over the visible
  window; "nice" iso-levels (round multiples spanning its value range) are
  traced with marching squares, so `x^2 + y^2` draws as concentric circles and
  `x*y` as hyperbolic level curves. The **`z = 0` level is drawn solid** (the
  boundary between `f > 0` and `f < 0`); the rest are dashed, in the row's
  colour. Levels re-pick as you pan and zoom.
- **Grapher — slope fields & solution curves.** A row written as `y' = f(x, y)`
  (or `dy/dx = f(x, y)`) now draws the **direction field** of that first-order
  ODE — a lattice of short segments whose slope is `f` — pairing the CAS with
  the canvas. Any plotted **point `(x₀, y₀)`** then grows the **solution curve**
  through it, integrated with RK4; because points stay draggable, dragging one
  **sweeps its solution curve live** across the field. The field's `f` is
  sampled once through the engine and the curves ride a bilinear interpolant of
  that grid, so the whole thing re-integrates smoothly per frame. A parameter in
  `f` (e.g. `y' = a - y`) auto-creates a slider like any other row.
- **Grapher — editable data tables with regression.** A new **＋ Add table**
  button drops an editable `(x, y)` grid into the expression list: type points
  (the grid auto-grows a blank row) and they plot as a scatter in the row's
  color. A **Fit** selector overlays a regression curve — **linear /
  quadratic / cubic** (solved *exactly* over the rationals by the CAS `fit`
  verb, so `(0,1) (1,2) (2,2) (3,4)` fits to `x²/4 + 3x/20 + 23/20`, not a
  rounded decimal) plus **exponential / power / logarithmic** numeric models —
  and shows the fitted equation with its R² and an "exact" badge. Tables
  persist to localStorage and ride along in share links. Each table also shows
  **exact per-column summary statistics** (mean, median, sample standard
  deviation) from the CAS `stats` verb — so `x = 1, 2, 3, 4` reports mean `5/2`
  and `s = √15/3`, not decimals.
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

- **`prob` plugin — probability distributions.** A new plugin covering the
  distributions of a first statistics course: **normal** (`normalpdf`,
  `normalcdf`, and `invnorm` — the inverse/quantile), **binomial** (`binompdf`,
  `binomcdf`), **Poisson** (`poissonpdf`, `poissoncdf`), **Student's t**
  (`tpdf`, `tcdf`), **chi-squared** (`chi2pdf`, `chi2cdf`), **exponential**
  (`exppdf`, `expcdf`), and **continuous uniform** (`unifpdf`, `unifcdf`). Each
  command reports the queried value and **plots the distribution** — the bell
  curve for the normal, stem markers for the discrete PMFs, the density for the
  continuous families — with a marker at the query point. `prob.normalcdf 1.96`
  → `0.975`, `prob.invnorm 0.975` → `1.95996`, `prob.binompdf 10, 0.5, 5` →
  `0.2461`, `prob.tcdf 2.228, 10` → `0.975`, `prob.chi2cdf 7.815, 3` → `0.95`.
  The normal CDF uses `erf` and the inverse uses Acklam's rational approximation
  refined by a Halley step; the discrete PMFs evaluate in log-space (`lgamma`)
  for stability at large counts; the t and chi-squared CDFs use the regularized
  incomplete beta and gamma functions.
- **`stats` — exact summary statistics.** A new verb that summarizes a data
  list (`stats "1, 2, 3, 4, 5"`) — n, sum, mean, min/max/range, the quartiles
  and median (Moore & McCabe method), IQR, and both population and sample
  variance and standard deviation. When the data are rational every statistic
  is **exact**: the mean stays a fraction (`1, 2, 4` → `7/3`) and the standard
  deviation a simplified radical (`1, 2, 3, 4, 5` → `√2` and `√10/2`), where a
  calculator can only show a decimal; non-rational data or 64-bit overflow fall
  back to double precision. Shipped across the CLI, REPL, the `stats` wasm
  binding, and the web console (rendered as a typeset table, with a
  reference-panel entry).
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
