// Pure text layer for grapher lists: `[1, 2, 3]` literals, `[a...b]` /
// `[a, b...c]` ranges, and the machinery for broadcasting an expression over a
// named list element-wise. Numeric evaluation is engine-backed and lives in
// GraphCalculator; everything here is side-effect-free and unit-tested
// (tools/graph_lists_test.mjs).
import { splitTopLevelCommas } from "./classify";

/** True if `text` is a single top-level bracket list `[ … ]` (not `[…][…]`). */
export function isBracketList(text: string): boolean {
  const t = text.trim();
  if (!t.startsWith("[") || !t.endsWith("]") || t.length < 2) return false;
  let depth = 0;
  for (let i = 0; i < t.length; i++) {
    const c = t[i];
    if (c === "[") depth++;
    else if (c === "]") {
      depth--;
      if (depth === 0 && i !== t.length - 1) return false; // closed early → `[…][…]`
    }
  }
  return depth === 0;
}

/** The inside of a bracket list (assumes `isBracketList`). */
export function listInside(text: string): string {
  const t = text.trim();
  return t.slice(1, -1);
}

export type ListSpec =
  | { kind: "literal"; items: string[] }
  | { kind: "range"; from: string; step: string | null; to: string }
  | { kind: "comprehension"; body: string; varName: string; source: string };

/** Find a top-level `...` (ellipsis) in `inside`, returning the split or null. */
function splitEllipsis(inside: string): { before: string; after: string } | null {
  let depth = 0;
  for (let i = 0; i < inside.length; i++) {
    const c = inside[i];
    if (c === "(" || c === "[" || c === "{") depth++;
    else if (c === ")" || c === "]" || c === "}") depth = Math.max(0, depth - 1);
    else if (depth === 0 && c === "." && inside.startsWith("...", i)) {
      return { before: inside.slice(0, i), after: inside.slice(i + 3) };
    }
  }
  return null;
}

/** Find a top-level ` for ` keyword in a comprehension body, or -1. */
function forKeywordAt(inside: string): number {
  let depth = 0;
  for (let i = 0; i < inside.length; i++) {
    const c = inside[i];
    if (c === "(" || c === "[" || c === "{") depth++;
    else if (c === ")" || c === "]" || c === "}") depth = Math.max(0, depth - 1);
    else if (depth === 0 && /\s/.test(c) && inside.slice(i + 1, i + 4) === "for" && /\s/.test(inside[i + 4] ?? " ")) {
      return i;
    }
  }
  return -1;
}

/** Parse the inside of `[ … ]` into a literal list, a range, or a comprehension. */
export function parseListBody(inside: string): ListSpec {
  // Comprehension: `body for var = source` (source is itself a list expr).
  const fi = forKeywordAt(inside);
  if (fi >= 0) {
    const body = inside.slice(0, fi).trim();
    const rest = inside.slice(fi + 4).trim();
    const eq = rest.indexOf("=");
    if (body && eq > 0) {
      const varName = rest.slice(0, eq).trim();
      const source = rest.slice(eq + 1).trim();
      if (/^[A-Za-z][A-Za-z0-9_]*$/.test(varName) && source) {
        return { kind: "comprehension", body, varName, source };
      }
    }
  }
  const dots = splitEllipsis(inside);
  if (dots) {
    const head = splitTopLevelCommas(dots.before).map((s) => s.trim()).filter(Boolean);
    const to = dots.after.trim();
    if (to) {
      if (head.length === 1) return { kind: "range", from: head[0], step: null, to };
      // `a, b ... c`: the step is the gap between the first two listed terms.
      if (head.length === 2) return { kind: "range", from: head[0], step: `(${head[1]}) - (${head[0]})`, to };
    }
  }
  const items = splitTopLevelCommas(inside).map((s) => s.trim()).filter((s) => s.length > 0);
  return { kind: "literal", items };
}

/** A `name = [ … ]` list definition, or null. */
export function matchListDef(text: string): { name: string; inside: string } | null {
  const eq = text.indexOf("=");
  if (eq < 0) return null;
  const lhs = text.slice(0, eq).trim();
  const rhs = text.slice(eq + 1).trim();
  if (!/^[A-Za-z][A-Za-z0-9_]*$/.test(lhs)) return null;
  if (["x", "y", "r"].includes(lhs)) return null;
  if (!isBracketList(rhs)) return null;
  return { name: lhs, inside: listInside(rhs) };
}

