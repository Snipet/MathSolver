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
  "tangent", "normal",
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
    const firstArg = splitTopLevelCommas(c.inner)[0] ?? "";
    s = s.slice(0, c.start) + "(" + firstArg + ")" + s.slice(c.end);
  }
  return { text: s, calledFns: [...called] };
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
