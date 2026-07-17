# MathSolver — Design Document

MathSolver is a from-scratch computer-algebra system (CAS) in C++23. It parses
LaTeX-style math input, simplifies/augments expressions, differentiates them
symbolically, and solves equations exactly where possible and numerically
otherwise. No third-party dependencies except Catch2 (tests only).

This document is the **contract** between modules. The public headers in
`include/mathsolver/` mirror it; if a header comment and this document
disagree, fix whichever is wrong and note it in the commit/summary.

---

## 1. Module map and dependency order

| Module          | Files                                   | Depends on               |
|-----------------|-----------------------------------------|--------------------------|
| errors          | `errors.hpp` (header-only)              | —                        |
| rational        | `rational.hpp/.cpp`                     | errors                   |
| expr (core AST) | `expr.hpp/.cpp`                         | rational, errors         |
| parser          | `parser.hpp/.cpp`                       | expr                     |
| printer         | `printer.hpp/.cpp`                      | expr                     |
| evaluator       | `evaluator.hpp/.cpp`                    | expr                     |
| simplify        | `simplify.hpp/.cpp`                     | expr                     |
| derivative      | `derivative.hpp/.cpp`                   | expr, simplify           |
| integrate       | `integrate.hpp/.cpp`                    | expr, simplify, derivative, evaluator, solver (partial fractions) |
| solver          | `solver.hpp/.cpp`                       | expr, simplify, derivative, evaluator |
| CLI/REPL        | `apps/main.cpp`                         | everything               |