/** Materialize a numeric range `from, from±step, …` up to (and including) `to`. */
export function rangeValues(from: number, step: number, to: number, cap = 100000): number[] {
  if (![from, step, to].every(Number.isFinite)) return [];
  const s = step === 0 ? (to >= from ? 1 : -1) : step;
  if ((to - from) * s < 0) return []; // step points away from `to` → empty
  const n = Math.floor((to - from) / s + 1e-9) + 1;
  if (n <= 0) return [];
  const out: number[] = [];
  const count = Math.min(n, cap);
  for (let i = 0; i < count; i++) out.push(from + i * s);
  return out;
}

/** Which of `names` occur as whole-word identifiers in `expr`. */
export function referencedLists(expr: string, names: readonly string[]): string[] {
  return names.filter((n) => new RegExp(`(^|[^A-Za-z0-9_])${escapeRe(n)}([^A-Za-z0-9_]|$)`).test(expr));
}

/** Replace every whole-word `name` in `expr` with `(replacement)`. */
export function substIdent(expr: string, name: string, replacement: string): string {
  return expr.replace(
    new RegExp(`(^|[^A-Za-z0-9_])${escapeRe(name)}([^A-Za-z0-9_]|$)`, "g"),
    (_m, a, b) => `${a}(${replacement})${b}`,
  );
}

