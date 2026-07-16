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
[LaTeX commands](#supported-latex-commands) ·
[Applying functions](#applying-functions) ·
[Equations](#equations) ·
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

Two literal forms:

```text
42        integer
3.14      decimal
```

Decimals are stored **exactly** as rationals: `3.14` becomes `157/50`, not a
floating-point approximation.

There is **no scientific notation**. `2e3` is *not* 2000 — digits and letters
never join into one token, so it reads as `2 · e · 3` where `e` is Euler's
number (see [pitfalls](#differences-from-real-latex-and-common-pitfalls)).

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
| `foo`   | `f * o * o`    | no known names anywhere, so three variables                |
| `pie`   | `pi * e`       | `pi` matches first, then the constant `e`                  |
| `alpha` | symbol `alpha` | Greek names are known, so the whole word is one symbol     |

The practical consequences: **variables are single letters or Greek names**,
and if you write a word like `speed`, you get a product of letters, not a
variable named "speed". Use subscripts (`v_1`, `v_a`) or Greek names when one
letter is not enough.

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
2e3      =  2 * e * 3        no scientific notation; e is Euler's number
```

The exponent of `^` may itself be signed: `2^-3` is fine and means `2^(-3)`.

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

## Differences from real LaTeX and common pitfalls

1. **Multi-letter names split into products.** `xy` is `x*y`; `foo` is
   `f*o*o`. Variables are single letters, optionally subscripted, or Greek
   names. There is no `\mathrm{...}` or multi-letter identifier support.
2. **`e` is always Euler's number.** You cannot use `e` as a variable.
   Pick another letter, or a subscripted one like `E_1`.
3. **No scientific notation.** `2e3` means `2*e*3` (which simplifies to
   `6e`), not 2000. Write `2000` or `2*10^3`.
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
non-symbol (other than `\log_b`), a second `=`, and mismatched
`\left`/`\right` delimiters.
