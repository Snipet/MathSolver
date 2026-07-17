# MathSolver Roadmap — Parser & Simplifier/Augmenter

Evidence-based roadmap for what the parser and the simplifier need next.
Grounded in DESIGN.md (§2, §4, §5, §7), docs/GRAMMAR.md, REVIEW-NOTES.md, and
~90 live probes against `build/mathsolver` (v0.3.0, 2026-07). Every gap below
cites verbatim probe output. Classification: **WORKS / PARTIAL / MISSING /
MISLEADING** (misleading = parses/simplifies to something *silently wrong* for
the user's obvious intent — the worst class, and the one this roadmap
prioritizes killing).

---

## 1. Current-state capability matrix

### Parser

| Area | Status | Probe evidence |
|---|---|---|
| Core grammar (LaTeX+ASCII, `\frac`, `\sqrt[n]`, `\sin^2 x`, `\sin^{-1}`, subscripts, `\left/\right`, precedence, caret diagnostics) | WORKS | `\sqrt[3]{8}` → `2`; `sin^2(x)` → `sin(x)^2`; `arcsin(1/2)` → `pi/6` |
| Scientific notation | MISLEADING | `2e-3` → `2*e - 3` (≈2.436, not 0.002); `1.5E10` → `15*E` (E is a *variable*); `2e3` → `6*e` |
| Multi-letter variables | MISLEADING | `speed` → `d*p*s*e^2` (Euler's e folded in!); `price - cost` → `e*c*i*p*r - cos(t)`; `cbrt(8)` → `8*b*c*r*t`; `floor(2.5)` → `5*f*l*r*o^2/2` |
| Digit-suffix vars (`v1`) | MISLEADING | `v1` → `v` (parsed `v*1`); `v_1` → `v_1` WORKS |
| Absolute-value bars | MISSING | `\|x\|` → `error: unexpected character '\|'`; same for `\left\|x\right\|` |
| Unicode operators/symbols (web keyboards!) | MISSING | `2×3`, `6÷2`, `5−3` (U+2212), `√9`, `π`, `θ + 1`, `x²`, `3·x`, `sin(30°)` — all `error: unexpected character '\xNN..'` |
| Inequalities | MISSING | `solve "x^2 < 4"` → `error: unexpected character '<'`; `\le` → `unknown command` |
| Factorial | MISSING | `5!` → `error: unexpected character '!'` |
| floor/ceil | MISSING | `\lfloor x \rfloor` → `unknown command '\lfloor'` (plain `floor(...)` is the MISLEADING product above) |
| Summation/product | MISSING | `\sum_{k=1}^{n} k` → `unknown command '\sum'` |
| Common LaTeX synonyms | MISSING | `\dfrac{1}{2}`, `\operatorname{f}`, `\text{speed}`, `\mathrm{d}x`, `\infty` → `unknown command`; `\frac12` → `expected '{'` |
| Function definition | MISLEADING | `f(x) = x^2` → equation `f*x = x^2` |
| Mixed numbers | MISLEADING | `3 1/2` → `3/2` (implicit multiplication) |
| Percent | MISSING (clean error) | `50%` → `unexpected character '%'` |
| Complex literals | by design (real domain) | `3 + 2i` → `2*i + 3` (`i` is an ordinary symbol) |
| `log` with attached base digits | MISLEADING | `log10(x)` → `x` (read as `log(10)*(x)` = `(ln10/ln10)*x`) |
| Matrices, `max(a,b)`, chains `x=y=z` | MISSING (clean errors) | `[[1,2],[3,4]]` → `expected ']' , found ','` |
| Differential notation | MISLEADING | `d/dx(x^2)` → `x^3`; `\sin(x)\,dx` → `d*x*sin(x)` |

### Simplifier / augmenter

| Area | Status | Probe evidence |
|---|---|---|
| Like terms/factors, numeric folding, exact trig table, inverse-trig table, Pythagorean, abs-provability, `(u^a)^b` guards | WORKS | `\sin^2 x + \cos^2 x` → `1`; `sin(pi/6)` → `1/2`; `sqrt(x^2)` → `abs(x)`; `x/x` → `1` |
| Radical normal form | MISSING | `sqrt(8)` → `sqrt(8)`; `sqrt(72)` → `sqrt(72)`; `sqrt(1/2)` → `sqrt(1/2)`; `2^(3/2)` stays; `expand "(1+sqrt(2))^2"` → `2^(3/2) + 3` (students expect `2*sqrt(2) + 3`) — the solver already owns this code (`sqrt_of_rational`, src/solver.cpp:96-126: `sqrt(8) -> 2*sqrt(2)`) but simplify never calls it |
| Radical products / symbolic radicands | MISSING | `sqrt(2)*sqrt(3)` stays split; `sqrt(4*x^2)` unchanged (expect `2*abs(x)`); `sqrt(x^2*y^4)` unchanged |
| Rationalizing denominators | MISSING | `1/sqrt(2)` unchanged; `1/(1 + sqrt(2))` unchanged |
| Rational-expression cancellation | MISSING | `(x^2 - 1)/(x - 1)` unchanged; `(x*y + x*z)/x` unchanged; `1/x + 1/y` not combined |
| Symbolic-exponent power combining | MISSING | `e^x*e^y`, `x^a*x^b`, even the *identical-factor* `2^x * 2^x` all unchanged; `e^(2*ln(x))` not folded to `x^2` |
| Rational log fold | MISSING | `ln(8)/ln(2)` stays (so `solve "2^x = 8"` answers `x = ln(8)/ln(2)`, not `x = 3`) |
| log expand/combine | MISSING as *commands* (excluded from simplify by §7 — correctly) | `ln(x*y)`, `ln(x^3)` unchanged; no `logexpand`/`logcombine` verb exists |
| Trig expand/combine | MISSING as *commands* | `sin(2x) - 2*sin(x)*cos(x)` ≠ 0; `sin(x)/cos(x)` not `tan(x)`; `cos(x)^2 - sin(x)^2` stays; no `trigexpand` verb |
| Factoring beyond quadratics | PARTIAL | `factor "x^2 - 5x + 6"` → `(x - 3)*(x - 2)` WORKS; but `x^3 - x` → `x*(x^2 - 1)` (no recursion into the residual); `x^2 - y^2`, `x^3 - 1`, `x^3+3x^2+3x+1`, `x^4 - 5x^2 + 4`, `a*x+a*y+b*x+b*y` all unchanged |
| `collect` (exists in lib, §7) | MISSING from CLI/REPL/wasm | REPL: `collect x^2+ax, x` → `error: unexpected ','` (no such command); `simplify "a*x + b*x"` stays uncollected |
| Substitution | MISSING as command | `subs x^2 x=3` → `usage error: unknown command 'subs'` (lib has `substitute`) |
| Partial fractions | MISSING as command | integrator uses it internally (`integrate "1/(x^2-1)"` → `method: partial fractions`) but no user verb |
| Poly GCD / division / degree / coeff queries | MISSING as commands | `polynomial_coefficients` is library-only |
| Assumptions | MISSING | no way to state `x>0` so `sqrt(x^2)` must stay `abs(x)` |
| 64-bit rational ceiling | wall | `simplify "2^100"` → exit 1, `rational arithmetic overflow in multiplication` |

Verification assets available to gate all of this (REVIEW-NOTES.md):
`tools/run_fuzz.sh` (round-trip + differential-eval, clean at 45k+),
`tools/run_acceptance.py` + `tests/acceptance/*.tsv`, simplify-idempotence and
round-trip property tests, simplify/derivative differential fuzzers.

---

## 2. Benchmark: the first five minutes vs SymPy / Symbolab

Where a student or engineer hits a wall immediately with this tool:

1. **Types on a phone** (the new web audience): `×`, `÷`, `−` (the minus most
   mobile math keyboards emit), `π`, `√`, `²` — *every one* is a hard parse
   error today. Symbolab-class tools accept all of them.
2. **Types a word**: `speed`, `cost`, `price` — silently becomes a letter
   product, sometimes with `e`/`cos` folded in (`cost` → `cos(t)`). SymPy
   treats these as one symbol. This is the single worst *trust-destroying*
   behavior found.
3. **Types `|x-2| = 3` or `x^2 < 4`** — parse errors. Both are day-one
   Symbolab inputs.
4. **Gets `sqrt(8)` back unsimplified**, or `2^(3/2)` where every textbook
   writes `2√2` — reads as "the tool can't do radicals".
5. **Solves `2^x = 8` and gets `ln(8)/ln(2)`** instead of `3`.
6. **Enters `2e-3`** (pasted from a datasheet) and gets `2e − 3` ≈ 2.44.
7. **Asks to cancel `(x^2-1)/(x-1)`** — nothing happens (SymPy: `cancel`,
   `simplify` both do it).

Items 1–3 are parser; 4–7 simplifier. The CLI user survives via docs; the web
user bounces. Forgiving input is the top priority for v0.4.

---

## 3. Prioritized recommendations

Effort: S <1 day, M = days, L = week+. All items must keep
`ctest --test-dir build` green and re-run `tools/run_fuzz.sh` +
`tools/run_acceptance.py` (the standing gate).

### P0 — quick wins (high value / low risk)

**P0-1. Unicode input pack (lexer).**
*User story:* a phone user types `2×3`, `5−3`, `√9`, `π`, `x²`, `sin(30°)` in
the web box and it just works.
*Evidence:* all nine unicode probes are hard errors (§1).
*Sketch:* recognize multi-byte sequences directly in `Lexer::lex()` (src/parser.cpp)
— `×`/`·` → `Tok::Star`, `÷` → `Tok::Slash`, `−` → `Tok::Minus`, `π` →
`Constant(Pi)`, Greek letters → their existing named symbols, `√` → `Func("sqrt")`
(bare-argument rule already handles `√9`, `√(x+1)`), `⁰¹²³⁴⁵⁶⁷⁸⁹` → `Caret` +
digit run, `°` → postfix `* (pi/180)` (a `Mul` inserted at lex/parse level;
`sin(30°)` then hits the existing exact-value table → `1/2`). Tokens carry the
true byte span of the multi-byte char, so **caret diagnostics keep working
unchanged** — do *not* pre-normalize the string, which would break §4's
byte-span contract. `≤ ≥ ≠ <` get a *targeted* error ("inequalities are not
supported yet") until P1-5 lands. Printer never emits unicode → §5 round-trip
untouched.
*Effort:* S-M. *Risk:* low (pure additions). *Gate:* new lexer unit tests,
acceptance TSV rows, `run_fuzz.sh` (should be unaffected).

**P0-2. Absolute-value bars `|x|` (+ fix the LaTeX printer for Abs).**
*User story:* `solve "|x-1| = 3"` works as typed; results render correctly.
*Evidence:* `|x|` → `unexpected character '|'`; `abs()` already fully works
(`solve "abs(x - 1) = 3"` → `x = -2`, `x = 4`). Also `latex abs(x)` emits
`abs\left(x\right)` — not real LaTeX; the web's KaTeX rendering will typeset
"abs" as a variable product.
*Sketch:* `|` token; in `parse_atom`, `|` opens an Abs group parsed as a full
expr up to the matching `|` (bar in atom position opens, otherwise closes — the
standard disambiguation; nested bars via `\left|`/`(`). Accept `\left| ... \right|`
in `parse_left_right` and `\lvert/\rvert/\lfloor→error-with-hint`. Printer:
LaTeX style for Abs → `\left| ... \right|` (Plain stays `abs(...)`; round-trip
needs the parser to accept both, which it then does).
*Effort:* S-M. *Risk:* bar-matching ambiguity in pathological inputs (`||x||`)
— define and test. *Gate:* parser tests, round-trip fuzzer, printer TSV.

**P0-3. Scientific notation `2e-3`, `1.5E10`.** ⚠ *doctrine change*
*User story:* an engineer pastes `1.5e-3 * x` and gets 0.0015x.
*Evidence:* `2e-3` → `2*e - 3`; `1.5E10` → `15*E` (capital `E` isn't even
Euler — it's a variable, so the misparse is doubly silent).
*Sketch:* in `lex_number()`, after the digit/decimal run, accept `[eE]` +
optional sign + **required** digit run as part of the literal (exact rational
scaling by 10^k). `2e` alone, `2ex`, `e3` unchanged (`e3` still `e*3`).
**This deliberately changes the meaning of existing inputs** (`2e3`: `6e` →
`2000`) and contradicts DESIGN §4/§12 and GRAMMAR's pitfall #3 — amend both
documents in the same commit; that pitfall section exists precisely because
users get this wrong, which is the argument *for* the change.
*Effort:* S. *Risk:* medium (value-changing; someone may rely on `2e3`=6e —
unlikely). *Gate:* lexer tests both directions, acceptance rows, fuzzer
(printer never emits e-notation, so round-trip safe).

**P0-4. Word-variable guard: silent products → honest errors.** 
*User story:* typing `speed` or `price - cost` produces "unknown name 'speed';
variables are single letters (a–z), Greek names, or subscripted (v_1)" instead
of `d*p*s*e^2`.
*Evidence:* `speed` → `d*p*s*e^2`; `price - cost` → `e*c*i*p*r - cos(t)`;
`cbrt(8)` → `8*b*c*r*t`; `floor(2.5)` → `5*f*l*r*o^2/2`.
*Sketch:* in `lex_identifier_run()`, after segmentation, if a maximal letter
run of length ≥ 3 decomposed into ≥ 3 tokens **none of which is a known
function applied to the following input** — or contains ≥ 2 consecutive
single-letter fallbacks — raise ParseError over the run's span with the hint.
Keep 2-letter runs (`xy`, `ab`) as products: idiomatic math. This preserves the
§4 single-letter doctrine while killing its trap; full multi-letter variables
remain P2 (they *would* violate §4 and break `xy`-style implicit mult).
Tune the heuristic against `tests/acceptance/cases.tsv` (e.g. `sinx`, `2pir`
must keep parsing).
*Effort:* S-M. *Risk:* heuristic false positives — mitigate with the corpus +
fuzzer. *Gate:* acceptance TSV (both accept and reject rows), round-trip fuzz.

**P0-5. Radical normal form in simplify (`sqrt(8) → 2*sqrt(2)`).**
*User story:* every textbook-form answer: `sqrt(72)` → `6*sqrt(2)`,
`(1+sqrt(2))^2` expands to `2*sqrt(2) + 3`, `sqrt(1/2)` → `sqrt(2)/2`.
*Evidence:* §1 radical rows; the algorithm already exists as
`sqrt_of_rational`/`sqrt_expr` in src/solver.cpp:96-154 (solver results
*already* print `x = sqrt(2)` nicely — simplify just never does it).
*Sketch:* lift `sqrt_of_rational` into simplify (or a shared internal header);
add a Pow rule: `Number^(p/q)` (non-integer, |base|>1) → extract the perfect
q-th-power part and integer part: `8^(1/2)` → `2*2^(1/2)`, `2^(3/2)` →
`2*2^(1/2)`. **Confluence hazard (probed):** `apply_mul_rules` currently folds
`2 * 2^(1/2)` right back to `2^(3/2)` (seen in `simplify "sqrt(3+2*sqrt(2))"`
→ `sqrt(2^(3/2) + 3)`). The two rules must agree on one normal form: amend the
like-factor rule to *not* merge a Number base with numeric exponents when the
merged exponent is a non-integer with |p|>q (i.e. extracted form is normal).
This is a §7 rule-inventory amendment — document it there.
*Effort:* M (the rule is easy; the fixpoint/idempotence proof is the work).
*Risk:* medium — non-confluence breaks the tested idempotence property.
*Gate:* simplify idempotence property test, simplify/expand differential
fuzzer (40k), round-trip fuzz, acceptance rows.

**P0-6. Rational log fold: `ln(8)/ln(2) → 3`.**
*User story:* `solve "2^x = 8"` answers `x = 3`; `log_2(8)` → `3`.
*Evidence:* both currently return `ln(8)/ln(2)`.
*Sketch:* Mul rule for the canonical shape `Mul(..., Ln(a), Pow(Ln(b), -1), ...)`
with integer/rational `a,b > 0`, `b ≠ 1`: if `b^k = a` for integer (extend:
rational) `k`, replace the pair with `k`. Safe on the reals (both sides defined
and equal). Bounded search: k ≤ 63 via overflow-checked pow.
*Effort:* S. *Risk:* low. *Gate:* differential fuzzer, unit tests.

**P0-7. Expose the existing library verbs: `subs`, `collect` (CLI + REPL + wasm).**
*User story:* `mathsolver subs "x^2 + 1" x=y+1` → `(y + 1)^2 + 1` (compose
with `expand` for the expanded form);
`mathsolver collect "a*x + b*x + 1" x` → `(a + b)*x + 1`.
*Evidence:* `subs` → `usage error: unknown command`; REPL `collect` → parse
error; `simplify "a*x + b*x"` stays split (correct per §7 — collection is
`collect`'s job, but the user can't reach it).
*Sketch:* `substitute` and `collect` already exist (expr.cpp / simplify.hpp §7).
CLI subcommand + REPL command + wasm binding + types.ts entry, reusing the
`eval`-style `var=expr` binding parser (bind an *expression*, not just a
number). No engine change at all.
*Effort:* S. *Risk:* none. *Gate:* acceptance TSV, wasm smoke (`wasm_smoke.mjs`).

### P1 — substantial features

**P1-1. Rational-expression toolkit: `cancel` (+ `together`), poly GCD/div as verbs.**
*Story:* `(x^2-1)/(x-1)` → `x+1`; `1/x + 1/y` → `(x+y)/(x*y)` on request.
*Evidence:* §1 rational rows. *Sketch:* univariate Euclidean GCD over Q[x] via
`polynomial_coefficients` + the synthetic division already in the solver's
rational-root path; `cancel` = divide num/den by GCD (formal cancellation —
same doctrine as `x/x → 1`, §12). Ship as an explicit command **not** inside
`simplify` (domain change; §7 conservatism), exactly like SymPy separates
`cancel` from `simplify`. Multivariate: content extraction only (best-effort,
like `factor`). *Effort:* M. *Risk:* low-medium. *Gate:* differential eval at
random points, acceptance.

**P1-2. `factor` upgrades: recursion, symbol patterns, deg ≥ 3.**
*Story:* `x^3 - x` → `x*(x-1)*(x+1)`; `x^2 - y^2` → `(x-y)*(x+y)`;
`x^3 - 1` → `(x-1)*(x^2+x+1)`.
*Evidence:* §1 factor rows. *Sketch:* (a) after `factor_common`, recurse into
the residual Add; (b) reuse the solver's rational-root peeling (§9 step 2) to
split numeric-coefficient polys of degree ≤ 6 into linear × residual factors;
(c) two-term patterns over arbitrary exprs: `A^2 - B^2`, `A^3 ± B^3` where A,B
are perfect powers/monomials; (d) grouping for 4-term Adds. All best-effort,
never-throw (§7 contract preserved). *Effort:* M. *Risk:* low. *Gate:*
`expand(factor(e)) == expand(e)` property fuzz, acceptance.

**P1-3. Explicit transform verbs: `trigexpand` / `trigcombine`, `logexpand` / `logcombine`.**
*Story:* `trigexpand sin(2x)` → `2*sin(x)*cos(x)`; `logcombine ln(x)+ln(y)` →
`ln(x*y)` (with the documented domain caveat).
*Evidence:* `sin(2x) - 2*sin(x)*cos(x)` doesn't cancel; §7 *correctly* keeps
these out of `simplify` (fixpoint-fighting, domain) — so give them their own
verbs, which the DESIGN's non-rules explicitly leave room for. *Sketch:*
one-pass top-down rewriters (not fixpoint rules): double/sum-angle expansion
for sin/cos/tan of integer-linear args; product-to-sum for combine; log rules
with "assumes positive arguments" warning strings (reuse the solver's warning
doctrine). New header or `simplify.hpp` additions; CLI/REPL/wasm verbs.
*Effort:* M each direction. *Risk:* medium for `combine` (pattern coverage
creep — timebox to the standard identities). *Gate:* differential eval fuzzer
restricted to domain-valid sample points, idempotence *not* required (they are
one-shot transforms — document that).

**P1-4. `partfrac` command.**
*Story:* `partfrac "1/(x^2-1)"` → `1/(2*(x-1)) - 1/(2*(x+1))`.
*Evidence:* the machinery exists and is battle-tested inside `integrate`
(`method: partial fractions` probe) but unreachable. *Sketch:* extract the
integrator's stage-6 decomposition (integrate.cpp) into a public function;
same input contract (rational Number coefficients, den degree ≤ 6). *Effort:*
S-M (mostly refactor). *Risk:* low. *Gate:* existing integrator tests keep
passing; new acceptance rows.

**P1-5. Inequalities end-to-end.**
*Story:* `solve "x^2 < 4"` → `-2 < x < 2`. Day-one Symbolab feature.
*Evidence:* `<` is a lex error; `\le` unknown. *Sketch:* lex `< > <= >= ≤ ≥ ≠`;
a `Relation { Expr lhs; RelOp op; Expr rhs; }` **alongside** `Equation` — a
struct outside the Expr AST, so the 7-node canonical form (§2) is untouched.
Solve: polynomial path → roots split the line, sign-test each interval
(evaluator); isolation path → track direction flips on negative
multiplication / decreasing functions. Output: interval strings + LaTeX. New
result type in CLI/wasm. *Effort:* L. *Risk:* medium (many edge cases —
restrict v1 to polynomial + single-isolation, `Unsolved` otherwise, honest per
the solver doctrine). *Gate:* sign-test verification at sampled points (same
philosophy as §9.5), acceptance.

**P1-6. Assumptions v1: `x > 0` unlocking `sqrt(x^2) = x`.**
*Story:* `simplify "sqrt(x^2)" --assume x>0` → `x`; engineers stop fighting
`abs`. *Evidence:* no mechanism exists; `provably_nonneg` (simplify.cpp:594)
is exactly the extension point. *Sketch:* `Assumptions` = set of
symbol→{positive, nonnegative, nonzero}; new overload
`simplify(e, const Assumptions&)` (default-empty keeps the §7 API frozen —
additive, no signature break); `provably_nonneg` consults it; `abs(u)`,
even-root, and `(u^even)^b` rules pick it up for free. CLI `--assume x>0`,
repeatable; wasm passthrough. Explicitly **not** a general inference system —
symbol-level facts only. *Effort:* M. *Risk:* low if kept symbol-level.
*Gate:* differential fuzzer with samples restricted to the assumed region.

**P1-7. Symbolic-exponent power combining in simplify.**
*Story:* `2^x * 2^x` → `2^(2x)`; `e^x * e^y` → `e^(x+y)`; `e^(2*ln x)` → `x^2`.
*Evidence:* all unchanged today (§1). *Sketch:* extend `apply_mul_rules`'
grouping to factors whose `Pow` exponents are symbolic when the *base* is a
positive Number, `e`, or provably-nonneg expr (value-safe on ℝ; for other
bases combining can only *extend* domain, which §7 permits — mirror the
`(u^a)^b` case analysis). Add Pow rule `Pow(E, Mul(c, Ln(u)))` → `Pow(u, c)`
(domain extension, allowed). *Effort:* S-M. *Risk:* medium — §7 currently
*forbids* symbolic-exponent merging implicitly (only numeric exponents
combine); amend the rule inventory with the same defined/undefined case table
used for `(u^a)^b`. *Gate:* 40k differential fuzz, idempotence property.

### P2 — architectural

- **P2-1 Multi-letter variables (full).** Violates §4's greedy segmentation
  and breaks `xy` = x·y. If ever done: opt-in quoting (`\text{speed}` /
  `"speed"`) mapping to a Symbol with a multi-char name — printer already
  handles arbitrary symbol names (`x_max`), so round-trip survives; implicit
  mult ambiguity is confined to the quoted form. Also auto-subscript `v1` ≡
  `v_1` (kills the `v1` → `v` trap, but value-changing: `x2` today is `2x`).
  M-L, needs a deliberate doctrine rewrite.
- **P2-2 Factorial / floor / ceil.** New FunctionIds — touches the §2 enum,
  evaluator, printer, derivative (undefined/piecewise), integrate tables, and
  every switch. Mechanical but wide; factorial folding for integer args, else
  symbolic. M-L. Worth it only after P0/P1 land.
- **P2-3 Summation/product binders (`\sum`).** Requires a binder node — a
  genuine 8th node kind; ripples through every visitor, fuzzer, and invariant.
  L. Defer until demanded.
- **P2-4 Big rationals.** `2^100` throws today (documented §12). Swapping
  `long long` for a checked big-int type behind the `Rational` API is
  contained by design (§3's overflow doctrine becomes mostly moot) but
  touches perf and every overflow test. L.
- **P2-5 Assumptions v2 / piecewise results.** `solve` returning
  condition-tagged solutions, piecewise `abs` integration. L.

---

## 4. Recommended next release — v0.4 "Forgiving Input"

Theme: the web box must never punish reasonable input. Ship, in this order:

1. **P0-1 Unicode pack** — the entire mobile-keyboard failure class.
2. **P0-2 `|x|` bars + LaTeX Abs printer fix** — top student notation, and the
   printer fix is needed for correct KaTeX rendering in the web app anyway.
3. **P0-3 Scientific notation** (with DESIGN §4/§12 + GRAMMAR amendments).
4. **P0-4 Word-variable guard** — converts the worst silent misparses into
   teaching errors; pairs with a web-side "did you mean" display.
5. **P0-5 Radical normal form** — the most visible simplifier weakness in
   every answer the web app renders.
6. **P0-6 log fold** — one rule, fixes an embarrassing `solve` answer.
7. **P0-7 `subs` + `collect` verbs** — zero engine risk, immediate CAS
   credibility, gives the web UI two new buttons for free.

Rationale: every item is S/S-M except P0-5 (M); all are gated by existing
fuzzers/acceptance; none adds a node kind or breaks the §5 round-trip. The cut
deliberately excludes inequalities (P1-5, L — the flagship of v0.5 together
with `cancel` + `factor` upgrades and assumptions v1). CLI users lose nothing;
web users stop bouncing in the first five minutes. Items 3 and 4 change
documented behavior — land them with the doc amendments and a CHANGELOG note,
and re-baseline `tests/acceptance/cases.tsv` in the same commit.

---

## 5. Explicit non-goals (and why)

- **Complex numbers (`3+2i`, `e^(i*pi)`).** Violates the real-domain doctrine
  (§6, §12) that the evaluator, solver verification (§9.5), and integrator
  self-verification all lean on. `i` stays an ordinary symbol; quadratics with
  negative discriminant keep saying "no real solutions". Revisit only as a
  major-version domain decision, never as a parser patch.
- **Matrices / vectors / tuples.** Needs new node kinds and n-ary function
  args — breaks the §2 seven-node form and "Function has exactly 1 arg". The
  linear-system CLI (`;`-separated equations) already covers the main student
  need.
- **Mixed numbers (`3 1/2`).** Irreconcilable with implicit multiplication
  (`3 1/2` *must* stay `3·1/2` or `2(x+1)` breaks). Document it; the web UI
  can warn on the `int int/int` pattern client-side.
- **`f(x) = ...` user function definitions.** An environment/binding concept,
  not an expression; the REPL is stateless by design. A future REPL-level
  `let` could sit above the parser without touching it.
- **General multi-letter identifiers by default.** See P2-1 — silently
  breaking `xy`/`2pir` for the existing corpus is worse than the guard error.
- **log expansion inside `simplify`.** §7 excludes it for fixpoint and domain
  reasons — the probes confirm nothing is broken, users just need the
  *explicit* verbs (P1-3). Keep the exclusion.
- **Risch-style integration completeness, symbolic ODEs, limits.** Out of
  scope for the parser/simplifier track this roadmap covers.
