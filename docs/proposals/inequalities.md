# Proposal: Solving Inequalities (ROADMAP P1-5, v0.5 candidate)

Status: **proposal** — not implemented. Size: **L** (per docs/ROADMAP.md §3).
This document is written to DESIGN.md's contract standard: when this ships,
its normative sections are folded into DESIGN.md (amendment list in §13) and
the headers must mirror them exactly.

Goal: `solve "x^2 < 4"` answers `-2 < x < 2`; `solve "x^2 >= 4"` answers
`(-inf, -2] U [2, inf)`. Day-one Symbolab input, currently a hard parse error.

---

## 1. Current behavior (probed 2026-07-16, `build/mathsolver`, v0.3.0)

Verbatim, all exit code 1:

```
$ mathsolver solve "x^2 < 4"
error: unexpected character '<'
    x^2 < 4
        ^

$ mathsolver solve "x^2 ≤ 4"
error: inequalities are not supported yet (only '=' equations)
    x^2 ≤ 4
        ^~~

$ mathsolver simplify "x >= 2"
error: unexpected character '>'
    x >= 2
      ^

$ mathsolver solve "x^2 \le 4"
error: unknown command '\le'
    x^2 \le 4
        ^~~

$ mathsolver solve "x != 2"
error: unexpected character '!'
    x != 2
      ^
```

