// User-defined-function support for graph/console expressions: the pure text
// scanning for `f(args)` / `f'(x)` call sites and the capture-avoiding
// placeholder machinery. This extends the calc-operator scanner (calculus.ts)
// to registered user functions, so one innermost-first loop (resolveRow in
// GraphCalculator) can interleave `diff(...)` / `integral(...)` expansion with
// user-function beta-reduction. Unit-tested (tools/graph_functions_test.mjs).
//
// Detection MUST be textual and pre-engine: the core parser turns an unknown
// `f(x)` into `f*x` (implicit multiplication) and rejects the apostrophe in
// `f'(x)`, so both are recognized here, before anything is parsed.
import { CALC_FNS } from "./calculus";
import { splitTopLevelCommas } from "./classify";

/**
 * Names a user function may NOT take: core builtins (function ids + aliases +
 * the parser's rewrites), constants, and the calc operators. Registering one
 * of these would shadow a real engine object at every call site.
 */
export const RESERVED_NAMES: ReadonlySet<string> = new Set([
  // builtins
  "sin", "cos", "tan", "asin", "acos", "atan", "arcsin", "arccos", "arctan",
  "sinh", "cosh", "tanh", "asinh", "acosh", "atanh",
  "ln", "log", "exp", "sqrt", "abs", "sec", "csc", "cot",
  "erf", "erfc", "gamma", "digamma", "psi", "conj", "conjugate",
  "re", "real", "im", "imag", "arg",
  "fib", "fibonacci", "harmonic", "binomial", "factorial",
  // constants
  "e", "i", "pi",
  // calc operators (CALC_FNS)
  "diff", "derivative", "integral", "antiderivative", "series", "taylor",
  "tangent", "normal", "sum", "product",
]);

/** A calc-operator or user-function call site found in a row. */
export interface AnyCall {
  kind: "calc" | "appl";
  name: string;
  /** Trailing apostrophes on the name (user functions only): f'(x) → 1. */
  primes: number;
  /** Raw argument text inside the parentheses. */
  inner: string;
  /** Index of the name / index just past the closing paren. */
  start: number;
  end: number;
}

/**
 * Does `name` begin a call at index `i`? Requires a left word boundary, then
 * (for user functions) an optional prime run, optional whitespace, then `(`.
 * Returns the prime count and the `(` index, or null.
 */
function callAt(
  text: string,
  i: number,
  name: string,
  allowPrime: boolean,
): { primes: number; parenAt: number } | null {
  if (text.slice(i, i + name.length) !== name) return null;
  const prev = text[i - 1];
  if (prev && /[A-Za-z0-9_]/.test(prev)) return null; // left word boundary
  let j = i + name.length;
  let primes = 0;
  if (allowPrime) while (text[j] === "'") (primes++, j++);
  while (j < text.length && /\s/.test(text[j])) j++;
  return text[j] === "(" ? { primes, parenAt: j } : null;
}