Build targets: static lib `mathsolver_core` (src/*), executable `mathsolver`
(apps/), test executable `mathsolver_tests` (tests/, Catch2 v3 via
FetchContent, registered with `catch_discover_tests`).

Error handling convention: **exceptions**, all derived from `mathsolver::Error`
(see `errors.hpp`). No `std::expected` in public APIs.

Style: 4-space indent, 100 columns (see `.clang-format`), `snake_case` for
functions/variables, `PascalCase` for types, `k`-prefixed constants. Naming and
signatures in the headers are final — do not rename public API.

---

## 2. Expression representation (`expr.hpp`)

A single immutable tagged node, shared by `std::shared_ptr`:

```cpp
using Expr = std::shared_ptr<const ExprNode>;
enum class Kind { Number, Symbol, Constant, Add, Mul, Pow, Function };
enum class ConstantId { Pi, E };
enum class FunctionId { Sin, Cos, Tan, Asin, Acos, Atan, Sinh, Cosh, Tanh, Ln, Abs };
```

**Canonical form (invariants that hold for every constructed `Expr`):**

- There is no Sub/Div/Neg/Sqrt/Exp/Log node. Rewrites at construction/parse:
  - `a - b`  → `Add(a, Mul(-1, b))`
  - `-a`     → `Mul(-1, a)`
  - `a / b`  → `Mul(a, Pow(b, -1))`
  - `sqrt(x)`→ `Pow(x, 1/2)`; `\sqrt[n]{x}` → `Pow(x, 1/n)`
  - `exp(x)` → `Pow(E, x)`
  - `log_b(x)` → `Mul(Ln(x), Pow(Ln(b), -1))`; plain `log` means base 10
  - `sec/csc/cot x` → `Pow(cos/sin/tan x, -1)`
- `Add` and `Mul` are n-ary (≥ 2 args), never directly nest themselves
  (auto-flattened), and their args are **sorted** by `compare_expr`.
- `Pow` has exactly 2 args (base, exponent); `Function` exactly 1.
- Numbers are exact rationals (`Rational`, §3). Decimal literals are converted
  exactly (`3.14` → `157/50`).

**Factory functions** (`make_add`, `make_mul`, `make_pow`, `make_num`,
`make_sym`, …) perform *light* canonicalization only — deterministic, cheap,
never recursive rewriting:

- `make_add(terms)`: flatten nested Adds; fold all Number terms into one exact
  sum; drop resulting 0; 0 args → `0`, 1 arg → that arg; sort args.
- `make_mul(factors)`: flatten; fold Numbers; any literal `0` → `0`; drop `1`s;
  0 args → `1`, 1 arg → that arg; sort args.
- `make_pow(b, e)`:
  - `e = 0` → `1` (convention: `0^0 = 1`, documented); `e = 1` → `b`;
    `b = 1` → `1`; `b = 0` with positive-rational `e` → `0` (`0^negative`
    throws DivisionByZeroError via the numeric fold).
  - Number base with **integer** Number exponent → exact fold
    (overflow-checked; a result that cannot fit 64 bits throws OverflowError).
  - Number base with non-integer rational exponent `p/q` (reduced) → fold only
    if the result is exactly rational (integer q-th roots of numerator and
    denominator exist): `pow(4, 1/2)` → `2`, `pow(8/27, 1/3)` → `2/3`.
    Negative Number bases fold only when `q` is odd (`(-8)^(1/3)` → `-2`,
    `(-8)^(2/3)` → `4`), never for even `q`. If the exact result exists
    mathematically but does not fit 64 bits (e.g. `pow(4, 101/2)` = 2^101),
    stay **symbolic** — catch the internal overflow; only the
    integer-exponent path throws.
  - `make_pow(Pow(u, r), s)` where `r` and `s` are both Numbers and `s` is an
    **integer** → `make_pow(u, r·s)`. (Required for the printer round-trip:
    `3/x^2` re-parses through `Pow(Pow(x,2),-1)` and must land on
    `Pow(x,-2)`. Note `(u^(1/2))^2 → u` extends the domain — standard CAS
    behavior, documented.)
  - `make_pow(Mul(...), s)` where `s` is an integer Number and the Mul
    contains a Number factor `c` → `Mul(c^s, make_pow(Mul(rest...), s))`.
    (Also required for round-trip: `1/(2*x)` must re-parse to the canonical
    `Mul(1/2, Pow(x,-1))`.)
- `make_fn(id, arg)`: no auto-evaluation (special values are simplify's job).

Numeric folding in `make_add`/`make_mul` must be **order-independent**:
accumulate exact sums/products with 128-bit intermediates (reducing as you
go) so the same multiset of Number args never produces a different result —
or a spurious OverflowError — depending on input order. Throw only if the
final reduced value does not fit 64 bits.

Like-term collection (`2x + 3x → 5x`), power combining, and all identities
live in **simplify**, not in the factories.

**Canonical ordering** `compare_expr(a, b) -> int` (-1/0/+1), a strict total
order used for sorting Add/Mul args and for deterministic output:

1. Rank by Kind: Number < Constant < Symbol < Pow < Function < Mul < Add.
2. Ties: Number by rational value; Constant by id; Symbol lexicographic by
   name; Function by (id, arg); Pow by (base, exponent); Add/Mul
   lexicographically element-wise, shorter first on prefix ties.

**Other core utilities** (in `expr.cpp`): `structurally_equal`, `hash_expr`
(consistent with equality), `contains_symbol`, `free_symbols`, `substitute`
(replaces every occurrence of a symbol with an expression, reconstructing
through factories), `debug_string` (s-expression dump, e.g.
`(add 2 (mul 3 x))` — exact format: lowercase kind names, numbers as `n` or
`n/d`, functions as `(sin x)`, constants as `pi`/`e`).

Convenience operators on `Expr` (`+ - * /`, unary `-`) forward to factories.

`Equation` is a simple struct `{ Expr lhs, rhs; }` — not an Expr node.

---

## 3. Rational (`rational.hpp`)

Exact rational over `long long`: normalized (den > 0, gcd = 1, zero is `0/1`).
All arithmetic is overflow-checked (`__builtin_*_overflow` or equivalent) and
throws `OverflowError` on overflow, `DivisionByZeroError` on x/0. Provides
`+ - * /`, unary minus, `==`/`<=>`, `pow(long long)` (negative exponents
invert; `0^negative` throws DivisionByZeroError), `from_decimal_string("3.14")`,
`to_double`, `to_string` (`"5"`, `"-3/2"`), predicates (`is_zero`, `is_one`,
`is_integer`, `is_negative`).

---

## 4. Parser (`parser.hpp`) — LaTeX-style grammar

Input: one expression, or one equation (`lhs = rhs`, single `=`). Both LaTeX
and plain ASCII style are accepted by the same grammar. Throws `ParseError`
carrying a byte span `[begin, end)` into the original input for caret
diagnostics.

### Tokens

- Numbers: integers `42`, decimals `3.14`, and (v0.4) scientific notation
  `2e3`, `1.5E-10`, `2e+5` — consumed as one Number token ONLY when digits
  follow the `e`/`E` (with optional sign), so `2e` stays `2·e` (Euler) and
  `2e x` stays `2·e·x`. The value is exact (`2e-3` = 1/500); a literal whose
  exact value cannot fit the 64-bit rational range (`1e300`) is a clean
  ParseError, never a crash or a silent approximation.
- Unicode math input (v0.4) — what phone keyboards and copy-pasted text
  produce, handled in the LEXER so byte-span caret diagnostics keep working:
  `×`, `⋅`, `·` → `*`; `÷` → `/`; `−` (U+2212) → `-`; `√` → `sqrt` (same
  bare-argument rule as any function name); `π` → pi; bare greek letters
  `α β γ δ ε θ λ μ φ ω` → the same symbols as their `\alpha`-style names;
  superscript runs `x²`, `x⁻¹`, `2x³` → `^` powers; `°` → `*(pi/180)` (so
  `sin(30°)` = `sin(pi/6)`); `≤ ≥ ≠` → targeted "inequalities are not
  supported yet" ParseError. Anything else non-ASCII keeps the standard
  unexpected-character error with the whole UTF-8 sequence hex-escaped.
- Operators: `+ - * / ^ = ( ) { } [ ]`, `**` (synonym for `^`), `,` (only
  meaningful to callers like the REPL, not inside expressions except function
  call syntax has exactly one argument — a comma inside parens is a
  ParseError).
- LaTeX commands: `\frac{a}{b}`, `\sqrt{x}`, `\sqrt[n]{x}`, `\cdot`, `\times`
  (→ multiplication), `\div` (→ division), `\left`/`\right` (paired with the
  following delimiter, must match), `\pi`, spacing commands `\, \; \! \:
  \quad \qquad` (ignored), function commands `\sin \cos \tan \arcsin \arccos
  \arctan \sinh \cosh \tanh \sec \csc \cot \exp \ln \log \sqrt`, greek
  letters `\alpha \beta \gamma \delta \epsilon \theta \lambda \mu \phi \omega`
  (→ symbols named `alpha`, `beta`, ...). `\log_{b}` / `\log_b` for explicit
  base. Unknown `\command` → ParseError pointing at the command.
- Identifier runs (`[A-Za-z]+`): segmented **greedily, longest known name
  first** at each position. Known names: function names (`sin`, `cos`, `tan`,
  `asin`, `acos`, `atan`, `arcsin`, `arccos`, `arctan`, `sinh`, `cosh`,
  `tanh`, `sec`, `csc`, `cot`, `exp`, `ln`, `log`, `sqrt`, `abs`), constants
  (`pi`, `e`), greek names (same list as above). Anything else consumes a
  single letter as a **symbol**. So `xy` → `x·y`, `sinx` → `sin(x)` (function
  then continues lexing), `alpha` → symbol `alpha`.
  Consequence: variables are single letters (or greek names), and `e` is
  always Euler's number — documented in README.
  **Word-variable guard (v0.4):** a run whose greedy segmentation contains
  **3 or more single-letter symbols** is rejected with a helpful ParseError
  instead of silently becoming a product — `speed` would otherwise parse as
  `s·p·e·e·d` with `e` as Euler's number, which is wrong math nobody asked
  for. The error names the word and shows the explicit-product escape hatch
  (`s*p*e*e*d`). Runs dominated by known names still segment: `sinx`,
  `pie` → `pi·e`, `exy` → `e·x·y` (two symbols — allowed); `xy` (two
  letters) stays a product.
- Subscripts: `x_1`, `x_{12}`, `x_a` → single symbol named `x_1`, `x_12`,
  `x_a`. After `_` **without** braces, the subscript is exactly one letter or
  one maximal digit run: `x_12` ≡ `x_{12}` (one symbol `x_12`), while `x_ab`
  is `x_a · b`. A braced subscript takes every letter/digit up to the
  matching `}` (`x_{max}` → symbol `x_max`); anything else inside the braces
  is a ParseError. Subscript on a non-symbol is a ParseError (except
  `\log_b`). The Plain printer emits the unbraced form (`x_12`), which must
  re-lex to the same symbol.

### Grammar (precedence, loosest → tightest)

```
input       := expr ( '=' expr )?
expr        := term ( ('+' | '-') term )*
term        := unary ( ('*' | '/' | implicit) unary )*     ; implicit multiplication
unary       := ('-' | '+') unary | postfix
postfix     := atom ( '^' unary )?                          ; right-assoc: 2^3^2 = 2^(3^2)
atom        := number | symbol | constant | '(' expr ')' | '{' expr '}'
             | '[' expr ']' | '|' expr '|' | \frac{expr}{expr}
             | \sqrt[expr]{expr} | function-application
```

- Implicit multiplication: two adjacent atoms/factors multiply (`2x`,
  `x y`, `2(x+1)`, `(x+1)(x-2)`, `2\pi r`). It binds exactly like explicit
  `*`, so `1/2x` = `(1/2)·x` — documented. It triggers **only** when the next
  token is an atom-starter — a number, a letter/known name, `(`, `{`, `[`, or
  a LaTeX command that begins an atom. It never triggers at
  `+ - * / ^ = ) } ]`, a comma, or end of input; in particular `2 - 3` is
  always the subtraction `Add(2, -3)`, never `Mul(2, -3)`.
- Unary minus: `-x^2` = `-(x^2)`; exponent may itself be signed: `2^-3` ok.
- Function application: `sin(x)`, `\sin{x}`, or bare `\sin x`. The bare form's
  argument is the following **tight factor sequence**: one or more adjacent
  atoms (numbers/symbols/constants, each with optional `^` power), stopping at
  `+ - * / = ) } ]`, a comma, another function name, `\frac`, or end of
  input; if the first token after the function is a group (`(`, `{`, `\frac`,
  `\sqrt`), the argument is exactly that one group. Examples:
  `\sin 2x` → `sin(2x)`; `\sin x \cos y` → `sin(x)·cos(y)`;
  `\sin x + 1` → `sin(x) + 1`; `\sin^2 x` → `(sin x)^2` (the common
  `\sin^{n}` notation is supported; `\sin^{-1} x` means `asin(x)`).
  A function name with no argument (`sin` then `+`) → ParseError.
- `=` at top level only, at most one → `Equation`.

API: `parse_input` (expr or equation), `parse_expression`, `parse_equation`.

### Diagnostics

`ParseError::what()` is a human message (e.g. `unexpected ')'`,
`missing '}' after \frac numerator`, `unknown command '\fraq'`). The CLI adds
the caret display:

```
error: unknown command '\fraq'
    \fraq{1}{2} + x
    ^~~~~
```

---

## 5. Printer (`printer.hpp`)

`to_string(expr, PrintStyle::Plain | PrintStyle::LaTeX)` and same for
`Equation` (`lhs = rhs`).

Plain style: minimal correct parentheses; explicit `*`; `^` for powers;
numbers as `5`, `-3/2`; `pi`, `e`; functions as `sin(...)`, natural log as
`ln(...)`; `Mul(-1, x)` → `-x`; Add renders subtractions
(`x + (-2)*y` → `x - 2*y`).

Division rendering (Plain): within a Mul, **only the first factor (in
canonical arg order) with a negative Number exponent** moves to a denominator
with the exponent negated, together with a non-integer rational coefficient's
denominator; any *further* negative-exponent factors stay in the numerator as
explicit negative powers. This single-factor rule is deliberate: the §2
`make_pow` folds recover only one denominator factor on re-parse (Number
extraction from an integer power of a Mul, never distribution over two
symbolic factors), so moving more than one below the bar would break the
§5/§11 round-trip invariant. Examples: `Mul(3/2, Pow(x,-1))` → `3/(2*x)`,
`Mul(3, Pow(x,-2))` → `3/x^2`, a bare `Pow(x,-1)` → `1/x`, and
`Mul(x, Pow(y,-1), Pow(z,-1))` → `x*z^(-1)/y` (only `y^(-1)` moves; `z^(-1)`
stays as a negative power). `Pow(u, 1/2)` → `sqrt(u)` and `Pow(u, -1/2)` →
`1/sqrt(u)`. These re-parse to the original AST **because of** the §2
make_pow folds (Pow-of-Pow with integer outer exponent; Number extraction
from an integer power of a Mul) — printer tests must cover exactly these
shapes. Exception: `Pow(E, x)` prints as `e^x` and this rule **wins** over
sqrt/division reconstruction whatever the exponent (`Pow(E, 1/2)` → `e^(1/2)`,
`Pow(E, -2)` → `e^(-2)`).

Add display ordering: the printer displays Add terms sorted by **descending
total degree**, where a term's total degree is the sum of the rational
exponents of its non-Number factors (a bare symbol or function counts 1, a
Number term counts 0); ties break by **ascending `compare_expr` of the whole
term** — i.e. equal-degree terms keep their canonical §2 relative order. So
`Add(x, Mul(-2, y))` → `x - 2*y` (Symbol x < Mul), `Add(Mul(2, x), y)` →
`y + 2*x` (Symbol y < Mul), and `Add(Mul(2, y), Mul(3, x))` → `2*y + 3*x`
(the two Muls compare elementwise, coefficient `2 < 3`). This is a print-time
ordering only — the AST keeps its canonical §2 arg order. Example:
`x^2 + 2*x + 3`.

LaTeX style: same structure but `\frac{...}{...}` for the division rendering,
`\sqrt{...}`, `\sin\left(...\right)` (always parenthesize function args with
`\left(`), `\pi`, greek symbol names get their backslash back
(`alpha` → `\alpha`), exponents in braces (`x^{10}`), subscripted symbols
re-rendered (`x_12` → `x_{12}`). Exception to the function-call form:
`abs(u)` renders as `\left|u\right|` (standard LaTeX has no `\abs`; bars are
what external renderers like KaTeX expect), and the parser accepts
`\left| ... \right|` as absolute value so the round-trip holds. Bare
`|expr|` is also accepted (§4): a `|` in operand position opens, the next
top-level `|` closes; genuinely ambiguous nestings need `abs()` or
parentheses. Juxtaposition between factors (`2 x` →
`2x`) EXCEPT: emit `\cdot` whenever the preceding factor ends in a digit and
the following factor's rendering starts with a digit (e.g.
`Mul(2, Pow(10, x))` → `2 \cdot 10^{x}`, which would otherwise re-lex as
`210^x`). Two literal Number factors never co-occur (the factories fold
them), so the digit-boundary rule is the only `\cdot` case.

Round-trip invariant (tested): `parse(to_string(e, Plain))` and
`parse(to_string(e, LaTeX))` are `structurally_equal` to `e`.

`debug_string` lives in expr, not here.

---

## 6. Evaluator (`evaluator.hpp`)

- `evaluate(expr, bindings) -> double` where `bindings` maps symbol name →
  double. Throws `EvalError` on: unbound symbol (message names it), division
  by zero, domain errors (`ln(x≤0)`, `asin(|x|>1)`, even root of a negative,
  `0^negative`), or non-finite result. `Pow` with non-integer exponent and
  negative base → EvalError (real domain only).
- `try_exact_numeric(expr) -> std::optional<Rational>`: folds an expression
  containing no symbols/constants to an exact rational if every operation
  stays rational (integer powers, rational-result roots); `std::nullopt`
  otherwise (including on overflow — catch internally).

---

## 7. Simplifier (`simplify.hpp`)

- `simplify(expr)`: apply the safe rule set bottom-up, repeat to fixpoint
  (bounded iterations, e.g. 32; the rules must be confluent enough that this
  terminates — every rule must strictly reduce a well-founded measure or be
  applied only once per pass).
- `simplify(Equation)`: simplify both sides.
- `expand(expr)`: distribute Mul over Add, expand integer powers of sums
  (binomial via repeated multiplication), then `simplify`.
- `collect(expr, symbol)`: rewrite as a polynomial in `symbol` with simplified
  coefficients (returns the expr unchanged apart from regrouping if not a
  polynomial in `symbol` — collect what it can: `x*y + x*z + 1` collected in
  `x` → `(y+z)*x + 1`). The returned Expr obeys the standard §2 canonical
  form; "reads highest-degree first" is a *printing* behavior delivered by
  §5's display ordering, not an AST property.
- `polynomial_coefficients(expr, symbol) -> optional<vector<Expr>>`: if
  `expand(expr)` is a polynomial in `symbol` (finite non-negative integer
  powers, coefficients free of `symbol`), return coeffs `c[0..n]` (index =
  degree, simplified, `c[n]` nonzero — except the zero polynomial, which
  returns the single coefficient `{0}`); else `nullopt`. This is the
  solver's workhorse.
- `factor(expr)`: best-effort — extract common numeric/symbolic factors from
  an Add; factor quadratics with rational roots into linear factors; leave
  anything else unchanged. Never throws just because it can't factor.

**Rule inventory for `simplify`** (all "safe" — value-preserving on the reals
except formal cancellations, which are standard CAS behavior and documented):

- Numeric folding everywhere (via factories + `try_exact_numeric` on subtrees).
- Like terms: `2x + 3x → 5x`; more generally sum terms with equal non-numeric
  part fold coefficients (`x*y + 2*x*y → 3*x*y`).
- Like factors: `x * x^2 → x^3`; equal bases with numeric exponents combine;
  `x/x → 1` (formal; assumes `x ≠ 0`).
- Power rules — `(u^a)^b → u^(a*b)` for rational Numbers `a`, `b`. `b`
  integer is already a §2 factory fold. For non-integer `b` the rule fires
  exactly when it cannot *restrict* the real domain or change the value
  under §6's evaluator (domain *extensions* are acceptable, restrictions and
  value changes are not). Since `u^a` with non-integer `a` is undefined for
  `u < 0` under §6, the case analysis is:
  - `a` non-integer, or `a` an **odd** integer → fold (the left side is
    undefined at `u < 0` anyway, so equality holds everywhere both are
    defined; e.g. `(x^(1/3))^(1/5) → x^(1/15)`, `(x^3)^(1/2) → x^(3/2)`,
    `(x^(2/3))^(3/2) → x` — the last extends the domain, which is fine).
  - `a` an **even** integer → `(u^a)^b = |u|^(a*b)`, so fold to `u^(a*b)`
    only when `a*b` is an **even integer** (`(x^6)^(1/3) → x^2`,
    `(x^4)^(1/2) → x^2`); when `a*b` is an **odd** integer fold to
    `abs(u)^(a*b)` (`(x^2)^(1/2) → abs(x)` — this generalizes the
    sqrt-of-square rule; `(x^6)^(1/2) → abs(x)^3`); otherwise (`a*b` not an
    integer, e.g. `(x^2)^(1/3)`) do NOT fold — `x^(2/3)` would be undefined
    at `x < 0` where the input is defined.
  `x^a * y^a` is NOT combined; `(x*y)^n → x^n * y^n` for integer `n`.
- `Pow(E, Ln(x)) → x`; `Ln(Pow(E, x)) → x`; `Ln(1) → 0`; `Ln(E) → 1`.
- `ln(a*b) → ln a + ln b` is NOT applied (domain); `ln(x^n)` for odd integer
  `n` → `n·ln(x)` is NOT applied either — log expansion is out of scope for
  `simplify` (would fight the fixpoint; note in README).
- Trig: exact values at rational multiples of π with denominator ∈
  {1, 2, 3, 4, 6} (reduced mod 2π, all quadrants) for sin and cos; same for
  tan **except** odd multiples of π/2, where tan is undefined — those are
  left unchanged by simplify (never invent a value, never throw);
  `sin(-u) → -sin(u)`, `tan(-u) → -tan(u)`, `cos(-u) → cos(u)` (negation
  detected as Mul with negative leading rational — pull the sign out);
  `sin(u)^2 + cos(u)^2 → 1` (also with a common coefficient:
  `k·sin² + k·cos² → k`); `asin(sin(x))` is NOT simplified (not
  value-preserving); `sin(asin(x)) → x` IS (valid where defined).
- Inverse trig special values (needed so solver results like `asin(1/2)`
  print as `pi/6`): asin and acos at `0, ±1/2, ±sqrt(2)/2, ±sqrt(3)/2, ±1`;
  atan at `0, ±1, ±sqrt(3), ±sqrt(3)/3` (recognize the canonical AST shapes
  of these arguments, e.g. `Mul(1/2, Pow(3, 1/2))` for `sqrt(3)/2`,
  `Mul(1/3, Pow(3, 1/2))` for `sqrt(3)/3`).
- `abs(c)` for numeric `c` → `|c|`; `abs(u) → u` whenever `u` is
  **structurally provably nonnegative** — nonnegative numbers, the constants
  pi/e, `abs`/`cosh` applications, even integer powers, even roots
  (principal value), `e^u`, and sums/products of such (conservative check:
  unprovable means keep the abs). Subsumes `abs(abs(u))`, `abs(pi)`,
  `abs(e^x) → e^x`, `abs(x^2) → x^2`. `abs(-u) → abs(u)` (sign pulled as
  above); `abs(u)^2 → u^2` (even integer powers).
- Hyperbolic: `sinh(0)=0, cosh(0)=1, tanh(0)=0`, odd/even sign rules.
- `sqrt` (as `Pow(u, 1/2)`): `sqrt(u^2) → abs(u)`; more generally
  `(u^even)^(1/even)` respects abs.

`simplify` must be **idempotent**: `simplify(simplify(e))` is
`structurally_equal` to `simplify(e)` — tested property.

---

## 8. Derivative (`derivative.hpp`)

`differentiate(expr, symbol) -> Expr` (returned already `simplify`-ed).
Linearity; n-ary product rule (Σᵢ argᵢ' · Πⱼ≠ᵢ argⱼ); general power rule
`d(u^v) = u^v · (v'·ln u + v·u'/u)`, with the two common special cases taken
directly (v constant → `v·u^(v-1)·u'`; u = E → `E^v · v'`) so results stay
clean; chain rule through every FunctionId:

| f      | f'(u)·u'                    |
|--------|-----------------------------|
| sin    | cos(u)                      |
| cos    | -sin(u)                     |
| tan    | 1 + tan(u)^2                |
| asin   | (1-u²)^(-1/2)               |
| acos   | -(1-u²)^(-1/2)              |
| atan   | (1+u²)^(-1)                 |
| sinh   | cosh(u)                     |
| cosh   | sinh(u)                     |
| tanh   | 1 - tanh(u)^2               |
| ln     | u^(-1)                      |
| abs    | u·abs(u)^(-1)  (note: undefined at 0) |

Symbols other than `symbol`, constants, numbers → 0.

## 8b. Integration (`integrate.hpp`, v0.3)

```cpp
struct IntegrateResult {
    enum class Status { Integrated, Unsolved };
    Status status = Status::Unsolved;
    Expr antiderivative;            // Integrated only; the "+ C" is implicit
    std::string method;             // see labels below
    std::vector<std::string> warnings;
};
IntegrateResult integrate(const Expr& e, std::string_view symbol);

struct DefiniteIntegralResult {
    enum class Status { Exact, Numeric, Unsolved };
    Status status = Status::Unsolved;
    Expr value;                     // Exact: symbolic; Numeric: a Number
    std::string method;             // "FTC" or "numeric (adaptive Simpson)"
    std::vector<std::string> warnings;
};
DefiniteIntegralResult integrate_definite(const Expr& e, std::string_view symbol,
                                          const Expr& lo, const Expr& hi);
```

Rule-based indefinite integration; an integrator that cannot integrate says
`Unsolved` honestly (warning `"no applicable integration rule"`) — it never
guesses. Real domain; antiderivatives of `1/u` use `ln(abs(u))`.

**Strategy pipeline** over `simplify(e)`, first applicable technique wins;
`method` records which (labels: `"table"`, `"power rule"`, `"linearity"`,
`"u-substitution"`, `"integration by parts"`, `"partial fractions"`,
`"trig identity"` — combinations join with `" + "`):

1. **Linearity.** Integrate `Add` term-wise; pull out factors free of
   `symbol` (numbers, constants, parameter symbols). Any term `Unsolved` →
   whole integral `Unsolved`.
2. **Table with linear inner argument.** For `u = a*x + b` (`a ≠ 0`, `a`, `b`
   free of `x`, including plain `u = x`):
   `u^r` (rational `r ≠ -1`) → `u^(r+1)/((r+1)·a)`; `u^(-1)` →
   `ln(abs(u))/a`; `sin u` → `-cos(u)/a`; `cos u` → `sin(u)/a`; `tan u` →
   `-ln(abs(cos u))/a`; `e^u` → `e^u/a`; `c^u` for numeric `c > 0`, `c ≠ 1`
   → `c^u/(a·ln c)`; `sinh u` → `cosh(u)/a`; `cosh u` → `sinh(u)/a`;
   `tanh u` → `ln(cosh u)/a`; `ln u` → `(u·ln u - u)/a`; `asin u` →
   `(u·asin u + sqrt(1-u^2))/a`; `acos u` → `(u·acos u - sqrt(1-u^2))/a`;
   `atan u` → `(u·atan u - ln(1+u^2)/2)/a`; `cos(u)^(-2)` → `tan(u)/a`;
   `sin(u)^(-2)` → `-cos(u)/(a·sin(u))`; `1/(u^2 + c)` for numeric/param
   `c > 0` → `atan(u/sqrt(c))/(a·sqrt(c))` (equivalently any quadratic
   denominator with x-free coefficients, by completing the square; a
   positivity/nonzeroness that cannot be decided for symbolic parameters is
   *assumed* and stated in an explicit warning — e.g. `"result assumes c >
   0"` — while a provably negative completed-square constant falls through
   to later stages); `1/sqrt(c - u^2)` for `c > 0` →
   `asin(u/sqrt(c))/a`. `abs(u)` → `Unsolved` (piecewise result; out of
   scope, warn).
3. **Polynomial / expansion.** If `expand(e)` is a polynomial in `x` →
   term-wise power rule. Otherwise, if `expand(e) ≠ e` structurally, retry
   the whole pipeline ONCE on the expanded form (guards against loops).
4. **u-substitution (derivative-divides).** Enumerate candidate inner
   subexpressions `u` containing `x` (function arguments, `Pow` bases and
   exponents); compute `w = simplify(e / differentiate(u, x))`; substitute
   `u → t` (fresh symbol) in `w`; if the result is free of `x`, recursively
   integrate in `t` (bounded recursion depth 8) and substitute back.
5. **Integration by parts**, total depth ≤ 3, patterns only (no blind
   search): `poly(x) · {sin, cos, sinh, cosh, e^}(linear u)` — parts
   reducing the polynomial degree; `poly(x) · {ln, atan, asin, acos}(...)`
   — parts with `dv = poly`; the cyclic `e^(a*x) · {sin, cos}(b*x)` →
   closed form directly (`e^(ax)(a·sin(bx) - b·cos(bx))/(a²+b²)` and
   `e^(ax)(a·cos(bx) + b·sin(bx))/(a²+b²)`).
6. **Rational functions** `P(x)/Q(x)` (both polynomials with **rational
   Number** coefficients; symbolic-parameter coefficients → `Unsolved`):
   polynomial division first; factor `Q` by the §9 rational-root machinery
   into linear factors (with multiplicity) and at most ONE irreducible
   quadratic; decompose by **partial fractions with unknown coefficients
   solved via `solve_system`** (§9b) after coefficient matching through
   `polynomial_coefficients`; integrate the pieces (`A/(x-r)^k`, and
   `(Bx+C)/(x²+px+q)` by completing the square → `ln` + `atan`). Denominator
   degree ≤ 6; anything else `Unsolved`.
7. **Trig powers**: rewrite `sin(u)^2 → (1 - cos(2u))/2`,
   `cos(u)^2 → (1 + cos(2u))/2` (linear `u`) and retry; odd powers
   `sin(u)^(2k+1)`, `cos(u)^(2k+1)` for small k (≤ 5): peel one factor and
   substitute (Pythagorean rewrite + stage 4 handles it).
8. Give up: `Unsolved` + warning.

**Self-verification doctrine (mandatory).** Every candidate antiderivative
`F` is checked before being returned: numerically compare
`evaluate(differentiate(F, x))` with `evaluate(e)` at ~5 sample points
(spread over positives/negatives/fractions; skip points where either side
throws EvalError; non-finite → skip). A clear mismatch (> 1e-6 relative) at
any clean point → internal-rule bug: return `Unsolved` with warning
`"a candidate antiderivative failed verification"` (never return an
unverified wrong answer). All points skipped → return it with a
`"could not verify numerically"` warning.

**Definite integrals.** Bounds must be symbol-free (constants like `pi`
fine); non-finite or symbol-bearing bounds → `Unsolved` with a warning.
Path: (a) indefinite succeeds → check the *integrand* evaluates finitely on
a ~64-point grid over `[lo, hi]` (a domain gap or non-finite value → FTC
unsafe: warn and go numeric); value = `simplify(F(hi) - F(lo))`, status
`Exact`, method `"FTC"`, cross-checked against the numeric quadrature
(a *converged* quadrature disagreeing > 1e-6 relative → prefer numeric with
a warning — FTC across an undetected discontinuity loses). (b) Otherwise
adaptive Simpson (tolerance 1e-10 relative, max depth 40) on the evaluable
integrand; status `Numeric`, value a decimal-converted Number. A grid
failure only at the interval *endpoints* (a removable or endpoint
singularity, e.g. `sin(x)/x` at 0) → integrate over a slightly shrunken
open interval, with a warning; an interior gap → `Unsolved`. A quadrature
that exhausts its depth budget without reaching the tolerance produced an
untrustworthy number — the integral may be divergent (a pole can sit
between grid points) — and is never published as a value: status
`Unsolved` with warning `"numeric quadrature failed to converge; the
integral may be divergent"`.

**CLI / REPL.** `mathsolver integrate "x*sin(x)" [x]` (variable optional
when unique) prints `F(x) + C` (the literal ` + C`), then `method:` and
warnings; `Unsolved` → `unable to integrate` (exit 0 — it is an answer).
Definite: `mathsolver integrate "sin(x)" x --from 0 --to pi` (both flags or
neither; bounds parsed as expressions) prints `value = 2` / `value ≈ ...`.
REPL: `integrate <expr>[, <var>[, <lo>, <hi>]]`. LaTeX mode renders the
antiderivative only (no `+ C` gymnastics: append `+ C` in math text).

---

## 9. Solver (`solver.hpp`)

```cpp
struct Solution { Expr value; bool exact; std::string note; };
struct SolveResult {
  enum class Status { Solved, NumericOnly, NoRealSolution, AllReals, Unsolved };
  Status status; std::vector<Solution> solutions;
  std::string method; std::vector<std::string> warnings;
};
struct NumericOptions { double lo = -100, hi = 100; int scan_points = 4001;
                        double tol = 1e-12; int max_iter = 128; };
SolveResult solve(const Equation&, std::string_view symbol,
                  const NumericOptions& = {});
SolveResult solve_numeric(const Equation&, std::string_view symbol,
                          const NumericOptions& = {});
```

`solve` pipeline over `f = simplify(lhs - rhs)`:

1. `f` free of the symbol: `f ≡ 0` → `AllReals` ("identity"); numeric nonzero
   → `NoRealSolution` ("contradiction"); otherwise `Unsolved` with a note.
2. **Polynomial path**: `polynomial_coefficients(f, x)`; degree 1 → linear;
   degree 2 → quadratic formula, exact roots kept symbolic — written
   unambiguously as `(-b ± sqrt(b^2 - 4*a*c)) / (2*a)`, simplified. Negative
   *numeric* discriminant → `NoRealSolution`; **zero** discriminant → a
   single Solution (not two identical ones); symbolic discriminant → return
   both roots with a warning that they exist only when the discriminant ≥ 0;
   a *symbolic* leading coefficient additionally gets a warning that the
   roots are valid only when that coefficient is nonzero. Degree ≥ 3 with
   **numeric rational** coefficients → rational-root theorem to peel exact
   roots + synthetic division, quadratic on what remains, numeric fallback
   for any irreducible remainder; biquadratic-style detection (poly in
   `x^k`) → substitute, solve, back-substitute (`x^k = r` → real k-th roots).
3. **Isolation path** (symbol occurs exactly once in `f` after simplify, or
   equation rearranges to `g(x-part) = const-part`): peel outermost layer
   repeatedly. Rules, with `c` the constant side after each peel:
   - Add: move symbol-free terms across.
   - Mul: divide by symbol-free factors; a *symbolic* divisor gets a
     "valid only when ... ≠ 0" warning.
   - Pow, `u^e = c` with `e` a Number `p/q` (reduced; covers `sqrt` = 1/2):
     `p` even (`q` odd) → require numeric `c ≥ 0` (`c < 0` →
     `NoRealSolution`), solutions `u = ±c^(q/p)`; `p` odd, `q` even →
     require numeric `c ≥ 0` (range of an even root; `c < 0` →
     `NoRealSolution`), single solution `u = c^(q/p)`; `p` odd, `q` odd →
     single solution `u = c^(q/p)`, where for numeric `c < 0` the result is
     written sign-extracted as `u = -(|c|^(q/p))` so it evaluates and
     verifies cleanly. Symbolic `c` → proceed with the generic form plus a
     domain warning.
   - Pow, `a^u = c` (symbol in the exponent) → `u = ln(c) / ln(a)`, requiring
     numeric `c > 0` and numeric `a > 0` (a ≤ 0 falls through to
     numeric/Unsolved; `a = 1` is unreachable — the factories fold `1^u`).
   - Function inverses: `ln → exp`; `abs(u) = c` → `u = ±c` requiring numeric
     `c ≥ 0` (`c < 0` → `NoRealSolution`); `asin/acos/atan(u) = c` → apply
     `sin/cos/tan`, with a numeric range check on `c` against the inverse
     function's range; `sin(u) = c` → principal `u = asin(c)`; `cos(u) = c` →
     principal `u = acos(c)`; `tan(u) = c` → principal `u = atan(c)`;
     `sinh/cosh/tanh` via their `ln`-form inverses (cosh: `±`, require
     numeric `c ≥ 1`).
   - Periodicity is reported per-root in `Solution.note` (equation-level
     caveats go in `warnings`), with these exact general-solution families:
     sin: `x = <root> + 2*pi*n or x = pi - <root> + 2*pi*n`;
     cos: `x = ±<root> + 2*pi*n`;
     tan: `x = <root> + pi*n` (period **π**, not 2π).
   Results are exact and simplified (the §7 inverse-trig table turns e.g.
   `asin(1/2)` into `pi/6`).
4. **Numeric fallback** (`solve_numeric`, also callable directly).
   Precondition: `free_symbols(f)` must be exactly `{x}` — if other free
   symbols remain when the fallback is reached, return `Unsolved` with a
   warning naming them (never let an unbound-symbol EvalError escape
   `solve`). Scan `[lo, hi]` on a uniform grid; where `f` changes sign →
   bisection tightened by Newton (derivative from §8, falling back to
   bisection when Newton leaves the bracket or derivative vanishes); where
   `|f|` is locally tiny without a sign change (grid minimum below 1e-7 of
   local scale) → Newton polish to catch even-multiplicity roots — every
   root harvested from this no-sign-change branch carries the per-root
   `Solution.note` `"tangency-type root: |f| has a near-zero minimum here;
   no sign change observed"`, because at these tolerances a genuine double
   root is numerically indistinguishable from a near-miss where the two
   sides approach within ~1e-7 but never touch (e.g.
   `e^x + e^-x = 2 - 1e-9`), and the note lets callers treat such roots
   with appropriate suspicion; skip
   non-finite regions (domain gaps); dedupe roots within
   `1e-8·max(1,|root|)`; verify every root by substitution
   (`|f(root)| < 1e-6` relative); sort ascending. Roots found → status
   `NumericOnly`, values are `Number` nodes holding decimal-converted
   rationals of the double roots, `exact = false`, warning notes the search
   interval ("roots outside [lo, hi] are not reported"). **Zero** roots
   found → status `Unsolved` (not an empty `NumericOnly`), same
   interval warning.
5. Every **exact** candidate is verified by numeric substitution at the end
   (random-free: evaluate the residual; if it contains free symbols other
   than x, substitute fixed test values — e.g. 1.7320508 and 0.3141593 —
   for them). Drop policy: a candidate is dropped (with a warning) ONLY when
   the residual evaluates **cleanly** (no EvalError) and is clearly nonzero
   (> 1e-6 relative) at every sample where it evaluates. An EvalError at all
   samples, or a residual that vanishes at some samples but not others,
   KEEPS the candidate with a "may be valid only under domain conditions"
   warning — conditional solutions (symbolic discriminants, range-limited
   inverses) must survive verification. If everything drops → fall through
   to numeric.

`solve` fills `method` with a short human label (`"linear"`,
`"quadratic formula"`, `"rational roots + quadratic"`, `"isolation"`,
`"numeric (Newton/bisection)"`).

## 9b. Linear systems (`solver.hpp`, v0.2)

```cpp
struct SystemSolveResult {
    enum class Status { Solved, NoSolution, Underdetermined, Unsolved };
    Status status = Status::Unsolved;
    std::map<std::string, Expr> values;  // Solved/Underdetermined: per symbol
    std::vector<std::string> free_variables;  // Underdetermined only
    std::string method;                  // "gaussian elimination"
    std::vector<std::string> warnings;
};
SystemSolveResult solve_system(const std::vector<Equation>& eqs,
                               const std::vector<std::string>& symbols);
```

Solves a system of equations **linear in the requested symbols**; any other
free symbols are treated as symbolic parameters (coefficients).

**Joint-linearity extraction.** For each equation, form
`f = simplify(expand(lhs - rhs))` and peel the requested symbols in order:
`polynomial_coefficients(f, x1)` must yield degree ≤ 1 with the linear
coefficient free of *all* requested symbols (this rejects cross-terms like
`x*y`, which are degree-1 in each variable separately but not jointly
linear); recurse on the constant term with `x2`, etc. Any violation →
`Unsolved` with the warning `"system is not linear in the requested
variables"`.

**Elimination.** Gaussian elimination over exact `Expr` arithmetic,
`simplify()` applied to every entry after each step. Pivot choice prefers
nonzero Number pivots; a *symbolic* pivot is allowed but adds the warning
`"valid only when <pivot> ≠ 0"` (same doctrine as the §9 quadratic leading
coefficient). Row `0 = c` with numeric nonzero `c` → `NoSolution`
("inconsistent system") — for an exact `Number` this is decided exactly (no
float epsilon; `x = 1; x = 1.0000000000001` is inconsistent), and a
constant-bearing symbol-free `c` (pi/e shapes) is `NoSolution` only when it
evaluates clearly nonzero; near-zero or non-evaluating constant shapes are
kept with the warning below, never silently accepted. With symbolic `c` →
keep, but warn `"inconsistent unless <c> = 0"`. If exact coefficient
arithmetic overflows the 64-bit rational anywhere (extraction, elimination,
back-substitution, or verification), the result is `Unsolved` with the
warning `"coefficient arithmetic overflowed 64-bit rationals"` — an
`OverflowError` never escapes `solve_system`.

**Shape of the answer.**
- Pivot count = #symbols → `Solved`; `values` maps every requested symbol to
  a simplified Expr free of all requested symbols.
- Fewer pivots → `Underdetermined`; non-pivot requested symbols are **free**
  (listed in `free_variables`, absent from `values`), and each pivot
  symbol's `values` entry may reference the free symbols
  (`x + y = 3` for `{x, y}` → `x = 3 - y`, free: `y`).
- More independent equations than symbols can still be `Solved` (consistent
  overdetermined) or `NoSolution`.

**Verification.** Substitute `values` into every input equation and
`simplify(lhs - rhs)`; anything that does not reduce to `0` is checked
numerically per the §9.5 doctrine (fixed test values for parameters and free
variables; EvalError → keep with a domain warning; clear nonzero → demote to
`Unsolved` with a warning naming the equation).

**CLI / REPL.** One-shot: `mathsolver solve "x + y = 3; x - y = 1" [x y]` —
equations separated by top-level `;` inside the single argument (the parser
itself is unchanged; the CLI splits). Variables optional: default is the
union of free symbols if its size is ≤ the number of equations, else usage
error (exit 2) listing the symbols. Output: one `x = ...` line per symbol
(pivot expressions may reference free variables), `free: y` line when
underdetermined, then `method:` and warnings; `NoSolution` → `no solution
(inconsistent system)`. REPL: `solve x+y=3; x-y=1, x, y` — the existing
top-level-comma split supplies the variables; `;` needs no new REPL logic.
A single equation with `;`-free input keeps the §9 single-equation path
untouched.

---

## 10. CLI / REPL (`apps/main.cpp`)

One-shot subcommands (all print plain style by default, `--latex` switches
output to LaTeX; errors go to stderr with caret diagnostics, exit code 1
(parse/math errors) or 2 (usage errors)):

```
mathsolver simplify "2x + 3x"
mathsolver expand   "(x+1)^3"
mathsolver factor   "x^2 - 5x + 6"
mathsolver solve    "x^2 = 4" [x] [--range LO HI]     # var optional if unique
mathsolver diff     "sin(x^2)" [x]                    # var optional if unique
mathsolver eval     "x^2 + y" x=3 y=0.5
mathsolver latex    "sqrt(x)/2"                       # print LaTeX form
mathsolver --help | --version
mathsolver          # no args → REPL
```

The `latex` command prints the parsed expression in LaTeX **without
simplifying** — only the §2 factory canonicalization inherent in parsing
applies (e.g. `2/4` → `\frac{1}{2}`); use `simplify --latex` for the
simplified LaTeX form (`2x+3x` → `5x`). The REPL `latex` command behaves the
same. A literal `--` argument ends option parsing (so an expression like
`-- "--x"` can be passed positionally).

Exit codes: **2 (usage error, `usage error:` prefix on stderr)** for a
malformed command line — unknown subcommand or flag, missing/extra arguments,
`--range` without two finite numeric bounds, or a malformed `eval` binding
such as `x=abc` or binding a constant (`pi=3`). **1** for errors thrown while
parsing or computing the expression itself — parse diagnostics (with a
caret), unbound symbols, and domain/overflow errors. **0** on success.

`solve` output: one line per solution (`x = 2`, `x ≈ 1.8955`), then
`method:` and any warnings. `NoRealSolution` → `no real solutions`;
`AllReals` → `true for all x`.

REPL (`>>> ` prompt, plain `std::getline` — no readline dependency; Ctrl-D or
`quit`/`exit` to leave):

- Bare expression → simplify and print.
- Bare equation → solve for its single free symbol (if several, ask user to
  use `solve ..., var`).
- Commands (comma-separated arguments, split at top-level commas only):
  `solve <eq>[, <var>]`, `diff <expr>[, <var>]`, `eval <expr>, x=1[, y=2 …]`,
  `expand <e>`, `factor <e>`, `latex <e>`, `debug <e>` (s-expr dump),
  `help`, `quit`/`exit`.
- Parse/math errors print the caret diagnostic and keep the session alive.

End-to-end tests: CTest invokes the built binary
(`add_test(... COMMAND mathsolver simplify ...)` + `PASS_REGULAR_EXPRESSION`,
or a small Catch2 file using `popen` with the binary path passed as a compile
definition — implementer's choice, but at least: simplify, solve exact, solve
numeric, diff, eval, a parse-error exit code, and a piped-stdin REPL session).

---

## 11. Testing conventions

- Catch2 v3, one `test_<module>.cpp` per module, registered in
  `tests/CMakeLists.txt`.
- Prefer building expectations with factories + `structurally_equal`, or
  round-trip through the parser once it exists; use `debug_string` in failure
  messages (`INFO(debug_string(e))`).
- Property-style checks worth including: parser round-trip (§5), simplify
  idempotence (§7), `evaluate(diff(f)) ≈ (f(x+h)-f(x-h))/2h` on a grid for
  every FunctionId, solver roots verified by substitution.
- Everything runs via `ctest --test-dir build` and must be green at the end of
  every stage.

## 12. Documented limitations (README material, keep honest)

- Real domain only (no complex results; quadratics with negative discriminant
  report "no real solutions").
- `e` is always Euler's number; variables are single letters/greek names.
- Rational arithmetic is 64-bit and overflow-checked (throws, never wraps).
- Formal cancellations (`x/x → 1`) assume nonzero denominators.
- Numeric root search only covers the requested interval.
- Scientific-notation literals are exact and must fit 64-bit rationals
  (`1e300` is a clean ParseError).
- Multi-letter words that would segment into 3+ single-letter variables are
  rejected with a helpful error (v0.4 word guard) rather than silently
  parsed as products.