The unicode `≤ ≥ ≠` case is the deliberate targeted error from the v0.4
unicode pack (src/parser.cpp:404-405, documented in docs/GRAMMAR.md "Unicode
input"). This proposal replaces that placeholder with real parsing. The ASCII
`< > <= >= !=` cases are plain unexpected-character errors today.

For output-shape reference, the equation solver today prints:

```
$ mathsolver solve "x^2 = 4"
x = -2
x = 2
method: quadratic formula

$ mathsolver solve "sin(x) = 1/2"
x = pi/6    (principal solution; general: x = pi/6 + 2*pi*n or x = pi - pi/6 + 2*pi*n)
method: isolation

$ mathsolver solve "x^5 + x = 3"
x ≈ 1.1329975658846
method: numeric (Newton/bisection)
warning: numeric search covered [-100, 100]; roots outside this interval are not reported
```

---

## 2. The `Relation` type (expr.hpp)

```cpp
enum class RelOp { Lt, Le, Gt, Ge, Ne };   // <  <=  >  >=  !=

/// A relation lhs REL rhs. Not an Expr node (same doctrine as Equation).
struct Relation {
    Expr lhs;
    RelOp op;
    Expr rhs;
};

/// "<", "<=", ">", ">=", "!=" — the Plain spellings.
std::string_view rel_op_symbol(RelOp op);
```

`Relation` lives in `include/mathsolver/expr.hpp` immediately beside
`Equation` (expr.hpp:135-139) and is a plain struct, **not** a `Kind`.

**Why not an Expr node** (this mirrors exactly why `Equation` stayed out of
the AST — DESIGN.md §2 "Equation is a simple struct, not an Expr node"):

- A relation is not a real-valued expression. Every §2 invariant, every
  factory fold, `compare_expr`, `simplify`, `differentiate`, `evaluate`,
  `substitute`, and both fuzzers are written against a closed 7-kind algebra
  over ℝ. `Add(Relation, 1)` is meaningless; admitting an 8th kind would
  force every visitor and both fuzz generators to grow a case they can never
  produce a value for.
- Relations never nest (`(x < 4) < 2` is not math this tool does), so the
  recursive shared-pointer machinery buys nothing.
- Keeping it out preserves the §5/§11 round-trip invariant's scope unchanged:
  the round-trip fuzzer keeps generating Exprs and stays authoritative
  without modification.
- The precedent works: `Equation` has needed zero special-casing in the AST
  layer through four releases.

The struct is direction-preserving: the parser never normalizes `>` to `<`
(the printer must reproduce the user's direction; the round-trip test for
relations, §11, depends on it). Normalization to a canonical direction is the
solver's private business.

**No AST change ⇒ no change** to `compare_expr`, `hash_expr`,
`structurally_equal`, factories, simplify rule inventory, derivative,
evaluator, or integrate. That is the load-bearing design decision of this
proposal.

### simplify overload (simplify.hpp)

```cpp
Relation simplify(const Relation& rel);   // simplify both sides; op unchanged
```

Mirrors `simplify(Equation)`. It never moves terms across the operator and
never multiplies/divides sides (direction safety is solve's job, §6).

---

## 3. Parser changes (parser.hpp / src/parser.cpp)

### 3.1 Tokens

New `Tok` values: `Lt, Le, Gt, Ge, Ne` (alongside `Equals` in the enum at
src/parser.cpp:23).

| Input | Token | Notes |
|---|---|---|
| `<` | `Lt` | |
| `<=` | `Le` | maximal munch: `<` followed immediately by `=` |
| `>` | `Gt` | |
| `>=` | `Ge` | |
| `!=` | `Ne` | `!` **not** followed by `=` keeps today's `unexpected character '!'` (factorial stays MISSING per ROADMAP §1) |
| `≤` `≥` `≠` (U+2264/2265/2260) | `Le` `Ge` `Ne` | **replaces** the targeted "inequalities are not supported yet" error at src/parser.cpp:404-405; token carries the true multi-byte span so caret diagnostics keep working (v0.4 unicode-pack doctrine: no pre-normalization) |
| `\le` `\leq` `\ge` `\geq` `\ne` `\neq` `\lt` `\gt` | `Le Le Ge Ge Ne Ne Lt Gt` | added to the LaTeX command table; today all are `unknown command` |

No other lexer behavior changes. `=<` and `=>` are *not* recognized (the `=`
lexes as `Equals` and the parse fails at the second operator with the §3.2
error — acceptable; do not add sugar).

### 3.2 Grammar and precedence

The §4 grammar's top rule changes from `input := expr ( '=' expr )?` to:

```
input  := expr ( relop expr )?
relop  := '=' | '<' | '<=' | '>' | '>=' | '!='
```

- Relational operators live on **exactly the level of `=`**: loosest of all,
  top level only, never inside parentheses/braces/brackets or a `\left...\right`
  group (`(x < 4)` → `unexpected '<'`, same as `(x = 4)` today).
- **At most one comparison per input**, counting `=` and relops together.
  A second comparison operator is a ParseError over its span with the message
  `at most one '=' or comparison per input` — this covers `a = b < c`,
  `x < y = z`, and the chained `a < x < b` (chained inequalities are a
  non-goal, §10; the error is the honest placeholder, exactly as the unicode
  `≤` error was for this feature).
- Implicit multiplication is unaffected: relops are not atom-starters, so
  they terminate a term just like `=` does (§4: "never triggers at
  `+ - * / ^ = ...`"). Bare-argument functions also stop at a relop:
  `\sin x < 1` parses as `sin(x) < 1`.

### 3.3 API (parser.hpp)

```cpp
/// Parse a bare expression, an equation, or a relation.
std::variant<Expr, Equation, Relation> parse_input(std::string_view src);

/// Parse a relation; an expression, equation, or malformed input throws.
Relation parse_relation(std::string_view src);
```

`parse_input`'s return variant grows a third alternative. This is
API-additive for the `std::holds_alternative`/`std::get` style used by every
in-repo caller (apps/main.cpp, wasm/bindings.cpp `equation_from`/`ms_analyze`,
tests), but any exhaustive two-arm `std::visit` would stop compiling — the
implementation stage must audit callers (a compile error, not a silent
change). `parse_expression` and `parse_equation` are unchanged;
`parse_equation` on a relation throws ParseError over the operator span:
`expected '=', found '<='` (existing message style).

### 3.4 GRAMMAR.md amendments (same commit)

"Unicode input" table: the `≤ ≥ ≠` row changes from the targeted-error note
to real meanings. "Equations" section becomes "Equations and inequalities"
with the one-comparison rule. Pitfalls: add `!` alone is still an error;
`a < b < c` is rejected. Operator table: relops join level 1.

---

## 4. Solution representation (inequality.hpp) — the contract

New header `include/mathsolver/inequality.hpp`, implementation
`src/inequality.cpp`. Module map row (DESIGN.md §1):

| Module | Files | Depends on |
|---|---|---|
| inequality | `inequality.hpp/.cpp` | expr, printer, evaluator, simplify, derivative, solver |

```cpp
namespace mathsolver {

/// One endpoint of an interval. `value == nullptr` means unbounded
/// (-infinity for a lo endpoint, +infinity for a hi endpoint).
struct IntervalEnd {
    Expr value;          ///< exact Expr (e.g. sqrt(2), -1/2, pi/6), or null.
    bool open = true;    ///< true: endpoint excluded; false: included.
    bool exact = true;   ///< false when the endpoint came from the numeric
                         ///< fallback (a decimal-converted Number, §6.5).
};

/// A nonempty real interval. Degenerate single points are lo == hi with
/// both ends closed ({0} is {0, closed, 0, closed}).
struct Interval {
    IntervalEnd lo, hi;
};

/// A finite union of disjoint intervals, in normal form (§4.1).
struct SolutionSet {
    std::vector<Interval> intervals;   ///< sorted ascending, pairwise
                                       ///< disjoint, non-mergeable.
};

struct InequalitySolveResult {
    enum class Status {
        Solved,          ///< exact interval set found.
        NumericOnly,     ///< set found, some endpoints approximate.
        NoRealSolution,  ///< provably empty (e.g. x^2 < -1).
        AllReals,        ///< every real value satisfies the relation.
        Unsolved         ///< could not solve; see warnings.
    };
    Status status = Status::Unsolved;
    SolutionSet set;     ///< meaningful for Solved / NumericOnly only;
                         ///< empty otherwise (status is authoritative).
    std::string method;  ///< e.g. "interval sign test (quadratic formula)",
                         ///< "linear isolation".
    std::vector<std::string> warnings;
};

InequalitySolveResult solve_inequality(const Relation& rel,
                                       std::string_view symbol,
                                       const NumericOptions& opts = {});

/// "(-inf, -2] U [2, inf)" / "\left(-\infty, -2\right] \cup \left[2, \infty\right)".
std::string to_string(const SolutionSet& s, PrintStyle style = PrintStyle::Plain);

/// Chained rendering of ONE interval against a variable name:
/// "-2 < x <= 2", "x >= 2", "x = 0" (degenerate). Both styles.
std::string to_chained_string(const Interval& iv, std::string_view symbol,
                              PrintStyle style = PrintStyle::Plain);

} // namespace mathsolver
```

Statuses reuse the §9 `SolveResult` names deliberately so every surface
(CLI wording, wasm status strings, web components) stays uniform:
`NoRealSolution` prints `no real solutions`, `AllReals` prints
`true for all x`, exactly as for equations.

### 4.1 SolutionSet normal form (invariants)

Every `SolutionSet` published by `solve_inequality` satisfies:

1. **Ordered:** intervals sorted by `evaluate(lo)` ascending (null lo first);
   within each interval `evaluate(lo) < evaluate(hi)`, or `lo == hi`
   (structurally equal Exprs) with both ends closed (a point).
2. **Disjoint and non-adjacent:** no two intervals overlap, and two intervals
   sharing an endpoint value with the shared endpoint **included on either
   side** have been merged. Adjacent intervals both *open* at the shared
   value stay separate — that is the punctured-line shape
   (`(x-1)^2 > 0` → `(-inf, 1) U (1, inf)`), which is irreducible.
3. **Endpoint Exprs are exact and simplified** (`sqrt(2)`, `pi/6`, `-1/2`)
   when `exact == true`; decimal-converted Numbers when `exact == false`.
   Unbounded ends are null Exprs — there is **no** infinity Expr node and no
   `inf` token in the grammar; infinity exists only in this struct and in
   printed output (output-only, never re-parsed — see §11 on round-trip
   scope).
4. **Verified:** the set has passed the §6.6 sign-test verification before
   being returned with status Solved/NumericOnly.
5. Consequence of 1+2: `AllReals` is never encoded as `{(-inf, inf)}` inside
   a Solved result — the assembly step (§6.4) detects the full line and
   returns status `AllReals` with an empty `intervals` vector; likewise an
   empty union becomes `NoRealSolution`. One representation per answer.

### 4.2 Worked representation examples

| Input | SolutionSet | Printed (Plain set notation) |
|---|---|---|
| `x^2 < 4` | {((-2 open), (2 open))} | `(-2, 2)` |
| `x^2 <= 4` | {((-2 closed), (2 closed))} | `[-2, 2]` |
| `x^2 >= 4` | {((null), (-2 closed)), ((2 closed), (null))} | `(-inf, -2] U [2, inf)` |
| `x^2 <= 0` | {((0 closed), (0 closed))} | `{0}` |
| `(x-1)^2 > 0` | {((null), (1 open)), ((1 open), (null))} | `(-inf, 1) U (1, inf)` |
| `x^2 != 4` | three open intervals | `(-inf, -2) U (-2, 2) U (2, inf)` |
| `x^2 < 2` | endpoints `-sqrt(2)`, `sqrt(2)` (exact Exprs) | `(-sqrt(2), sqrt(2))` |

---

## 5. Rendering (printer.hpp + inequality.hpp)

### 5.1 `to_string(const Relation&, PrintStyle)` (printer.hpp)

Added beside `to_string(const Equation&)`. `lhs OP rhs` with each side
printed by the existing Expr printer.

| RelOp | Plain | LaTeX |
|---|---|---|
| Lt | `<` | `<` |
| Le | `<=` | `\le` |
| Gt | `>` | `>` |
| Ge | `>=` | `\ge` |
| Ne | `!=` | `\ne` |

Round-trip invariant (tested, §11): `parse_relation(to_string(r, style))`
yields structurally equal sides and the same `op`, both styles. This is why
the parser must accept both the ASCII digraphs and the `\le`-family commands.

### 5.2 Interval-set rendering (inequality.cpp)

**Set notation**, `to_string(SolutionSet, style)`:

- Interval: `(` / `[` + lo + `, ` + hi + `)` / `]`, endpoints via the Expr
  printer in the requested style; unbounded ends print `-inf` / `inf`
  (Plain) and `-\infty` / `\infty` (LaTeX).
- Degenerate interval: `{v}` (Plain), `\{v\}` (LaTeX).
- Union separator: ` U ` (Plain), ` \cup ` (LaTeX).
- LaTeX brackets are `\left(`/`\right]` etc., matching the §5 house style of
  always using `\left`/`\right` for grouping the web app KaTeX-renders.
- Empty set / full line are never rendered by this function (§4.1 inv. 5:
  those are statuses, and each surface already has words for them).

**Chained notation**, `to_chained_string(iv, "x", style)` — the
human-facing form, one interval at a time:

| Interval shape | Plain rendering |
|---|---|
| bounded | `-2 < x < 2`, `-2 <= x < 2` (each `<`/`<=` independently by that end's open flag) |
| ray, lo unbounded | `x < 2` / `x <= 2` |
| ray, hi unbounded | `x > -2` / `x >= -2` |
| degenerate | `x = 0` |

LaTeX: same structure with `\le`. Chained strings always use `<`-direction
(smallest on the left) regardless of the input relation's direction.

`inf`, `U`, `{...}`, and chained strings are **output-only**: they are not in
the grammar, and no round-trip requirement attaches to them (explicitly out
of the round-trip fuzzer's jurisdiction — it generates Exprs and, after this
ships, Relations; never SolutionSets).

---

## 6. Solving strategy (src/inequality.cpp)

Pipeline over `f = simplify(lhs - rhs)` and the (possibly flipped) operator:
rewrite `rel` as `f REL 0` — subtraction is direction-safe, so `REL` is just
`rel.op`. `Gt`/`Ge` are then normalized internally to `Lt`/`Le` on `-f`
(direction flip under negation), so the core only reasons about
`f < 0`, `f <= 0`, `f != 0`.

### 6.1 Step 1 — trivial cases (mirrors §9 step 1)

`f` free of `symbol`:

- `try_exact_numeric(f)` (or a clean `evaluate` for pi/e shapes) yields a
  number `v`: the relation is a ground truth — `v REL 0` true → `AllReals`
  ("identity"; `3 < 4`), false → `NoRealSolution` ("contradiction"; `5 < 4`).
- Otherwise (symbolic parameters) → `Unsolved` with a note naming them.

### 6.2 Step 2 — linear isolation special case

If `polynomial_coefficients(f, x)` yields degree 1 with a **numeric** leading
coefficient `c1` (constant term `c0` may be symbolic): solution is the ray at
`b = simplify(-c0/c1)`, direction flipped when `c1 < 0`, endpoint open iff
the relation is strict, `Ne` → two open rays around `b`. Method
`"linear isolation"`. This is the one path where **endpoints may contain
symbolic parameters** (`2x + a < 0` → `(-inf, -a/2)`); it is sound without
sampling because the direction argument needs only `sign(c1)`. If `c1` is
symbolic → fall through (and ultimately `Unsolved` via §6.7 — v1 does not do
sign case analysis on parameters).

### 6.3 Step 3 — boundary roots + breakpoints

**Boundary equation.** Call the existing `solve()` (§9, unmodified) on
`f = 0`:

- `Solved` → exact boundary roots (Exprs).
- `NumericOnly` → approximate boundary roots (decimal Numbers) — §6.5.
- `NoRealSolution` → no roots: the line is one candidate interval.
- `AllReals` (`f ≡ 0`): `f <= 0` → `AllReals`; `f < 0` and `f != 0` →
  `NoRealSolution`.
- `Unsolved` → `Unsolved`, forwarding solve's warnings plus
  `"could not solve the boundary equation <f> = 0"`.
- Any solution whose `note` reports a periodic general family → §6.8.

**Additional breakpoints (domain awareness).** Roots alone do not partition
correctly when `f` has poles or domain edges (`1/x < 0` has no roots at all).
Collect, by walking `f`'s canonical form, every maximal subexpression:

- `Pow(base, e)` with negative exponent `e` (a denominator): solve
  `base = 0` → **pole breakpoints**;
- `Pow(u, p/q)` with even `q` (an even root): solve `u = 0` → **domain-edge
  breakpoints** (the region `u < 0` is handled by sampling — it evaluates to
  EvalError and drops, §6.4);
- `Ln(u)`: solve `u = 0` → domain-edge breakpoints;
- `Tan(u)` in v1: → `Unsolved` with warning
  `"tan has poles the v1 sign test does not locate"` **unless** `f`'s symbol
  occurrences make §6.8's periodic mode applicable.

Each auxiliary `solve()` call that returns `Unsolved` for a base that
actually contains `x` → whole result `Unsolved` (an unlocated pole makes the
sign chart unsound; the §6.6 consistency check is the backstop, but we do not
knowingly proceed). Numeric-only auxiliary roots follow §6.5.

### 6.4 Step 4 — partition, sample, assemble

1. **Order the breakpoints**: evaluate each root/breakpoint Expr to a double
   (`evaluate`, no bindings needed — they are symbol-free by construction;
   an EvalError here → `Unsolved` with a warning). Sort ascending; dedupe
   pairs within `1e-9·max(1,|v|)` — if two deduped candidates are *not*
   `structurally_equal` and `simplify(a - b)` is not the Number 0, keep one
   and add the warning `"two boundary points are numerically
   indistinguishable"`.
2. **Candidate intervals**: `(-inf, b1), (b1, b2), …, (bn, inf)` (open; the
   endpoints' fate is decided in step 4). No breakpoints → the single
   candidate `(-inf, inf)`.
3. **Sign-test each interval** at up to 3 interior sample points (positions
   1/4, 1/2, 3/4 of the interval in double space; for unbounded ends use
   `b1 - 1, b1 - 10, b1 - 100` / symmetric). Evaluate `f` at each:
   - all evaluable samples have the same strict sign → interval is **in**
     (sign satisfies `REL`) or **out**;
   - every sample throws EvalError → the interval lies outside `f`'s domain
     → **out**, with the note-level warning
     `"<interval> is outside the domain of <f>"` added once;
   - evaluable samples with **conflicting signs**, or a sample that is
     numerically zero (`|v| ≤ 1e-9·scale`) → the partition missed a feature
     → `Unsolved` with warning `"sign is not constant between <a> and <b>;
     a boundary point may be missing"`. (Never guess — the §8b/§9
     honesty doctrine.)
4. **Endpoint inclusion**:
   - Endpoints that are **roots of `f`** (came from the boundary solve):
     included iff the relation is non-strict (`Le`/`Ge`); `Lt`/`Gt`/`Ne` →
     excluded. (`f` is exactly 0 there by the solver's own verification.)
   - **Pole and domain-edge breakpoints**: decided by direct evaluation —
     compute `r = simplify(substitute(f, x, b))`; if `try_exact_numeric(r)`
     (or a clean `evaluate(r)`) yields a value and that value satisfies
     `REL 0` → included; EvalError or unsatisfied → excluded. This single
     rule closes `sqrt(x) < 1` at 0 (f(0) = −1, defined and satisfying)
     and keeps every pole open (EvalError).
   - Unbounded ends are always open by construction.
5. **Normalize** (§4.1): merge adjacent "in" intervals when the shared
   endpoint is included; a lone included endpoint between two "out"
   intervals becomes a degenerate point interval (`x^2 <= 0` → `{0}`);
   the full line → status `AllReals`; nothing in → `NoRealSolution`.

`Ne` is just the strict case with the "satisfies" predicate `v != 0`: the
complement of the boundary points within the domain, which the machinery
above produces naturally (every interval is in; every root endpoint is
excluded).

`method` is `"interval sign test (<boundary method>)"`, where
`<boundary method>` is the inner `solve()`'s method label —
e.g. `interval sign test (quadratic formula)`,
`interval sign test (numeric (Newton/bisection))`.

Any `OverflowError` escaping the exact arithmetic anywhere in the pipeline →
`Unsolved` with `"coefficient arithmetic overflowed 64-bit rationals"`
(the §9b doctrine; never let it propagate out of `solve_inequality`).

### 6.5 Numeric boundary roots (NumericOnly endpoints)

When the boundary solve (or any auxiliary breakpoint solve) returns
`NumericOnly`:

- The affected endpoints carry `exact = false` (their Exprs are the
  solver's decimal-converted Numbers).
- Result status is `NumericOnly` (even if other endpoints are exact — one
  approximate endpoint taints the set's exactness claim).
- Warnings: `"interval endpoints marked ≈ are numeric approximations"` plus
  the inherited scan-window warning, rephrased for sets:
  `"boundary roots were searched numerically on [<lo>, <hi>]; the solution
  set assumes no further boundary points outside this interval"`. The set is
  **not** clamped to the window — the unbounded rays are reported, and the
  warning states the assumption honestly (same contract as the equation
  solver's "roots outside [lo, hi] are not reported").
- Strictness at approximate endpoints is applied per the same rules as
  §6.4.4 — with the caveat inherited from §9's tangency-root doctrine: a
  root carrying the tangency note ("no sign change observed") produces an
  endpoint whose *membership for non-strict relations is uncertain*; keep
  the §9 note text as an endpoint-level warning.

### 6.6 Verification (the §9.5 analogue — mandatory)

Before returning `Solved`/`NumericOnly`, verify the assembled set against
the original relation:

- Sample up to 3 interior points of every reported interval: the relation
  (`evaluate(f)` vs 0 with the strictness predicate) must **hold** at every
  evaluable sample.
- Sample up to 3 interior points of every gap (the complement intervals
  between/around the reported ones): the relation must **fail** at every
  evaluable sample (EvalError counts as failing — outside the domain is
  outside the set).
- A strict-tolerance zone `|f| ≤ 1e-9·scale` at a sample → skip that sample
  (too close to a boundary to classify).
- Any clean counterexample → **demote to `Unsolved`** with warning
  `"the candidate solution set failed verification"` — never publish an
  unverified set (the §8b self-verification doctrine, applied to sets).
- All samples of some interval skipped/EvalError on the *in* side → keep,
  with `"could not verify <interval> numerically"`.

### 6.7 Fallthrough

Anything the pipeline cannot handle — symbolic parameters surviving to the
sampling stage (samples need every symbol bound except `x`; mirror §9 step
4's precondition: `free_symbols(f)` must be exactly `{x}` by then, else
`Unsolved` warning naming the extras), unlocated poles, mixed
trig/exponential boundaries with `Unsolved` boundary equations — returns
`Unsolved` with specific warnings. v1 makes no attempt at direction-tracking
isolation through nonlinear layers (ROADMAP sketched it; the sign-test
covers the same ground more uniformly, and what it can't cover honestly
fails).

### 6.8 Periodic boundaries (`sin(x) < 1/2`)

The honest problem: `sin(x) = 1/2` has infinitely many roots, so the true
solution set is an infinite union — not representable in §4's finite normal
form. The equation solver already handles the analogous situation by
reporting *principal* solutions plus a general-family `note` (§9 step 3,
"Periodicity is reported per-root"). The inequality analogue:

**v1 scope:** periodic mode engages only when `f`'s every occurrence of `x`
is inside a **single** application `sin(u)`, `cos(u)`, or `tan(u)` with
`u = a*x + b`, `a` a nonzero Number, `b` symbol-free (the same shape §9's
isolation handles). Anything else whose boundary equation is periodic →
`Unsolved` with warning `"the boundary equation has infinitely many
solutions; only single-trig inequalities are supported"`.

Procedure:

1. Reporting window: `W = [x0, x0 + P/|a|)` where `P` is the trig period
   (2π for sin/cos, π for tan) and `x0 = -b/a` shifted so that `W` maps onto
   one full period of the inner function starting at phase 0; for the common
   `u = x` this is simply `[0, 2*pi)` (`[0, pi)` for tan).
2. Enumerate every boundary point in `W` from the principal roots' general
   families (`asin` branch pairs, `±acos`, `atan + pi*n`); for tan also the
   pole `pi/2` (within-window pole breakpoint, always-open endpoint).
3. Partition `W`, sign-test, assemble, and verify exactly per §6.4/§6.6 —
   the window endpoints themselves are half-open by construction (`x0`
   closed-tested like a domain edge, `x0 + P/|a|` excluded as the period
   seam).
4. Status `Solved` (or `NumericOnly`), with the mandatory warning stating
   the window and the family, mirroring §9's exact-general-solution style:
   `"the boundary is periodic: the set shown covers one period [0, 2*pi);
   the full solution adds 2*pi*n to every endpoint"` (period and stride
   printed via the Plain printer; for `u = a*x + b` the stride is
   `2*pi/|a|` etc.).

Worked example — `sin(x) < 1/2`:

- boundary roots in `[0, 2*pi)`: `pi/6`, `5*pi/6` (from the asin family);
- sign test: `[0, pi/6)` in (f(0) = −1/2 < 0), `(pi/6, 5*pi/6)` out,
  `(5*pi/6, 2*pi)` in;
- endpoints: strict → both roots open; `0` closed (window edge, f(0)
  satisfies); `2*pi` excluded (period seam);
- result: `[0, pi/6) U (5*pi/6, 2*pi)`, warning as above.

This is deliberately a *window report*, not a general solution — the same
honesty trade the equation solver makes with principal-solution notes. A
future parametric-family representation (endpoint Exprs containing `n`) is
explicitly out of scope (§10).

---

## 7. CLI / REPL surface (apps/main.cpp)

`solve` accepts a relation wherever it accepts an equation (the parse-input
dispatch grows a `Relation` arm). `[var]` inference and `--range LO HI`
behave identically (`--range` feeds `NumericOptions` for the boundary
search).

Output — one chained line (§5.2) per interval, then the set line, then the
standard trailer:

```
$ mathsolver solve "x^2 < 4"
-2 < x < 2
set: (-2, 2)
method: interval sign test (quadratic formula)

$ mathsolver solve "x^2 >= 4"
x <= -2
x >= 2
set: (-inf, -2] U [2, inf)
method: interval sign test (quadratic formula)

$ mathsolver solve "x^2 <= 0"
x = 0
set: {0}
method: interval sign test (quadratic formula)

$ mathsolver solve "sin(x) < 1/2"
0 <= x < pi/6
5*pi/6 < x < 2*pi
set: [0, pi/6) U (5*pi/6, 2*pi)
method: interval sign test (isolation)
warning: the boundary is periodic: the set shown covers one period [0, 2*pi); the full solution adds 2*pi*n to every endpoint
```

Multiple interval lines are alternatives (an OR), exactly as multiple
`x = ...` lines are for equations. `NoRealSolution` → `no real solutions`;
`AllReals` → `true for all x`; `Unsolved` → `unable to solve for x` —
byte-identical wording to the equation paths (§10 of DESIGN.md).
Approximate endpoints render their decimal value in both the chained lines
and the set line; the warnings carry the ≈ caveat (no per-line `≈` symbol —
the chained comparison operator slot is already taken).

`--latex` renders the chained lines and set line in LaTeX style
(`\left(-\infty, -2\right] \cup \left[2, \infty\right)`).

Other subcommands: `simplify` and `latex` accept relations (both-sides
transform, §2; `latex "x^2 \le 4"` → `x^{2} \le 4`); `subs` substitutes into
both sides. `diff`, `integrate`, `eval`, `expand`, `factor`, `collect` on a
relation → the same error those commands give an equation today (exit 1).

REPL: a bare relation solves for its single free symbol (mirroring bare
equations, including the "use solve ..., var" nudge for several symbols);
`solve x^2 < 4, x` works via the existing comma-split. The `;`-system path
rejects relations (`solve_system` is equations-only): parse error surfaces
naturally.

---

## 8. WASM / web surface (wasm/bindings.cpp, web/src/lib)

- `ms_analyze`: a relation input returns
  `{"ok":true,"kind":"inequality","symbols":[...],"plain":...,"latex":...}`
  (rendered via `to_string(Relation)`; same shape as `kind:"equation"`).
- `ms_solve`: when the input parses as a Relation, call `solve_inequality`
  and return the envelope (status strings identical to the equation path:
  `"solved" | "numeric" | "noRealSolution" | "allReals" | "unsolved"`):

```json
{"ok":true,"kind":"inequality","status":"solved",
 "method":"interval sign test (quadratic formula)","warnings":[],
 "set":{"plain":"(-inf, -2] U [2, inf)",
        "latex":"\\left(-\\infty, -2\\right] \\cup \\left[2, \\infty\\right)"},
 "intervals":[
   {"plain":"x <= -2","latex":"x \\le -2",
    "lo":null,
    "hi":{"plain":"-2","latex":"-2","approx":-2.0,"open":false,"exact":true}},
   {"plain":"x >= 2","latex":"x \\ge 2",
    "lo":{"plain":"2","latex":"2","approx":2.0,"open":false,"exact":true},
    "hi":null}]}
```

  Equation inputs gain `"kind":"equation"` for symmetry (additive; absent
  field = equation for old clients). Per-interval `plain`/`latex` are the
  chained renderings so the web stays presentation-dumb; `approx` is
  `evaluate()` of the endpoint (null on EvalError), mirroring the existing
  per-solution `approx` field.
- `web/src/lib/engine/types.ts`: extend `AnalyzeResult` with the
  `"inequality"` kind and `SolveResult` with the optional
  `kind`/`set`/`intervals` fields (envelope-mirroring comment discipline:
  "Typed envelopes mirroring wasm/bindings.cpp exactly").
- `web/src/lib/components/ResultSolve.svelte`: render chained interval lines
  via KaTeX (the provided `latex` strings), the set line beneath, warnings
  as today. `ParsePreview` picks up `kind:"inequality"` for free through
  analyze. Region shading in `Plot.svelte` is a non-goal (§10).

---

## 9. Invariants (called out)

1. **No new Expr kind.** The §2 seven-node canonical form, `compare_expr`
   ranking, factory contracts, and every existing fuzzer generator are
   byte-for-byte untouched.
2. **Relation round-trip:** `parse_relation(to_string(r, style))` preserves
   both sides structurally and the operator, for both styles.
3. **SolutionSet normal form** (§4.1): ordered, disjoint, non-mergeable,
   exact-flagged endpoints; AllReals/empty are statuses, never interval
   encodings.
4. **No unverified set is ever published** (§6.6) — the inequality analogue
   of §8b/§9.5.
5. **Direction safety:** the only operations ever applied across a relation
   are simplify-both-sides and subtract-rhs; a multiplication/division by a
   negative or sign-unknown quantity never crosses the operator.
6. **Infinity is representation-only:** no `inf` token, no infinity Expr;
   unbounded ends exist only as null `IntervalEnd.value` and in printed
   output.
7. **Existing solver untouched:** `solve()` is called as a black box; no
   behavior change to any current command or output.

---

## 10. Non-goals (v1)

- **Chained inequalities** (`-2 < x < 4` as *input*): targeted ParseError
  (§3.2). Chained notation is output-only.
- **Systems of inequalities / intersection-union algebra** on SolutionSets
  (no public set operations API).
- **Symbolic-parameter sign analysis** (`a*x < 1` for unknown-sign `a`):
  `Unsolved` with a warning, except the numeric-leading-coefficient linear
  case (§6.2). No condition-tagged piecewise answers (that is ROADMAP
  P2-5's territory).
- **General periodic families as answers:** one-window reports only (§6.8);
  no `n`-parameterized endpoint Exprs.
- **Boolean evaluation of ground relations** (`eval "3 < 4"` → `true`):
  float-tolerance equality for `!=`/`<=` at doubles is a trap; excluded
  until there is a doctrine for it.
- **Integer/discrete domains**, complex domain (real-only per §12),
  region plotting in the web app, and `solve_system` over relations.
- **`!` as factorial** stays an error; only the digraph `!=` lexes.

---

## 11. Testing and verification (gating assets)

New unit file `tests/test_inequality.cpp` (registered in
tests/CMakeLists.txt) plus targeted additions; the standing gates from
REVIEW-NOTES.md all apply:

| Gate | What it checks here |
|---|---|
| **Round-trip fuzzer** (`tools/run_fuzz.sh`, `tools/fuzz_roundtrip.cpp`) | Must stay clean — the Expr grammar/printer changes are zero, so any new failure is a regression from the token additions. **Extend** the generator with a Relation mode: wrap two generated Exprs in a random RelOp, round-trip through both print styles (invariant §9.2). |
| **Differential fuzzers** (simplify/expand, derivative — REVIEW-NOTES) | Re-run unchanged (no simplify/derivative rule changes). New **inequality differential fuzzer**: random polynomial/rational `f` (degree ≤ 4, rational coefficients), random RelOp; compare the reported set against a brute-force membership scan of the relation at 2001 grid points on [-10, 10], skipping points within 1e-6 of any endpoint; every evaluable grid point's membership must match. Statuses AllReals/NoRealSolution checked by full-grid membership. |
| **Acceptance battery** (`tools/run_acceptance.py`, `tests/acceptance/cases.tsv`) | New `ineq-*` rows: every §4.2 and §6.8 worked example (exact/regex per the README semantics — `set:` lines are pinnable with `exact`; multi-line interval output uses the lookahead-regex idiom the solve rows already use), the parse-error rows for `(x < 4)`, `a < b < c`, bare `!`, and the `\le`-family and unicode `≤ ≥ ≠` inputs, plus exit-code checks. |
| **Printer battery** (`tests/acceptance/printer_cases.tsv`) | Relation rendering rows both styles (`x^2 \le 4` → `x^{2} \le 4`). |
| **WASM/browser suite** (`tools/wasm_smoke.mjs`, `tools/wasm_acceptance.mjs`; puppeteer-core available in `web/` for a DOM check) | Extend the acceptance adapter to reconstruct the CLI's inequality output from the JSON envelope (as it does for solve/system today) so the `ineq-*` rows run through the wasm engine unchanged; smoke: one `solveInequality`-path call asserting the envelope shape; a browser-level check that ResultSolve renders the chained KaTeX lines. |
| **Property tests** (Catch2) | SolutionSet normalization idempotence; §6.6 verification actually demotes a deliberately-corrupted set (test hook); endpoint-inclusion rules table-driven over all five RelOps. |

Everything runs via `ctest --test-dir build` and must be green at the end of
every stage (§11 of DESIGN.md).

---

## 12. Staged implementation plan

Order matters: each stage leaves `ctest` green and is independently
landable. Effort: S < 1 day, M = days, L = week+ (ROADMAP convention).

| Stage | Deliverable | Files | Effort |
|---|---|---|---|
| 1 | `Relation`/`RelOp` + `simplify(Relation)` + `to_string(Relation)` both styles + unit tests | `include/mathsolver/expr.hpp`, `include/mathsolver/simplify.hpp`, `src/simplify.cpp`, `include/mathsolver/printer.hpp`, `src/printer.cpp`, `tests/test_expr.cpp`, `tests/test_printer.cpp` | S |
| 2 | Lexer tokens (ASCII digraphs, unicode `≤ ≥ ≠` replacing the targeted error at src/parser.cpp:404, `\le`-family), grammar rule, `parse_relation`, `parse_input` variant extension + caller audit, GRAMMAR.md amendments | `src/parser.cpp`, `include/mathsolver/parser.hpp`, `docs/GRAMMAR.md`, `tests/test_parser.cpp` | M |
| 3 | `inequality.hpp/.cpp`: SolutionSet + rendering (§4, §5.2); solve pipeline §6.1–§6.7 (trivial, linear isolation, boundary+breakpoints, partition/sample/assemble, NumericOnly, verification); unit tests for every worked example | `include/mathsolver/inequality.hpp`, `src/inequality.cpp`, `CMakeLists.txt` (new source), `tests/test_inequality.cpp`, `tests/CMakeLists.txt` | **L** (the bulk: breakpoint collection, endpoint rules, normalization, verification) |
| 4 | Periodic window mode §6.8 | `src/inequality.cpp`, `tests/test_inequality.cpp` | M |
| 5 | CLI + REPL surface §7 (dispatch, output shapes, `--latex`, REPL bare-relation) | `apps/main.cpp`, `tests/test_cli.cpp` | S-M |
| 6 | WASM envelope + web types/rendering §8 | `wasm/bindings.cpp`, `web/src/lib/engine/types.ts`, `web/src/lib/engine/worker.ts`/`index.ts`, `web/src/lib/components/ResultSolve.svelte`, `ParsePreview.svelte` | M |
| 7 | Gates: `ineq-*` acceptance rows, printer rows, fuzzer Relation mode, inequality differential fuzzer, wasm adapter + smoke, DESIGN.md amendments (§13) | `tests/acceptance/cases.tsv`, `tests/acceptance/printer_cases.tsv`, `tools/fuzz_roundtrip.cpp`, new `tools/fuzz_inequality.cpp` (or a run_fuzz.sh mode), `tools/wasm_acceptance.mjs`, `tools/wasm_smoke.mjs`, `DESIGN.md`, `docs/ROADMAP.md` (mark P1-5) | M |

**Total: L** (≈ 1.5–2 weeks of focused work), matching the ROADMAP P1-5
sizing. Stages 1–2 are shippable early (they merely turn today's hard errors
into parseable-but-`Unsolved` inputs if stage 3 lags — acceptable only
behind a `solve` arm that says `unable to solve for x` with an
"inequality solving not yet implemented" warning; prefer landing 1–3
together).

## 13. DESIGN.md amendments (land with the code, per stage)

- §1 module map: `inequality` row (stage 3).
- §2: `Relation` sentence beside the `Equation` one (stage 1).
- §4: token table + top-rule change + one-comparison diagnostic (stage 2).
- §5: `to_string(Relation)` op table; note set/chained rendering lives in
  inequality.hpp and is output-only (stages 1, 3).
- §7: `simplify(Relation)` line (stage 1).
- new §9c: the §4–§6 contracts of this proposal, condensed (stage 3–4).
- §10: `solve` inequality output shapes (stage 5).
- §12 limitations: one-window periodic reports; no chained-inequality input;
  no parameter sign analysis; numeric-boundary sets assume no roots outside
  the scan window (stage 7).
