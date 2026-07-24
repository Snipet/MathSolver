// Console command reference: one source of truth for the reference panel,
// the autocomplete popup, and the inline usage hints. Built-in commands are
// static; plugin commands are appended from the live catalog. Every entry
// carries a concrete runnable example so the grammar can be learned by
// clicking, not reading.
import type { PluginMeta } from "../engine/types";
import { getCatalog } from "./run";

export interface RefItem {
  /** Text inserted into the prompt when picked (trailing space added). */
  insert: string;
  /** Full usage synopsis, e.g. "solve <equation>[, <variable>]". */
  usage: string;
  /** One-line description. */
  hint: string;
  /** A concrete runnable invocation, inserted verbatim when clicked. */
  example: string;
}

export interface RefGroup {
  title: string;
  items: RefItem[];
}

export const BUILTIN_GROUPS: RefGroup[] = [
  {
    title: "Compute",
    items: [
      {
        insert: "f(x) = ",
        usage: "<name>(<params>) = <body>   define a function",
        hint: "Define a function, then use f(3), f'(x), g(f(x)) — list them with funcs",
        example: "f(x) = x^2 - 3",
      },
      {
        insert: "simplify",
        usage: "simplify <expr>   (or just type a bare expression)",
        hint: "Simplify an expression",
        example: "simplify sin(x)^2 + cos(x)^2",
      },
      {
        insert: "expand",
        usage: "expand <expr>",
        hint: "Multiply everything out",
        example: "expand (x+1)^3",
      },
      {
        insert: "factor",
        usage: "factor <expr | integer>",
        hint: "Factor a polynomial, or an integer into primes",
        example: "factor 360",
      },
      {
        insert: "trigexpand",
        usage: "trigexpand <expr>",
        hint: "Expand trig of sums/multiples: sin(a+b), cos(2x), …",
        example: "trigexpand sin(a + b)",
      },
      {
        insert: "trigreduce",
        usage: "trigreduce <expr>",
        hint: "Products/powers of sin,cos → multiple angles (sin(x)² → …)",
        example: "trigreduce sin(x)^2",
      },
      {
        insert: "logexpand",
        usage: "logexpand <expr>",
        hint: "ln of products/quotients/powers → sums (ln(x·y) → ln x + ln y)",
        example: "logexpand ln(x*y)",
      },
      {
        insert: "logcombine",
        usage: "logcombine <expr>",
        hint: "Sum of logs → a single log (ln x + ln y → ln(x·y))",
        example: "logcombine ln(x) + ln(y)",
      },
      {
        insert: "cancel",
        usage: "cancel <expr>",
        hint: "Cancel a rational's common polynomial factor",
        example: "cancel (x^2 - 1)/(x - 1)",
      },
      {
        insert: "together",
        usage: "together <expr>",
        hint: "Combine a sum of fractions over one denominator",
        example: "together 1/x + 1/y",
      },
      {
        insert: "collect",
        usage: "collect <expr>[, <var>]",
        hint: "Regroup as a polynomial in one variable",
        example: "collect x*y + 2x + y*x^2, x",
      },
      {
        insert: "apart",
        usage: "apart <expr>[, <var>]",
        hint: "Partial-fraction expansion of a rational function",
        example: "apart (3x+2)/((x+1)(x+2))",
      },
      {
        insert: "latex",
        usage: "latex <expr>",
        hint: "Convert to LaTeX, unsimplified",
        example: "latex sqrt(x)/2",
      },
      {
        insert: "plot",
        usage: "plot <expr>[, <lo>, <hi>]",
        hint: "Sample and chart an expression",
        example: "plot sin(x)/x, -20, 20",
      },
    ],
  },
  {
    title: "Number theory",
    items: [
      {
        insert: "gcd",
        usage: "gcd <a, b, …>",
        hint: "Greatest common divisor of a list of integers (exact)",
        example: "gcd 48, 36",
      },
      {
        insert: "lcm",
        usage: "lcm <a, b, …>",
        hint: "Least common multiple of a list of integers (exact)",
        example: "lcm 4, 6, 8",
      },
      {
        insert: "isprime",
        usage: "isprime <n>",
        hint: "Deterministic primality test (all 64-bit integers)",
        example: "isprime 97",
      },
      {
        insert: "nextprime",
        usage: "nextprime <n>",
        hint: "Smallest prime greater than n",
        example: "nextprime 100",
      },
      {
        insert: "divisors",
        usage: "divisors <n>",
        hint: "All positive divisors of n, ascending",
        example: "divisors 360",
      },
      {
        insert: "totient",
        usage: "totient <n>",
        hint: "Euler's φ(n): count of integers ≤ n coprime to n",
        example: "totient 36",
      },
      {
        insert: "sigma",
        usage: "sigma <n> [, <k>]",
        hint: "Divisor function σₖ(n): sum of the k-th powers of divisors (default k=1)",
        example: "sigma 12",
      },
      {
        insert: "mobius",
        usage: "mobius <n>",
        hint: "Möbius μ(n): 0 if n has a squared factor, else ±1 by prime count",
        example: "mobius 30",
      },
      {
        insert: "partitions",
        usage: "partitions <n>",
        hint: "Integer partition count p(n): ways to write n as a sum of positive integers",
        example: "partitions 10",
      },
      {
        insert: "catalan",
        usage: "catalan <n>",
        hint: "The n-th Catalan number C(n) = binomial(2n, n) / (n + 1)",
        example: "catalan 8",
      },
      {
        insert: "bernoulli",
        usage: "bernoulli <n>",
        hint: "The n-th Bernoulli number Bₙ as an exact rational (0 ≤ n ≤ 20)",
        example: "bernoulli 12",
      },
      {
        insert: "stirling2",
        usage: "stirling2 <n>, <k>",
        hint: "Stirling number of the second kind S(n, k): partitions of an n-set into k nonempty blocks",
        example: "stirling2 5, 3",
      },
      {
        insert: "bell",
        usage: "bell <n>",
        hint: "The n-th Bell number Bₙ: the number of partitions of an n-element set",
        example: "bell 8",
      },
      {
        insert: "derangement",
        usage: "derangement <n>",
        hint: "The subfactorial !n: permutations of n items with no fixed point",
        example: "derangement 6",
      },
      {
        insert: "lucas",
        usage: "lucas <n>",
        hint: "The n-th Lucas number L(n): companion to Fibonacci, L₀=2, L₁=1",
        example: "lucas 10",
      },
      {
        insert: "primorial",
        usage: "primorial <n>",
        hint: "The primorial n#: the product of all primes ≤ n",
        example: "primorial 13",
      },
      {
        insert: "motzkin",
        usage: "motzkin <n>",
        hint: "The n-th Motzkin number M(n): non-crossing chords on n circle points",
        example: "motzkin 10",
      },
      {
        insert: "euler",
        usage: "euler <n>",
        hint: "The n-th Euler (secant) number Eₙ: E₀=1, E₂=−1, E₄=5, odd n = 0",
        example: "euler 8",
      },
      {
        insert: "tribonacci",
        usage: "tribonacci <n>",
        hint: "The n-th tribonacci number: T₀=T₁=0, T₂=1, each term the sum of the previous three",
        example: "tribonacci 10",
      },
      {
        insert: "pell",
        usage: "pell <n>",
        hint: "The n-th Pell number: P₀=0, P₁=1, P(n)=2·P(n−1)+P(n−2) — numerators of the √2 convergents",
        example: "pell 10",
      },
      {
        insert: "cfrac",
        usage: "cfrac <rational | sqrt(n) | real>",
        hint: "Continued fraction + convergents (best rational approximations)",
        example: "cfrac 355/113",
      },
      {
        insert: "powmod",
        usage: "powmod <base>, <exponent>, <modulus>",
        hint: "Modular exponentiation — handles huge exponents exactly",
        example: "powmod 7, 100, 13",
      },
      {
        insert: "modinv",
        usage: "modinv <a>, <m>",
        hint: "Modular inverse a⁻¹ mod m (extended Euclid)",
        example: "modinv 3, 11",
      },
      {
        insert: "mod",
        usage: "mod <a>, <m>",
        hint: "Euclidean remainder in [0, m)",
        example: "mod 17, 5",
      },
      {
        insert: "crt",
        usage: "crt <r1, r2, …; m1, m2, …>",
        hint: "Chinese remainder theorem (allows non-coprime moduli)",
        example: "crt 2, 3, 2; 3, 5, 7",
      },
    ],
  },
  {
    title: "Calculus",
    items: [
      {
        insert: "diff",
        usage: "diff <expr>[, <var>]",
        hint: "Symbolic derivative",
        example: "diff sin(x^2), x",
      },
      {
        insert: "steps",
        usage: "steps [diff|integrate] <expr>[, <var>]",
        hint: "Worked, rule-by-rule derivative (power/product/chain/…)",
        example: "steps sin(x^2), x",
      },
      {
        insert: "steps integrate",
        usage: "steps integrate <expr>[, <var>]",
        hint: "Worked integral (linearity, u-substitution, parts, …)",
        example: "steps integrate x*sin(x), x",
      },
      {
        insert: "integrate",
        usage: "integrate <expr>[, <var>[, <lo>, <hi>]]",
        hint: "Antiderivative, or a definite integral with bounds",
        example: "integrate e^(-x^2), x, 0, 1",
      },
      {
        insert: "laplace",
        usage: "laplace <expr>[, <t>]",
        hint: "Laplace transform f(t) → F(s)",
        example: "laplace e^(-t) sin(2t)",
      },
      {
        insert: "ilaplace",
        usage: "ilaplace <expr>[, <s>]",
        hint: "Inverse Laplace F(s) → f(t)",
        example: "ilaplace 1/(s^2 + 2s + 5)",
      },
      {
        insert: "dsolve",
        usage: "dsolve <ode>[, y(0)=v, y'(0)=v, …]",
        hint: "Solve a linear ODE initial-value problem exactly",
        example: "dsolve y'' + 3y' + 2y = e^(-t), y(0)=1, y'(0)=0",
      },
      {
        insert: "series",
        usage: "series <expr>[, <var>[, <center>[, <order>]]]",
        hint: "Taylor expansion (center 0, order 6 by default)",
        example: "series sin(x), x, 0, 5",
      },
      {
        insert: "pade",
        usage: "pade <expr>, <m>, <n>[, <var>]",
        hint: "[m/n] Padé approximant — rational fit of the Maclaurin series",
        example: "pade exp(x), 2, 2",
      },
      {
        insert: "rootcount",
        usage: "rootcount <poly>[, <var>[, <lo>, <hi>]]",
        hint: "Distinct real roots of a polynomial (Sturm's theorem), exactly",
        example: "rootcount x^5 - 3x + 1",
      },
      {
        insert: "isolate",
        usage: "isolate <poly>[, <var>]",
        hint: "Isolate each real root in a rational interval (exact rationals shown)",
        example: "isolate x^3 - x - 1",
      },
      {
        insert: "discriminant",
        usage: "discriminant <polynomial>[, <var>]",
        hint: "Discriminant of a degree 2–4 polynomial (symbolic coefficients OK)",
        example: "discriminant a*x^2 + b*x + c, x",
      },
      {
        insert: "polydiv",
        usage: "polydiv <dividend>, <divisor>[, <var>]",
        hint: "Polynomial long division → quotient and remainder",
        example: "polydiv x^3 - 1, x - 1",
      },
      {
        insert: "polygcd",
        usage: "polygcd <a>, <b>[, <var>]",
        hint: "Monic greatest common divisor of two polynomials",
        example: "polygcd x^2 - 1, x^3 - 1",
      },
      {
        insert: "polylcm",
        usage: "polylcm <a>, <b>[, <var>]",
        hint: "Monic least common multiple of two polynomials",
        example: "polylcm x - 1, x + 1",
      },
      {
        insert: "resultant",
        usage: "resultant <a>, <b>[, <var>]",
        hint: "Resultant of two polynomials (0 iff they share a root)",
        example: "resultant x^2 - 1, x - 2",
      },
      {
        insert: "bezout",
        usage: "bezout <a>, <b>[, <var>]",
        hint: "Extended gcd: monic gcd + cofactors s, t with s·a + t·b = gcd",
        example: "bezout x^2 - 1, x^3 - 1",
      },
      {
        insert: "companion",
        usage: "companion <polynomial>[, <var>]",
        hint: "Companion matrix of a polynomial (its eigenvalues are the roots)",
        example: "companion x^3 - 2x + 1",
      },
      {
        insert: "vandermonde",
        usage: "vandermonde <x1, x2, x3, ...>",
        hint: "Vandermonde matrix of a node list (the interpolation matrix)",
        example: "vandermonde 1, 2, 3",
      },
      {
        insert: "limit",
        usage: "limit <expr>, <var>, <point>[, left|right]",
        hint: "Limit at a point or ±inf (exact where possible)",
        example: "limit sin(x)/x, x, 0",
      },
      {
        insert: "mlimit",
        usage: "mlimit <expr>, <x>, <a>, <y>, <b>",
        hint: "Two-variable limit by path sampling (rays + parabolas)",
        example: "mlimit x*y/(x^2+y^2), x, 0, y, 0",
      },
      {
        insert: "sum",
        usage: "sum <term>, <var>, <lo>, <hi>",
        hint: "Closed-form summation (hi may be a symbol or inf)",
        example: "sum 1/k, k, 1, n",
      },
      {
        insert: "product",
        usage: "product <term>, <var>, <lo>, <hi>",
        hint: "Closed-form or exact numeric product",
        example: "product k, k, 1, 5",
      },
      {
        insert: "rsolve",
        usage: "rsolve <recurrence>[, a(0)=v, …]",
        hint: "Solve a linear recurrence (e.g. Fibonacci → Binet)",
        example: "rsolve a(n+2) = a(n+1) + a(n), a(0)=0, a(1)=1",
      },
      {
        insert: "fit",
        usage: "fit <x,y; x,y; …> [| <model> [degree]]",
        hint: "Least-squares regression — polynomial fits are exact; also exp/power/log",
        example: "fit 0,1; 1,2; 2,2; 3,4 | linear",
      },
      {
        insert: "interp",
        usage: "interp <x,y; x,y; …>",
        hint: "Exact polynomial through the points (Vandermonde over the rationals)",
        example: "interp 1,1; 2,4; 3,9",
      },
      {
        insert: "newton",
        usage: "newton <x,y; x,y; …>",
        hint: "Interpolant in Newton divided-difference form (kept factored); also lagrange",
        example: "newton 1,1; 2,4; 3,9",
      },
      {
        insert: "chebyshev",
        usage: "chebyshev <n> [, <var>]",
        hint: "Exact Chebyshev T_n (first kind); also chebyu, legendre, hermite, laguerre",
        example: "chebyshev 5",
      },
      {
        insert: "legendre",
        usage: "legendre <n> [, <var>]",
        hint: "Exact Legendre polynomial P_n over the rationals",
        example: "legendre 4",
      },
      {
        insert: "stats",
        usage: "stats <v1, v2, v3, …>",
        hint: "Exact summary statistics — mean, median, quartiles, stdev (fractions & radicals)",
        example: "stats 1, 2, 3, 4, 5",
      },
      {
        insert: "stirling",
        usage: "stirling [<var>[, <terms>]]",
        hint: "Stirling series for ln Γ with exact Bernoulli coefficients",
        example: "stirling x, 3",
      },
      {
        insert: "seq",
        usage: "seq <a0>, <a1>, <a2>, <a3>[, …]",
        hint: "Recognize a sequence (arithmetic/geometric/polynomial/recurrence)",
        example: "seq 0, 1, 1, 2, 3, 5, 8",
      },
    ],
  },
  {
    title: "Vector calculus",
    items: [
      {
        insert: "grad",
        usage: "grad <f>, <var>[, <var> …]",
        hint: "Gradient ∇f",
        example: "grad x^2 + y^2, x, y",
      },
      {
        insert: "div",
        usage: "div <F1; F2; …>, <var> …",
        hint: "Divergence ∇·F (';'-separated field)",
        example: "div x*y; y*z; z*x, x, y, z",
      },
      {
        insert: "curl",
        usage: "curl <F1; F2[; F3]>, <var> …",
        hint: "Curl ∇×F (3-D) or scalar curl (2-D)",
        example: "curl -y; x; 0, x, y, z",
      },
      {
        insert: "laplacian",
        usage: "laplacian <f>, <var> …",
        hint: "Laplacian ∇²f",
        example: "laplacian x^2 + y^2, x, y",
      },
      {
        insert: "jacobian",
        usage: "jacobian <F1; F2; …>, <var> …",
        hint: "Jacobian matrix ∂F_i/∂x_j",
        example: "jacobian x*y; x + y, x, y",
      },
      {
        insert: "hessian",
        usage: "hessian <f>, <var> …",
        hint: "Hessian matrix ∂²f/∂x_i∂x_j",
        example: "hessian x^3 + x*y^2, x, y",
      },
      {
        insert: "vecfield",
        usage: "vecfield <Fx>; <Fy>[, <xlo>, <xhi>, <ylo>, <yhi>]",
        hint: "Quiver plot of a planar vector field",
        example: "vecfield -y; x",
      },
    ],
  },
  {
    title: "Tools",
    items: [
      {
        insert: "wave",
        usage: "wave [<columns>][, <boundary>]",
        hint: "Interactive 2D wave field — click to pluck, drag to add energy",
        example: "wave",
      },
    ],
  },
  {
    title: "Solve & evaluate",
    items: [
      {
        insert: "solve",
        usage: "solve <equation | inequality>[, <var>]  ·  solve <eq>; <eq>[, <vars…>]",
        hint: "Equations, inequalities (→ intervals), and ;-separated linear systems",
        example: "solve x^2 < 4",
      },
      {
        insert: "eval",
        usage: "eval <expr>, x=1[, y=2 …]",
        hint: "Numeric value with variable bindings",
        example: "eval x^2 + y, x=3, y=0.5",
      },
      {
        insert: "subs",
        usage: "subs <expr>, x=y+1[, …]",
        hint: "Substitute expressions, then simplify",
        example: "subs a*x + 3, a=2",
      },
    ],
  },
  {
    title: "Session variables",
    items: [
      {
        insert: ":=",
        usage: "<name> := <value>   e.g.  a := 2   E_1 := x + y = 3",
        hint: "Bind a variable; applies to later lines",
        example: "a := 2",
      },
      {
        insert: "vars",
        usage: "vars",
        hint: "List current bindings",
        example: "vars",
      },
      {
        insert: "unset",
        usage: "unset <name>",
        hint: "Remove one binding",
        example: "unset a",
      },
      {
        insert: "clear",
        usage: "clear",
        hint: "Remove all bindings",
        example: "clear",
      },
    ],
  },
  {
    title: "Notebooks",
    items: [
      {
        insert: "save",
        usage: "save <name>",
        hint: "Save this session's commands as a notebook",
        example: "save demo",
      },
      {
        insert: "open",
        usage: "open <name>",
        hint: "Load a notebook's commands into the console (without running)",
        example: "open demo",
      },
      {
        insert: "run",
        usage: "run <name>",
        hint: "Run a notebook top-to-bottom in a fresh variable scope",
        example: "run demo",
      },
      {
        insert: "notebooks",
        usage: "notebooks",
        hint: "List saved notebooks",
        example: "notebooks",
      },
    ],
  },
  {
    title: "Console",
    items: [
      {
        insert: "help",
        usage: "help",
        hint: "Show the full grammar",
        example: "help",
      },
      {
        insert: "plugins",
        usage: "plugins",
        hint: "List compiled-in plugins",
        example: "plugins",
      },
    ],
  },
];

/** Plugin catalog rendered as reference groups (one per plugin). */
export function pluginGroups(catalog: PluginMeta[]): RefGroup[] {
  return catalog.map((p) => ({
    title: `Plugin: ${p.name} ${p.version}`,
    items: p.commands.map((c) => ({
      insert: `${p.name}.${c.name}`,
      usage: c.usage,
      hint: c.summary,
      example: c.example,
    })),
  }));
}

/** Everything, for the reference panel. */
export async function allGroups(): Promise<RefGroup[]> {
  try {
    return [...BUILTIN_GROUPS, ...pluginGroups(await getCatalog())];
  } catch {
    return BUILTIN_GROUPS;
  }
}

/**
 * Flat completion list for the prompt: every insertable command word
 * (excluding `:=`, which is not a leading token).
 */
export async function completionItems(): Promise<RefItem[]> {
  const groups = await allGroups();
  const out: RefItem[] = [];
  for (const g of groups) {
    for (const it of g.items) {
      if (it.insert !== ":=") out.push(it);
    }
  }
  return out;
}
