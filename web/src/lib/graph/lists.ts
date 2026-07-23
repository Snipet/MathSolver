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
  | { kind: "range"; from: string; step: string | null; to: string };

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

/** Parse the inside of `[ … ]` into a literal element list or a numeric range. */
export function parseListBody(inside: string): ListSpec {
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