/** Escape a string for literal use inside a RegExp. */
function escapeRe(s: string): string {
  return s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

/** Match the closing paren for the `(` at index `open`, or -1 if unbalanced. */
function matchParen(text: string, open: number): number {
  let depth = 0;
  for (let k = open; k < text.length; k++) {
    if (text[k] === "(") depth++;
    else if (text[k] === ")") {
      depth--;
      if (depth === 0) return k;
    }
  }
  return -1;
}

/** Is there any calc or user-function call anywhere in `text`? */
function containsAnyCall(text: string, fnNames: readonly string[]): boolean {
  for (let i = 0; i < text.length; i++) {
    for (const n of CALC_FNS) if (callAt(text, i, n, false)) return true;
    for (const n of fnNames) if (callAt(text, i, n, true)) return true;
  }
  return false;
}

/**
 * The innermost call (calc OR user function) whose argument text contains no
 * further call of either kind — scanning left to right. Blocking on BOTH kinds
 * gives correct interleaving: `f(diff(x))` expands `diff` first (it is inner),
 * `diff(f(x))` expands `f` first. Null when there are none.
 */
export function findInnermostAny(text: string, fnNames: readonly string[]): AnyCall | null {
  for (let i = 0; i < text.length; i++) {
    for (const n of CALC_FNS) {
      const m = callAt(text, i, n, false);
      if (!m) continue;
      const close = matchParen(text, m.parenAt);
      if (close < 0) continue;
      const inner = text.slice(m.parenAt + 1, close);
      if (!containsAnyCall(inner, fnNames))
        return { kind: "calc", name: n, primes: 0, inner, start: i, end: close + 1 };
    }
    for (const n of fnNames) {
      const m = callAt(text, i, n, true);
      if (!m) continue;
      const close = matchParen(text, m.parenAt);
      if (close < 0) continue;
      const inner = text.slice(m.parenAt + 1, close);
      if (!containsAnyCall(inner, fnNames))
        return { kind: "appl", name: n, primes: m.primes, inner, start: i, end: close + 1 };
    }
  }
  return null;
}

/**
 * Replace every call (calc or user function) with `(firstArg)` — the analog of
 * calculus.ts `stripCalc`, so free-symbol `analyze` never sees a call. The
 * prime run is consumed with the call. Returns the stripped text plus the set
 * of user-function names that were called (so the caller can union in their
 * body's free parameters — the strip keeps only the argument's symbols).
 */
export function stripCalls(
  text: string,
  fnNames: readonly string[],
): { text: string; calledFns: string[] } {
  let s = text;
  const called = new Set<string>();
  for (let guard = 0; guard < 64; guard++) {
    const c = findInnermostAny(s, fnNames);
    if (!c) break;
    if (c.kind === "appl") called.add(c.name);
    let replacement: string;
    if (c.kind === "calc" && (c.name === "sum" || c.name === "product")) {
      // sum/product(term, index, lo, hi): the index is BOUND — it must not leak
      // out as a free symbol (would become a phantom slider). Neutralize it in
      // the term, then surface the term's remaining symbols plus lo/hi (which
      // may hold real sliders, e.g. the upper bound `n`).
      const p = splitTopLevelCommas(c.inner).map((x) => x.trim());
      const bv = p[1] ?? "";
      const termNoIdx = bv
        ? (p[0] ?? "").replace(new RegExp(`\\b${escapeRe(bv)}\\b`, "g"), "1")
        : (p[0] ?? "");
      replacement = `((${termNoIdx})+(${p[2] ?? ""})+(${p[3] ?? ""}))`;
    } else {
      replacement = "(" + (splitTopLevelCommas(c.inner)[0] ?? "") + ")";
    }
    s = s.slice(0, c.start) + replacement + s.slice(c.end);
  }
  return { text: s, calledFns: [...called] };
}

/** True if any calc operator (diff/integral/series/…) is called in `text`. A
 *  row without one expands purely by user-function beta-reduction, which is
 *  independent of the viewport and session variables — hence memoizable. */
export function hasCalcCall(text: string): boolean {
  for (let i = 0; i < text.length; i++) {
    for (const n of CALC_FNS) if (callAt(text, i, n, false)) return true;
  }
  return false;
}

/** True if any user-function call occurs in `text` (calc calls ignored). */
function containsApplCall(text: string, fnNames: readonly string[]): boolean {
  for (let i = 0; i < text.length; i++) {
    for (const n of fnNames) if (callAt(text, i, n, true)) return true;
  }
  return false;
}

/**
 * The innermost USER-FUNCTION call whose argument text contains no further
 * user-function call — calc operators are treated as opaque and left in place.
 * This is the console's application-only expander driver (the console does not
 * expand diff/integral inline, unlike the grapher's interleaved resolveRow).
 */
export function findInnermostAppl(text: string, fnNames: readonly string[]): AnyCall | null {
  for (let i = 0; i < text.length; i++) {
    for (const n of fnNames) {
      const m = callAt(text, i, n, true);
      if (!m) continue;
      const close = matchParen(text, m.parenAt);
      if (close < 0) continue;
      const inner = text.slice(m.parenAt + 1, close);
      if (!containsApplCall(inner, fnNames))
        return { kind: "appl", name: n, primes: m.primes, inner, start: i, end: close + 1 };
    }
  }
  return null;
}

/** Is there a prime-notation call `name'(…)` for one of `names` in `text`? */
function containsScalarPrime(text: string, names: readonly string[]): boolean {
  for (let i = 0; i < text.length; i++) {
    for (const n of names) {
      const m = callAt(text, i, n, true);
      if (m && m.primes > 0) return true;
    }
  }
  return false;
}

/**
 * The innermost prime-notation call `name'(arg)` whose `name` is a SCALAR
 * define — a `name = expr` row with no parameters, e.g. `f = x` — rather than a
 * registered function. Such a name is absent from fnNames(), so findInnermostAny
 * never sees it and the trailing apostrophe would otherwise reach the core
 * parser, which rejects it ("unexpected character '''"). The grapher rewrites
 * these to the derivative of the define's resolved body. Only names carrying at
 * least one prime match; a bare `f(x)` on a scalar define is left untouched
 * (implicit multiplication, as before). Innermost-first: the chosen call's
 * argument contains no further scalar-prime call.
 */
export function findScalarPrimeCall(
  text: string,
  scalarNames: readonly string[],
): AnyCall | null {
  for (let i = 0; i < text.length; i++) {
    for (const n of scalarNames) {
      const m = callAt(text, i, n, true);
      if (!m || m.primes === 0) continue; // prime notation only, not f(x)
      const close = matchParen(text, m.parenAt);
      if (close < 0) continue;
      const inner = text.slice(m.parenAt + 1, close);
      if (!containsScalarPrime(inner, scalarNames))
        return { kind: "appl", name: n, primes: m.primes, inner, start: i, end: close + 1 };
    }
  }
  return null;
}

/**
 * Replace each scalar-define prime call `f'(arg)` with `(arg)` — the analog of
 * stripCalls for scalar defines, used before free-symbol `analyze`. The callee
 * and its apostrophes must not reach the engine (which rejects the apostrophe);
 * the derivative is materialized later, at sampling time. The argument's own
 * symbols are kept (they may be real plot variables / sliders); the define's
 * body symbols are surfaced by its own `name = expr` row.
 */
export function stripScalarPrimes(text: string, scalarNames: readonly string[]): string {
  let s = text;
  for (let guard = 0; guard < 64; guard++) {
    const c = findScalarPrimeCall(s, scalarNames);
    if (!c) break;
    s = s.slice(0, c.start) + "(" + c.inner + ")" + s.slice(c.end);
  }
  return s;
}

/**
 * `count` fresh single-letter placeholder symbols that appear nowhere in
 * `avoid` (the concatenation of the body and all argument texts). Single
 * capital letters are used because they lex to a canonical symbol that prints
 * and re-lexes to the SAME name — unlike a subscripted `Q_{0}`, which the
 * engine canonicalizes to `Q_0`, breaking exact-string `subs` matching.
 */
export function freshPlaceholders(count: number, avoid: string): string[] {
  const used = new Set((avoid.match(/[A-Za-z]/g) ?? []));
  const pool = "ZQWJKVUXYHGNMBTRCPLOSDAF"; // uppercase, minus E and I
  const out: string[] = [];
  for (const ch of pool) {
    if (out.length === count) break;
    if (!used.has(ch)) out.push(ch);
  }
  if (out.length < count) throw new Error("too many parameters to reduce safely");
  return out;
}
