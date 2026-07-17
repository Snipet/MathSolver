# MathSolver Input Syntax

MathSolver reads a single **expression** or a single **equation** (`lhs = rhs`).
It accepts both LaTeX-style input and plain ASCII input — the same grammar
handles both, and you can mix them freely (`\frac{1}{2} + sqrt(x)` is fine).

This page is the complete reference for what the parser accepts.

**Contents:**
[Quick examples](#quick-examples) ·
[Numbers](#numbers) ·
[Variables, subscripts, Greek letters](#variables-subscripts-and-greek-letters) ·
[How words are read](#how-multi-letter-words-are-read) ·
[Operators and precedence](#operators-and-precedence) ·
[Unicode input](#unicode-input) ·
[LaTeX commands](#supported-latex-commands) ·
[Applying functions](#applying-functions) ·
[Absolute value](#absolute-value) ·
[Equations](#equations) ·
[Assignments](#assignments-repl-and-web) ·
[Pitfalls](#differences-from-real-latex-and-common-pitfalls) ·
[Errors](#error-messages)

---

## Quick examples

| LaTeX form            | Plain form      | Meaning                                  |
|-----------------------|-----------------|------------------------------------------|
| `\frac{x+1}{2}`       | `(x+1)/2`       | division                                 |
| `\sqrt{x}`            | `sqrt(x)`       | square root, i.e. `x^(1/2)`              |
| `\sqrt[3]{x}`         | `x^(1/3)`       | cube root                                |
| `2\pi r`              | `2 pi r`        | 2·π·r (implicit multiplication)          |
| `x^{10}`              | `x^10`          | power                                    |
| `\sin^2 x`            | `sin(x)^2`      | sine squared                             |
| `\sin^{-1} x`         | `asin(x)`       | inverse sine (arcsine)                   |
| `\log_2 8`            | `log_2(8)`      | logarithm base 2                         |
| `\alpha + x_1`        | `alpha + x_1`   | Greek variable plus subscripted variable |
| `\left(x+1\right)^2`  | `(x+1)^2`       | grouping                                 |
| `x^2 = 4`             | `x^2 = 4`       | equation                                 |

---

## Numbers

Three literal forms:

```text
42        integer
3.14      decimal
2e3       scientific notation (= 2000)
```

Decimals are stored **exactly** as rationals: `3.14` becomes `157/50`, not a
floating-point approximation.

**Scientific notation** is stored exactly too: `2e3` is `2000`, `2e-3` is
`1/500`, `1.5E-10` is `3/20000000000`, and `2e+5` is `200000`. A literal
whose exact value cannot fit the 64-bit rational range (`1e300`) is a clean
parse error, never a silent approximation.

The `e` (or `E`) joins the number **only when digits follow it directly** —
optionally after a `+`/`-` sign, with no space in between. Everywhere else
`e` is still Euler's number:

| Input    | Read as     | Why                                              |
|----------|-------------|--------------------------------------------------|
| `2e3`    | `2000`      | digits follow the `e` — one number token         |
| `2e-3`   | `1/500`     | a sign then digits also joins                    |
| `2e`     | `2 * e`     | no digits after `e` — Euler's number             |
| `2e x`   | `2 * e * x` | same: `e` is the constant, `x` a variable        |
| `2e - 3` | `2*e - 3`   | the space breaks the token — this is subtraction |

Negative numbers are written with the ordinary unary minus: `-3`, `-3/2`
(the latter parses as `(-3)/2`, which is the value −3/2).

---

## Variables, subscripts, and Greek letters

**Variables are single letters** (`x`, `y`, `A`, …) or Greek names. Multi-letter
variable names do not exist — see
[How multi-letter words are read](#how-multi-letter-words-are-read).

Two letters are reserved as constants and cannot be variables:

| Written as     | Meaning          |
|----------------|------------------|
| `pi` or `\pi`  | π                |
| `e`            | Euler's number e |

**Greek letters** are available both as backslash commands and as bare names.
Both spellings produce the same symbol:

```text
\alpha \beta \gamma \delta \epsilon \theta \lambda \mu \phi \omega
alpha  beta  gamma  delta  epsilon  theta  lambda  mu  phi  omega
```

**Subscripts** attach to a symbol with `_` and become part of its name:

```text
x_1        symbol named x_1
x_{12}     symbol named x_12
x_a        symbol named x_a
```

A subscript on anything that is not a symbol is a parse error — with one
exception, `\log_b` / `log_2`, where the subscript is the logarithm's base
(see [Supported LaTeX commands](#supported-latex-commands)).

---

## How multi-letter words are read

When the parser meets a run of letters, it does **not** treat it as one name.
Instead it walks the run left to right, and at each position:

1. takes the **longest known name** that starts there — a function name,
   a constant (`pi`, `e`), or a Greek name;
2. if no known name starts there, takes a **single letter** as a variable.

The known names are:

- **Functions:** `sin cos tan asin acos atan arcsin arccos arctan sinh cosh
  tanh sec csc cot exp ln log sqrt abs`
- **Constants:** `pi e`
- **Greek:** `alpha beta gamma delta epsilon theta lambda mu phi omega`

Examples of what this rule does:

| Input   | Read as        | Why                                                        |
|---------|----------------|------------------------------------------------------------|
| `xy`    | `x * y`        | no known name starts with `x`, so two single letters       |
| `sinx`  | `sin(x)`       | `sin` is a known name; `x` becomes its argument            |
| `exy`   | `e * x * y`    | the constant `e`, then two variables                       |
| `pie`   | `pi * e`       | `pi` matches first, then the constant `e`                  |
| `alpha` | symbol `alpha` | Greek names are known, so the whole word is one symbol     |

**The word guard.** A run whose segmentation would contain **three or more
single-letter variables** is rejected with a helpful error instead of
silently becoming a product — `speed` would otherwise read as `s*p*e*e*d`
with `e` as Euler's number, which is math nobody asked for:

```text
error: unknown name 'speed': variables are single letters (x, y), greek names (alpha), or subscripted (x_1); for a product, write it explicitly (s*p*e*e*d)
    speed
    ^~~~~
```

Two-letter runs (`xy`) still multiply, and runs dominated by known names
still segment (`sinx`, `pie`, `exy` above — `exy` has only two single-letter
variables, so it passes). If you really do want the product, the error shows
the escape hatch: write it explicitly (`s*p*e*e*d`).

The practical consequences: **variables are single letters or Greek names**,
and a word like `speed` is an error, not a variable named "speed". Use
subscripts (`v_1`, `v_a`) or Greek names when one letter is not enough.

---

## Operators and precedence

| Level        | Operators                              | Associativity | Example                          |
|--------------|----------------------------------------|---------------|----------------------------------|
| 1 (loosest)  | `=`                                    | — (only one, at top level) | `x^2 = 4`           |
| 2            | `+`, binary `-`                        | left          | `a - b + c` = `(a - b) + c`      |
| 3            | `*`, `/`, implicit multiplication      | left          | `a / b * c` = `(a / b) * c`      |
| 4            | unary `-`, unary `+`                   | —             | `-x^2` = `-(x^2)`                |
| 5 (tightest) | `^` (synonym: `**`)                    | right         | `2^3^2` = `2^(3^2)` = `512`      |

Additional operator notes:

- `**` is an exact synonym for `^`.
- `\cdot` and `\times` mean `*`; `\div` means `/` — same precedence level.
- Grouping: `(...)`, `{...}`, and `[...]` all group an expression, and
  `\left`/`\right` may decorate any delimiter (see below).
- A comma inside an expression is a parse error — functions take exactly one
  argument, and there are no tuples.

**Implicit multiplication** — two adjacent factors multiply with no operator:

```text
2x            (x+1)(x-2)
x y           2\pi r
2(x+1)
```

It binds **exactly like explicit `*`** — no tighter. That gives four classic
gotchas, all worth memorizing:

```text
1/2x     =  (1/2) * x        implicit mult is NOT tighter than /
-x^2     =  -(x^2)           ^ binds tighter than unary minus
2^3^2    =  2^(3^2) = 512    ^ is right-associative (not 8^2 = 64)
2e3      =  2000             scientific notation — but bare 2e is still 2*e
```

The exponent of `^` may itself be signed: `2^-3` is fine and means `2^(-3)`.

---

## Unicode input

The math symbols that phone keyboards, Word documents, and copy-pasted web
text produce are understood directly — no need to translate them to ASCII
first:

| You type              | Means                | Example                                      |
|-----------------------|----------------------|----------------------------------------------|
| `×`, `⋅`, `·`         | `*`                  | `3×4` = `12`                                 |
| `÷`                   | `/`                  | `10÷4` = `5/2`                               |
| `−` (minus sign)      | `-`                  | `5−2` = `3`                                  |
| `√`                   | `sqrt`               | `√9` = `3`, `√(x+1)` = `sqrt(x + 1)`         |
| `π`                   | `pi`                 | `2π` = `2*pi`                                |
| `α β γ δ ε θ λ μ φ ω` | the Greek symbols    | `α+β` = `alpha + beta`                       |
| superscripts          | `^` powers           | `x²` = `x^2`, `x⁻¹` = `1/x`, `2x³` = `2*x^3` |
| `°`                   | degrees, `*(pi/180)` | `sin(30°)` = `1/2`, `90°` = `pi/2`           |

Notes:

- `√` follows the same bare-argument rule as any function name (see
  [Applying functions](#applying-functions)): `√2x` is `sqrt(2*x)`, while
  `√x+1` is `sqrt(x) + 1`.
- The comparison signs `≤`, `≥`, `≠` are recognized just enough to give a
  targeted error: `inequalities are not supported yet (only '=' equations)`.
- Any other non-ASCII character is an ordinary parse error showing the
  character's bytes (e.g. `unexpected character '\xE2\x98\x83'`).

---

## Supported LaTeX commands

| Command                        | Meaning                                          |
|--------------------------------|--------------------------------------------------|
| `\frac{a}{b}`                  | `a / b`                                          |
| `\sqrt{x}`                     | `x^(1/2)`                                        |
| `\sqrt[n]{x}`                  | n-th root, `x^(1/n)`                             |
| `\cdot`, `\times`              | multiplication                                   |
| `\div`                         | division                                         |
| `\left( ... \right)`           | grouping; works with `(`, `{`, `[` and their closers — the pair must match |
| `\pi`                          | the constant π                                   |
| `\alpha` … `\omega`            | Greek symbols (list [above](#variables-subscripts-and-greek-letters)) |
| `\sin \cos \tan`               | trig functions                                   |
| `\arcsin \arccos \arctan`      | inverse trig                                     |
| `\sinh \cosh \tanh`            | hyperbolic functions                             |
| `\sec \csc \cot`               | reciprocals of cos / sin / tan                   |
| `\exp`                         | `exp(x)` = `e^x`                                 |
| `\ln`                          | natural logarithm                                |
| `\log`                         | logarithm, **base 10** when no base is given     |
| `\log_b`, `\log_{b}`           | logarithm with explicit base `b`                 |
| `\, \; \! \: \quad \qquad`     | spacing — ignored                                |

Extra notes:

- `\sin^2 x` — a power written directly on the function name — is supported
  and means `(sin x)^2`. As in standard notation, `\sin^{-1} x` is special:
  it means `asin(x)` (arcsine), **not** `1/sin(x)`.
- `\log_2 x` and plain `log_2(x)` both work; an explicit base can be any
  expression: `\log_{b} x`.
- Any **unknown command** (`\fraq`, `\alfa`, …) is a parse error pointing at
  the command — the parser never silently skips a backslash word.

---

## Applying functions

Three equivalent call forms:

```text
sin(x)        parenthesized (plain style)
\sin{x}       braced (LaTeX style)
\sin x        bare argument
```

**Bare arguments** are where the rules matter. If the token right after the
function name is a group — `(`, `{`, `\frac`, or `\sqrt` — the argument is
exactly that one group. Otherwise the argument is the following **tight
factor sequence**: one or more adjacent atoms (numbers, symbols, constants,
each with an optional `^` power), stopping at any of

```text
+  -  *  /  =  )  }  ]  ,       another function name, \frac, or end of input
```

Worked examples:

| Input             | Read as              | Why                                                    |
|-------------------|----------------------|--------------------------------------------------------|
| `\sin 2x`         | `sin(2x)`            | `2` and `x` are adjacent atoms — both are consumed     |
| `\sin x \cos y`   | `sin(x) * cos(y)`    | the argument stops at the next function name           |
| `\sin x + 1`      | `sin(x) + 1`         | the argument stops at `+`                              |
| `\sin x^2`        | `sin(x^2)`           | an atom in the sequence may carry a `^` power          |
| `\sin 2*x`        | `sin(2) * x`         | explicit `*` stops the argument — unlike implicit mult |
| `\sin^2 x`        | `sin(x)^2`           | power-on-the-name notation                             |
| `\sin^{-1} x`     | `asin(x)`            | the classic inverse-sine notation                      |

Note the `\sin 2x` vs `\sin 2*x` pair: implicit multiplication is *inside*
a bare argument, but an explicit `*` or `/` *ends* it. When in doubt, write
parentheses: `sin(2x)` says exactly what it means.

A function name with no argument at all (e.g. `sin + 1`) is a parse error.

---

## Absolute value

Three equivalent spellings:

```text
|x|                  bars (plain style)
\left|x\right|       bars (LaTeX style)
abs(x)               function form
```

A `|` in operand position opens an absolute value, and the next `|` at the
same level closes it. That covers everyday inputs — `|x - y|`, `|x| + |y|`,
`2|x|`, `|3 - π|` — but it means a bar cannot tell "open" from "close" in
every nesting: **immediately inside an open bar group, a `|` closes it**.
So `2|x|` is fine at the top level, but `|2|x||` is a parse error — after
`|2` the second bar closes the group. Use `abs()` or parentheses for the
inner bars when nesting is ambiguous:

```text
2|x|          fine at top level: 2*abs(x)
|2|x||        parse error — the inner | closes the outer group early
abs(2|x|)     fine: the bars now sit inside abs()'s parentheses
```

---

## Equations

An equation is two expressions joined by a single `=`:

```text
x^2 = 4
\frac{x}{2} = \sqrt{x+1}
```

- At most **one** `=` per input.
- `=` is only allowed at the top level — never inside parentheses or braces.

Anything without `=` is parsed as a plain expression.

---

## Assignments (REPL and web)

In the REPL (and the web app's Variables panel) you can bind a variable for
the session with `:=`:

```text
>>> a := 2
a := 2
>>> a*x + 3
2*x + 3
```

**`:=` is not part of the expression grammar.** The parser never sees it — a
bare `:` is still a parse error, and one-shot CLI subcommands
(`mathsolver simplify "x := 3"`) reject it the same way. Assignment is
recognized by the REPL's input layer, and everything to the right of `:=` is
one ordinary input from this page: an expression or an equation.

- **Targets** must be a name this grammar reads as a single symbol: a single
  letter (`x`), a Greek name (`alpha`), or a subscripted form (`x_1`, `E_1`,
  `x_{max}`). Words like `speed` stay errors (the
  [word guard](#how-multi-letter-words-are-read)) — use a subscripted name
  (`s_max := 5`). Constants (`pi`, `e`) and function names (`sin`) cannot be
  assigned. `E1 := ...` is an error with a hint: `E1` reads as `E*1`; write
  `E_1`.
- **Values** may be equations: `E_1 := x + y = 3` names an equation, which
  may then stand wherever a whole equation is expected (a bare input line,
  or a `;`-separated segment of a `solve`) — never inside an expression.
- Bindings resolve **lazily** at each use (`f := g + 1` before `g` exists is
  fine; redefining `g` updates `f`), and definitions that would create a
  cycle are rejected.
- `vars` lists the bindings, `unset <name>` removes one, `clear` removes
  all. `latex` and `debug` display the input as typed, without resolving.

For the stateless one-shot equivalent, use `subs`:
`mathsolver subs "a*x + 3" a=2`.

---

## Differences from real LaTeX and common pitfalls

1. **Multi-letter names are not variables.** `xy` is the product `x*y`, and
   a longer word like `speed` is a parse error (the
   [word guard](#how-multi-letter-words-are-read)): variables are single
   letters, optionally subscripted (`x_1`), or Greek names. The error shows
   the escape hatch — write `s*p*e*e*d` if you really meant the product.
   There is no `\mathrm{...}` or multi-letter identifier support.
2. **`e` is always Euler's number.** You cannot use `e` as a variable.
   Pick another letter, or a subscripted one like `E_1`.
3. **`2e3` is scientific notation** (= `2000`) — but only when digits
   directly follow the `e`. Bare `2e` is still `2 * e` with Euler's number,
   and `2e - 3` (with spaces) is the subtraction `2*e - 3`. See
   [Numbers](#numbers).
4. **`1/2x` means `(1/2)*x`,** not `1/(2x)`. Implicit multiplication binds
   exactly like `*`, not tighter. Write `1/(2x)` if that is what you mean.
5. **`-x^2` means `-(x^2)`** (the usual math convention), not `(-x)^2`.
6. **`^` is right-associative:** `2^3^2` = `2^(3^2)` = `512`, not `64`.
7. **Braces and brackets group.** `{x+1}` and `[x+1]` behave like `(x+1)`.
   In real LaTeX, `[...]` renders literal brackets; here it is grouping.
8. **Functions take exactly one argument, and commas are errors** inside an
   expression. There are no two-argument functions like `\max(a, b)`.
9. **`log` is base 10; `ln` is natural log.** Use `\log_b` / `log_2(x)` for
   other bases.
10. **`\sin^{-1} x` is arcsine,** not `1/sin(x)` — but `sec`, `csc`, `cot`
    *are* the reciprocals of cos, sin, tan.
11. **Bare function arguments stop at `+ - * / =`,** a comma, a closing
    delimiter, another function name, or `\frac`. `\sin x + 1` is
    `sin(x) + 1`, and `\sin 2*x` is `sin(2)*x`. Parenthesize when unsure.
12. **Only the listed LaTeX commands exist.** Anything else — including
    typos like `\fraq` — is a parse error, not passthrough text.
13. **Decimals are exact rationals.** `0.1` really is `1/10`, with no
    floating-point rounding.

---

## Error messages

Parse errors carry the exact span of the offending input, and the CLI prints
a caret diagnostic underneath the line you typed:

```text
error: unknown command '\fraq'
    \fraq{1}{2} + x
    ^~~~~
```

```text
error: unexpected ')'
    (x + ) * 2
         ^
```

```text
error: missing '}' after \frac numerator
    \frac{x + 1
               ^
```

The caret (`^`) marks where the problem starts and the tildes (`~`) cover the
rest of the offending span. Other inputs that raise parse errors include a
comma inside parentheses, a function name with no argument, a subscript on a
non-symbol (other than `\log_b`), a second `=`, mismatched `\left`/`\right`
delimiters, a word that would split into three or more single-letter
variables (`speed`), an inequality sign (`≤`, `≥`, `≠`), and a
scientific-notation literal too large for 64-bit rationals (`1e300`).