function escapeRe(s: string): string {
  return s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

// --- list operations (Phase 2) ---------------------------------------------

/** Scalar reductions of a list. Names chosen to match Desmos where possible. */
export const AGGREGATES: Record<string, (xs: number[]) => number> = {
  total: (xs) => xs.reduce((a, b) => a + b, 0),
  mean: (xs) => (xs.length ? xs.reduce((a, b) => a + b, 0) / xs.length : NaN),
  min: (xs) => (xs.length ? Math.min(...xs) : NaN),
  max: (xs) => (xs.length ? Math.max(...xs) : NaN),
  length: (xs) => xs.length,
  count: (xs) => xs.length,
  median: (xs) => {
    if (!xs.length) return NaN;
    const s = [...xs].sort((a, b) => a - b);
    const m = s.length >> 1;
    return s.length % 2 ? s[m] : (s[m - 1] + s[m]) / 2;
  },
  stdev: (xs) => {
    const n = xs.length;
    if (n < 2) return NaN;
    const m = xs.reduce((a, b) => a + b, 0) / n;
    return Math.sqrt(xs.reduce((a, b) => a + (b - m) ** 2, 0) / (n - 1)); // sample sd
  },
  stdevp: (xs) => {
    const n = xs.length;
    if (!n) return NaN;
    const m = xs.reduce((a, b) => a + b, 0) / n;
    return Math.sqrt(xs.reduce((a, b) => a + (b - m) ** 2, 0) / n); // population sd
  },
};
export const AGG_NAMES: readonly string[] = Object.keys(AGGREGATES);

export interface FnCall {
  name: string;
  inner: string;
  start: number;
  end: number;
}

function matchClose(text: string, open: number, oc: string, cc: string): number {
  let depth = 0;
  for (let k = open; k < text.length; k++) {
    if (text[k] === oc) depth++;
    else if (text[k] === cc) {
      depth--;
      if (depth === 0) return k;
    }
  }
  return -1;
}

function wordStart(text: string, i: number, name: string): number {
  // The `(` after `name` (skipping spaces), or -1. Requires a left boundary.
  if (text.slice(i, i + name.length) !== name) return -1;
  const prev = text[i - 1];
  if (prev && /[A-Za-z0-9_]/.test(prev)) return -1;
  let j = i + name.length;
  while (j < text.length && /\s/.test(text[j])) j++;
  return text[j] === "(" ? j : -1;
}

/** Is `arg` a list-valued expression: a `[ … ]` list or a reference to a
 *  known list name? (Used to tell an aggregate `mean(L)` apart from a scalar
 *  builtin like `max(x, 2)`.) */
export function isListArg(arg: string, listNames: readonly string[]): boolean {
  const a = arg.trim();
  return isBracketList(a) || referencedLists(a, listNames).length > 0;
}

/** True if any aggregate call over a list appears in `text`. */
function containsListAgg(text: string, names: readonly string[], listNames: readonly string[]): boolean {
  return findAggCall(text, names, listNames) !== null;
}

/**
 * The innermost aggregate-over-a-list call `AGG([list])` — its single argument
 * is a list and holds no further list-aggregate — scanning left to right. A
 * scalar call like `max(x, 2)` (arg not a list, or multiple args) is skipped, so
 * `max(x, 2) + mean(L)` still finds `mean(L)`. Null when there are none.
 */
export function findAggCall(
  text: string,
  names: readonly string[],
  listNames: readonly string[],
): FnCall | null {
  for (let i = 0; i < text.length; i++) {
    for (const n of names) {
      const paren = wordStart(text, i, n);
      if (paren < 0) continue;
      const close = matchClose(text, paren, "(", ")");
      if (close < 0) continue;
      const inner = text.slice(paren + 1, close);
      const args = splitTopLevelCommas(inner);
      if (args.length !== 1 || !isListArg(args[0], listNames)) continue; // scalar call — skip
      if (!containsListAgg(inner, names, listNames)) return { name: n, inner, start: i, end: close + 1 };
    }
  }
  return null;
}

export interface IndexRef {
  name: string;
  idx: string;
  start: number;
  end: number;
}

/** A scalar list index `NAME[i]` for one of `names` (innermost, NOT a `a...b`
 *  slice — those are list-valued and handled by evalListArray), or null. */
export function findIndex(text: string, names: readonly string[]): IndexRef | null {
  for (let i = 0; i < text.length; i++) {
    for (const n of names) {
      if (text.slice(i, i + n.length) !== n) continue;
      const prev = text[i - 1];
      if (prev && /[A-Za-z0-9_]/.test(prev)) continue;
      let j = i + n.length;
      while (j < text.length && /\s/.test(text[j])) j++;
      if (text[j] !== "[") continue;
      const close = matchClose(text, j, "[", "]");
      if (close < 0) continue;
      const idx = text.slice(j + 1, close);
      if (!idx.includes("[") && !idx.includes("...")) return { name: n, idx, start: i, end: close + 1 };
    }
  }
  return null;
}

// --- list-returning operations (sort / unique / reverse / join, slices) -----

/** Pure list→list transforms (single-list). `join` is variadic → handled in
 *  the evaluator by concatenating its argument lists. */
export const LIST_XFORM: Record<string, (xs: number[]) => number[]> = {
  sort: (xs) => [...xs].sort((a, b) => a - b),
  reverse: (xs) => [...xs].reverse(),
  unique: (xs) => {
    const seen = new Set<number>();
    const out: number[] = [];
    for (const v of xs) if (!seen.has(v)) (seen.add(v), out.push(v));
    return out;
  },
};
export const LIST_FNS: readonly string[] = ["sort", "unique", "reverse", "join"];

/** The innermost list-returning call `sort(L)` / `join(A, B)` whose arguments
 *  hold no further list-fn call — every argument must be list-valued (so a
 *  scalar call is skipped). Used by listSurrogate & hasListOps. */
export function findListFnCall(text: string, listNames: readonly string[]): FnCall | null {
  for (let i = 0; i < text.length; i++) {
    for (const n of LIST_FNS) {
      const paren = wordStart(text, i, n);
      if (paren < 0) continue;
      const close = matchClose(text, paren, "(", ")");
      if (close < 0) continue;
      const inner = text.slice(paren + 1, close);
      const args = splitTopLevelCommas(inner);
      if (!args.length || !args.every((a) => isListArg(a, listNames))) continue;
      if (!findListFnCall(inner, listNames)) return { name: n, inner, start: i, end: close + 1 };
    }
  }
  return null;
}

/** If the WHOLE trimmed `text` is a single list-fn call `name( … )`, its name
 *  and argument list; else null. (Nesting resolves via recursion in the
 *  evaluator, so this needn't check the args are lists.) */
export function parseWholeListFn(text: string): { name: string; args: string[] } | null {
  const t = text.trim();
  for (const n of LIST_FNS) {
    const paren = wordStart(t, 0, n);
    if (paren < 0) continue;
    const close = matchClose(t, paren, "(", ")");
    if (close !== t.length - 1) continue; // the call must span the whole string
    return { name: n, args: splitTopLevelCommas(t.slice(paren + 1, close)).map((a) => a.trim()) };
  }
  return null;
}

/** If the WHOLE trimmed `text` is a slice `name[a...b]`, its parts; else null. */
export function parseWholeSlice(text: string): { name: string; from: string; to: string } | null {
  const t = text.trim();
  const m = /^([A-Za-z][A-Za-z0-9_]*)\s*\[(.*)\]$/.exec(t);
  if (!m) return null;
  const inner = m[2];
  if (inner.includes("[")) return null;
  const dots = inner.indexOf("...");
  if (dots < 0) return null;
  return { name: m[1], from: inner.slice(0, dots).trim(), to: inner.slice(dots + 3).trim() };
}

/** Does `text` use any list operation (bracket, aggregate, or list reference)? */
export function hasListOps(text: string, listNames: readonly string[]): boolean {
  if (text.includes("[")) return true;
  if (findAggCall(text, AGG_NAMES, listNames) !== null) return true;
  return referencedLists(text, listNames).length > 0;
}

/** Replace each top-level `[ … ]` in `text` via `map(inside)`, innermost first. */
function mapBrackets(text: string, map: (inside: string) => string): string {
  let s = text;
  for (let g = 0; g < 200; g++) {
    // find an innermost bracket (no nested `[` in its body)
    let open = -1;
    for (let i = 0; i < s.length; i++) {
      if (s[i] === "[") open = i;
      else if (s[i] === "]" && open >= 0) {
        s = s.slice(0, open) + `(${map(s.slice(open + 1, i))})` + s.slice(i + 1);
        open = -2;
        break;
      }
    }
    if (open !== -2) break; // no bracket replaced this pass
  }
  return s;
}

/**
 * A scalar-valued surrogate of `text` for free-variable analysis: every list
 * operation is collapsed to the scalar sub-expressions whose symbols matter
 * (range bounds, comprehension coefficients), while list names, aggregate
 * names, indices, and bound comprehension variables are removed. So
 * `mean([1...n])` → an expression whose only symbol is `n`, and `a*L + b` keeps
 * `a`/`b` while dropping the list `L`. Pure and unit-tested.
 */
export function listSurrogate(text: string, listNames: readonly string[]): string {
  let s = text;
  // Aggregates first (their arg is a list), then indices (name[idx] → idx).
  for (let g = 0; g < 64; g++) {
    const ag = findAggCall(s, AGG_NAMES, listNames);
    if (!ag) break;
    s = s.slice(0, ag.start) + `(${listSurrogate(ag.inner, listNames)})` + s.slice(ag.end);
  }
  for (let g = 0; g < 64; g++) {
    const ix = findIndex(s, listNames);
    if (!ix) break;
    s = s.slice(0, ix.start) + `(${listSurrogate(ix.idx, listNames)})` + s.slice(ix.end);
  }
  // List-returning calls sort/unique/reverse/join → the scalar parts of args.
  for (let g = 0; g < 64; g++) {
    const lf = findListFnCall(s, listNames);
    if (!lf) break;
    const parts = splitTopLevelCommas(lf.inner)
      .map((a) => `(${listSurrogate(a, listNames)})`)
      .join("+");
    s = s.slice(0, lf.start) + `(${parts || "0"})` + s.slice(lf.end);
  }
  // Brackets → the scalar parts of the list body (slices `[a...b]` too).
  s = mapBrackets(s, (inside) => bracketSurrogate(inside, listNames));
  // Bare list-name references contribute no scalar symbol.
  for (const n of listNames) s = substIdent(s, n, "0");
  return s;
}

function bracketSurrogate(inside: string, listNames: readonly string[]): string {
  const ls = parseListBody(inside);
  if (ls.kind === "range") {
    return [ls.from, ls.to, ...(ls.step ? [ls.step] : [])]
      .map((e) => `(${listSurrogate(e, listNames)})`)
      .join("+");
  }
  if (ls.kind === "comprehension") {
    const body = substIdent(ls.body, ls.varName, "0"); // bound var is not a slider
    return `(${listSurrogate(body, listNames)})+(${listSurrogate(ls.source, listNames)})`;
  }
  return ls.items.length ? ls.items.map((e) => `(${listSurrogate(e, listNames)})`).join("+") : "0";
}
