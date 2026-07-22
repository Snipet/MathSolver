# Planning: Next Large Features — Bridging the Gap to Commercial CAS

Status: **planning** — strategic direction, not a per-feature contract. Each
project below graduates to its own `docs/proposals/<name>.md` (DESIGN.md
contract standard) before implementation. Grounded in live probes against
`build-local/mathsolver` (v0.5.0, 2026-07) and cross-referenced with
docs/ROADMAP.md.

Goal: identify the **large projects** that most narrow the distance between
MathSolver and commercial/OSS math software (Wolfram|Alpha & Mathematica,
Maple, MATLAB, SymPy, Symbolab, Desmos/GeoGebra).

---

## 1. Where we stand

MathSolver is already broad on the **applied/engineering** side: verified
symbolic calculus (diff; rule-based integration that differentiates every
candidate antiderivative back before printing; limits; Laplace/inverse-Laplace;
Taylor series; `apart`), ODE/recurrence solving, vector calculus, and a real
applied-math **plugin ecosystem** — `dsp`, `sys`, `linalg`, `pde`, `fem`, `ie`,
`hyb`. It ships as a CLI, a WASM web app (workbench, console notebook, 2-D
grapher), and an MCP server.

The gaps are not in exotic engineering domains — they are in the **CAS
foundations** and the **student-facing experience** that commercial tools take
for granted. Probed against the built engine:

| Area | Probe | Result today |
|---|---|---|
| Big integers | `2^100` | ✗ `rational arithmetic overflow` |
| Factorials/combinatorics | `factorial(30)`, `50!` | ✗ stays `gamma(31)`; `!` won't parse |
| Complex arithmetic | `(2+3i)*(1-i)`, `e^(i*pi)` | ⚠ `expand` folds products; `simplify` won't rationalize `1/(1+i)`; `eval` refuses complex |
| Complex solving | `x^2+x+1=0` | ⚠ solver special-cases quadratic complex roots only |
| Matrices in the language | `[[1,2],[3,4]]`, `det …` | ✗ parse error; matrices live only in the `linalg` plugin's own syntax |
| Inequalities | `solve "x^2 < 4"` | ✗ `<` is a lex error |
| Assumptions | `sqrt(x^2)` with `x>0` | ✗ always `abs(x)` |
| Number theory | `gcd`, `mod`, `factor 360` | ✗ none; `factor` is polynomial-only |
| Units | `3 meters + 2 meters` | ✗ no unit system |
| Step-by-step | any solve/integrate | ⚠ one-line `method:` label, not steps |
| Plotting | web grapher | ⚠ 2-D only — no 3-D/surface/contour |

---

## 2. The large projects, tiered by leverage

### Tier 1 — Foundational enablers

**T1. Arbitrary-precision arithmetic** (bignum integers/rationals +
arbitrary-precision float). Removes the `2^100` / `n!` / high-precision wall; a
prerequisite for number theory and combinatorics. Contained *behind* the
existing `Rational` API (ROADMAP P2-4) — no AST change. Effort **L**.

**T2. Complex numbers as a first-class domain (ℂ).** The single biggest
CAS-credibility item and a force-multiplier for the plugins (AC impedance in
`sys`, complex response in `dsp`, full Fourier/Laplace, residues). The core
already has `ConstantId::I`, `i^n` folding, and complex quadratic roots — this
project completes the domain (rationalized `a+b·i` normal form, complex
evaluation, `conj`/`Re`/`Im`/`arg`/`abs`, Euler's formula, all-degree complex
roots). Doctrine change handled behind an opt-in domain so the real-path
invariants and fuzzers stay intact. Effort **L–XL**. **← flagship; see
docs/proposals/complex-domain.md.**

### Tier 2 — High-leverage capability & experience

**T3. Step-by-step solution engine.** The Symbolab/Wolfram education moat and
the strongest pull for the web audience. The engine already knows the `method`
for solve/diff/integrate/simplify; this captures the derivation as an ordered,
KaTeX-rendered step trace. Self-contained (instrument the transform pipelines);
no foundational dependency. Effort **L**.

**T4. First-class symbolic linear algebra.** Promote matrices/vectors into the
expression language (`[[1,2],[3,4]]` parses; `A·B`, symbolic `det`/`inv`/`rref`/
eigen; solve `Ax=b` symbolically), folding the siloed `linalg` plugin syntax
into the core. Needs a Matrix node — a deliberate extension of the 7-node AST.
Effort **L**.

**T5. Assumptions & domains framework.** `x>0`, `n∈ℤ`, `x∈ℝ` → correct
simplification (`sqrt(x^2)=x`), integration-branch selection, better solving,
and unlocks `logcombine`/`trigcombine`. Additive by design (ROADMAP P1-6):
threaded into `simplify`/`solve`; `provably_nonneg` is the extension point.
Effort **M–L**.

### Tier 3 — Breadth & market-specific (mostly plugin-shaped)

- **T6. Inequalities & richer solving** (ROADMAP P1-5, **L**): interval
  solutions, nonlinear systems, transcendental. Day-one Symbolab feature.
- **T7. Number-theory & discrete toolkit** (**M**, *after T1*): `isprime`,
  `factorint`, `gcd`/`lcm`, modular arithmetic/inverse, CRT, Diophantine,
  combinatorics.
- **T8. 3-D & advanced plotting** in the web app (**L**, web-only): surfaces
  `z=f(x,y)`, parametric curves/surfaces, contour, implicit (WebGL) — Desmos-3D
  / GeoGebra parity on a grapher that already exists in 2-D.
- **T9. Statistics & probability plugin** (**M–L**): distributions, descriptive
  stats, regression/curve-fit, hypothesis tests.
- **T10. Units & physical quantities** (**M–L**): dimension-checked arithmetic +
  SI conversion — an engineering differentiator for the `dsp`/`sys` audience.

---

## 3. Recommended sequence

1. **T1 Bignum** — cheap-ish, removes embarrassing hard walls, unblocks T7.
2. **T2 Complex domain** — the flagship; biggest "why can't it do this" fixer.
3. **T3 Step-by-step** — independent, strongest web/education pull.

Then **T4 symbolic linear algebra** and **T5 assumptions** as the next
credibility tier, with the Tier-3 items slotting in as plugins.

**Doctrine note:** T2, T4, and T6 each nudge a founding doctrine (real-domain,
7-node AST). Each wants a conscious "extend the doctrine here, behind a
flag/node, with the fuzzers as the gate" decision — captured in that project's
proposal — rather than incremental patches.

---

## 4. Status

- **T2 Complex domain** — in progress. Proposal: `docs/proposals/complex-domain.md`.
- All others — planned; no proposal yet.
