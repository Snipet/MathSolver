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
        insert: "discriminant",
        usage: "discriminant <polynomial>[, <var>]",
        hint: "Discriminant of a degree 2–4 polynomial (symbolic coefficients OK)",
        example: "discriminant a*x^2 + b*x + c, x",
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
