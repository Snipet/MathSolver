// Syntactic classification of a graph expression row into what it plots. The
// engine-dependent refinements (free-variable detection, points vs. a
// parametric curve) happen in GraphCalculator; this pass is pure text and is
// unit-tested (tools/graph_classify_test.mjs).

export type RelOp = "=" | "<" | ">" | "<=" | ">=";

/** A Desmos-style domain restriction, one comparison of `{ … }`, e.g. x > 0. */
export interface Comparison {
  lhs: string;
  op: RelOp;
  rhs: string;
}

// Every plottable kind may carry `restrict` — trailing `{ … }` domain clauses
// stripped off the row (e.g. `y = x^2 {x > 0}` → restrict: ["x > 0"]).
export type RowKind =
  | { t: "empty" }
  | { t: "function"; expr: string; restrict?: string[] } // y = f(x)  (bare expr, or "y = …")
  | { t: "functionY"; expr: string; restrict?: string[] } // x = f(y)
  | { t: "polar"; expr: string; restrict?: string[] } // r = f(θ)
  | { t: "slopefield"; expr: string; restrict?: string[] } // y' = f(x, y) / dy/dx = …
  | { t: "pointish"; coords: [string, string][]; restrict?: string[] } // points or parametric
  | { t: "define"; name: string; expr: string; params?: string[]; restrict?: string[] } // name = expr, or name(params) = expr
  | { t: "listdef"; name: string; expr: string; restrict?: string[] } // name = <list> ([…], sort(L), L[a...b], …)
  | { t: "piecewise"; branches: { cond: string; value: string }[]; otherwise?: string; restrict?: string[] } // {cond: val, …[, else]}
  | { t: "area"; expr: string; lo: string; hi: string; restrict?: string[] } // ∫ f dx shaded over [a, b]
  | { t: "relation"; lhs: string; rhs: string; op: RelOp; restrict?: string[] }; // implicit / ineq

/**
 * Peel trailing `{ … }` restriction clauses off a row, returning the plottable
 * body and each clause's inner text (in source order). `y = x^2 {x>0}{x<5}` →
 * { body: "y = x^2", restrict: ["x>0", "x<5"] }.
 */
export function splitRestrictions(text: string): { body: string; restrict: string[] } {
  let t = text.trimEnd();
  const restrict: string[] = [];
  while (t.endsWith("}")) {
    let depth = 0;
    let start = -1;
    for (let i = t.length - 1; i >= 0; i--) {
      if (t[i] === "}") depth++;
      else if (t[i] === "{") {
        depth--;
        if (depth === 0) {
          start = i;
          break;
        }
      }
    }
    if (start < 0) break; // unbalanced — leave as-is
    restrict.unshift(t.slice(start + 1, t.length - 1).trim());
    t = t.slice(0, start).trimEnd();
  }
  return { body: t, restrict };
}

/**
 * How a point coordinate can be dragged: a numeric `literal` (rewrite it), a
 * bare `ident` (a variable — the component decides if it's settable), or
 * `locked` (a fraction like 1/2, an expression, or x/y/r — not draggable).
 */
export function coordAtom(expr: string): "literal" | "ident" | "locked" {
  const t = expr.trim();
  if (/^[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?$/.test(t)) return "literal";
  if (/^[A-Za-z][A-Za-z0-9_]*$/.test(t) && !["x", "y", "r"].includes(t)) return "ident";
  return "locked";
}

/**
 * Rewrite one point's coordinates in a pointish row, preserving the other
 * points and any `{ … }` restrictions. `x`/`y` are the new coordinate strings
 * (null = keep). Returns the row text unchanged if it isn't pointish or the
 * index is out of range. Normalizes interior spacing (e.g. `(1,2)`→`(1, 2)`).
 */
export function rebuildPointRow(
  text: string,
  coordIndex: number,
  x: string | null,
  y: string | null,
): string {
  const spec = classifyRow(text);
  if (spec.t !== "pointish") return text;
  const coords = spec.coords.map((p) => [...p] as [string, string]);
  if (coordIndex < 0 || coordIndex >= coords.length) return text;
  if (x !== null) coords[coordIndex][0] = x;
  if (y !== null) coords[coordIndex][1] = y;
  let body = coords.map(([a, b]) => `(${a}, ${b})`).join(", ");
  for (const r of spec.restrict ?? []) body += ` {${r}}`;
  return body;
}

/** Flatten restriction clauses into comparisons, splitting chains a ≤ v ≤ b. */
export function parseRestriction(clauses: string[]): Comparison[] {
  return clauses.flatMap((c) => parseChain(c));
}

function parseChain(s: string): Comparison[] {
  const first = splitRelation(s);
  if (!first) return [];
  const rest = splitRelation(first.rhs);
  if (rest) {
    // a ≤ v ≤ b → [a ≤ v, v ≤ b] (and continue any further chaining).
    return [
      { lhs: first.lhs.trim(), op: first.op, rhs: rest.lhs.trim() },
      ...parseChain(first.rhs),
    ];
  }
  return [{ lhs: first.lhs.trim(), op: first.op, rhs: first.rhs.trim() }];
}

/** Split top-level on commas, respecting (), [], {} nesting. */
export function splitTopLevelCommas(s: string): string[] {
  const parts: string[] = [];
  let depth = 0;
  let cur = "";
  for (const ch of s) {
    if (ch === "(" || ch === "[" || ch === "{") depth++;
    else if (ch === ")" || ch === "]" || ch === "}") depth = Math.max(0, depth - 1);
    if (ch === "," && depth === 0) {
      parts.push(cur);
      cur = "";
    } else cur += ch;
  }
  parts.push(cur);
  return parts;
}

/** First top-level relational operator, or null. Longest ops first. */
export function splitRelation(text: string): { lhs: string; op: RelOp; rhs: string } | null {
  let depth = 0;
  for (let i = 0; i < text.length; i++) {
    const ch = text[i];
    if (ch === "(" || ch === "[" || ch === "{") depth++;
    else if (ch === ")" || ch === "]" || ch === "}") depth = Math.max(0, depth - 1);
    else if (depth === 0) {
      const two = text.slice(i, i + 2);
      if (two === "<=" || two === ">=") {
        return { lhs: text.slice(0, i), op: two, rhs: text.slice(i + 2) };
      }
      if ((ch === "<" || ch === ">") && text[i + 1] !== "=") {
        return { lhs: text.slice(0, i), op: ch, rhs: text.slice(i + 1) };
      }
      if (ch === "=" && text[i - 1] !== "<" && text[i - 1] !== ">" && text[i + 1] !== "=") {
        return { lhs: text.slice(0, i), op: "=", rhs: text.slice(i + 1) };
      }
    }
  }
  return null;
}

/** Whole string is one or more balanced parenthesized groups, comma-joined. */
function parsePointish(text: string): [string, string][] | null {
  const t = text.trim();
  if (!t.startsWith("(")) return null;
  const groups: string[] = [];
  let depth = 0;
  let start = -1;
  for (let i = 0; i < t.length; i++) {
    const ch = t[i];
    if (ch === "(") {
      if (depth === 0) start = i;
      depth++;
    } else if (ch === ")") {
      depth--;
      if (depth === 0) groups.push(t.slice(start + 1, i));
      if (depth < 0) return null;
    } else if (depth === 0 && ch !== "," && !/\s/.test(ch)) {
      return null; // stray content between groups → not a point list
    }
  }
  if (depth !== 0 || groups.length === 0) return null;
  const coords: [string, string][] = [];
  for (const g of groups) {
    const parts = splitTopLevelCommas(g);
    if (parts.length !== 2) return null;
    coords.push([parts[0].trim(), parts[1].trim()]);
  }
  return coords;
}

/** Is `name` the entire (trimmed) side? */
function isVar(side: string, name: string): boolean {
  return side.trim() === name;
}

/**
 * Match a whole-row definite-integral area form — `integral(f, a, b)` or
 * `antiderivative(f, a, b)` — where the call spans the entire (trimmed) row and
 * splits into exactly three top-level arguments: the integrand and the two
 * bounds. Returns them, or null. The one- and two-argument forms
 * (`integral(f)` / `integral(f, t)`) stay ordinary function rows whose
 * antiderivative GraphCalculator plots as a curve — only the three-argument
 * form shades the signed area under `y = f(x)` over `[a, b]`.
 */
function matchArea(text: string): { expr: string; lo: string; hi: string } | null {
  const m = /^(?:integral|antiderivative)\s*\(/.exec(text);
  if (!m) return null;
  const open = m[0].length - 1; // index of the '('
  let depth = 0;
  let close = -1;
  for (let i = open; i < text.length; i++) {
    if (text[i] === "(") depth++;
    else if (text[i] === ")") {
      depth--;
      if (depth === 0) {
        close = i;
        break;
      }
    }
  }
  if (close !== text.length - 1) return null; // the call must be the whole row
  const parts = splitTopLevelCommas(text.slice(open + 1, close)).map((p) => p.trim());
  if (parts.length !== 3 || parts.some((p) => p === "")) return null;
  return { expr: parts[0], lo: parts[1], hi: parts[2] };
}

/** Index of the first top-level `:` (outside any bracket), or -1. */
function topLevelColon(s: string): number {
  let depth = 0;
  for (let i = 0; i < s.length; i++) {
    const ch = s[i];
    if (ch === "(" || ch === "[" || ch === "{") depth++;
    else if (ch === ")" || ch === "]" || ch === "}") depth = Math.max(0, depth - 1);
    else if (ch === ":" && depth === 0) return i;
  }
  return -1;
}

/**
 * Match a Desmos-style piecewise: a whole row that is one `{ … }` group whose
 * segments carry `condition: value` (with an optional bare final `value` as the
 * else). A leading `y =` is allowed. A brace group with no `:` is a domain
 * restriction, not a piecewise, so `{x > 0}` is left to splitRestrictions.
 */
function matchPiecewise(text: string): RowKind | null {
  let t = text.trim();
  const ym = /^y\s*=\s*([\s\S]*)$/.exec(t);
  if (ym) t = ym[1].trim();
  if (!t.startsWith("{") || !t.endsWith("}")) return null;
  // The braces must be a single group spanning the whole (post-`y =`) text.
  let depth = 0;
  for (let i = 0; i < t.length; i++) {
    if (t[i] === "{") depth++;
    else if (t[i] === "}") {
      depth--;
      if (depth === 0 && i !== t.length - 1) return null;
    }
  }
  if (depth !== 0) return null;
  const branches: { cond: string; value: string }[] = [];
  let otherwise: string | undefined;
  for (const seg of splitTopLevelCommas(t.slice(1, -1))) {
    const colon = topLevelColon(seg);
    if (colon >= 0) {
      branches.push({ cond: seg.slice(0, colon).trim(), value: seg.slice(colon + 1).trim() });
    } else if (seg.trim()) {
      otherwise = seg.trim(); // a bare value is the else branch
    }
  }
  if (!branches.length) return null; // no `cond: value` → a restriction, not this
  return { t: "piecewise", branches, otherwise };
}

// Is an assignment RHS list-valued by syntax? Kept local so the classifier
// stays dependency-free; lists.ts has the deeper evaluation.
function looksListValued(rhs: string): boolean {
  const t = rhs.trim();
  if (t.startsWith("[")) return true;
  if (/^(sort|unique|reverse|join)\s*\(/.test(t)) return true;
  // A whole-expression slice `name[ … a...b … ]`.
  const m = /^[A-Za-z][A-Za-z0-9_]*\s*\[(.*)\]$/.exec(t);
  return m !== null && m[1].includes("...");
}

export function classifyRow(text: string): RowKind {
  // A piecewise `{cond: val, …}` must be recognized before splitRestrictions,
  // which would otherwise peel the whole brace group off as a restriction.
  const pw = matchPiecewise(text);
  if (pw) return pw;
  const { body, restrict } = splitRestrictions(text);
  const kind = classifyBody(body);
  return restrict.length && kind.t !== "empty" ? { ...kind, restrict } : kind;
}

function classifyBody(text: string): RowKind {
  const t = text.trim();
  if (!t) return { t: "empty" };

  // A definite-integral area row: `integral(f, a, b)` shades ∫ₐᵇ f dx.
  const area = matchArea(t);
  if (area) return { t: "area", ...area };

  const pts = parsePointish(t);
  if (pts) return { t: "pointish", coords: pts };

  const rel = splitRelation(t);
  if (rel) {
    const { lhs, op, rhs } = rel;
    if (op === "=") {
      // A chained `y = x = 2` — the right side still holds a relation. Treat the
      // whole row as a relation rather than folding the second '=' into a body.
      if (splitRelation(rhs)) return { t: "relation", lhs: lhs.trim(), rhs: rhs.trim(), op };
      // A first-order ODE: y' = f(x, y) or dy/dx = f(x, y) → slope field.
      const lhsNo = lhs.replace(/\s+/g, "");
      if (lhsNo === "y'" || lhsNo === "dy/dx") return { t: "slopefield", expr: rhs.trim() };
      if (isVar(lhs, "y")) return { t: "function", expr: rhs.trim() };
      if (isVar(rhs, "y")) return { t: "function", expr: lhs.trim() };
      if (isVar(lhs, "x")) return { t: "functionY", expr: rhs.trim() };
      if (isVar(rhs, "x")) return { t: "functionY", expr: lhs.trim() };
      if (isVar(lhs, "r")) return { t: "polar", expr: rhs.trim() };
      if (isVar(rhs, "r")) return { t: "polar", expr: lhs.trim() };
      // A definition: `name = expr` (a reusable session value) or
      // `name(params) = expr` (a user function). The parameter list is captured
      // so `f(x) = x^2` can later be applied as `f(3)`, `f'(x)`, `g(f(x))`.
      const dm = /^([A-Za-z][A-Za-z0-9_]*)\s*(?:\(\s*([^()]*)\s*\))?$/.exec(lhs.trim());
      if (dm && !["x", "y", "r"].includes(dm[1])) {
        if (dm[2] !== undefined) {
          const params = splitTopLevelCommas(dm[2]).map((p) => p.trim()).filter(Boolean);
          // Empty parens `f() = …` is not a function (a function needs ≥1
          // parameter); fall through to a relation so it reads as an error/plot.
          if (params.length) return { t: "define", name: dm[1], expr: rhs.trim(), params };
        } else {
          // `name = <list>` is a list definition, not a scalar value: a `[ … ]`
          // literal/range/comprehension (any RHS opening a bracket, even
          // mid-type, so a partial `[1,` never commits as a broken scalar
          // variable), a list-returning call (`sort`/`unique`/`reverse`/`join`),
          // or a slice `L[a...b]`. The whole RHS is kept and materialized later.
          const rt = rhs.trim();
          if (looksListValued(rt)) return { t: "listdef", name: dm[1], expr: rt };
          return { t: "define", name: dm[1], expr: rhs.trim() };
        }
      }
      return { t: "relation", lhs: lhs.trim(), rhs: rhs.trim(), op };
    }
    return { t: "relation", lhs: lhs.trim(), rhs: rhs.trim(), op };
  }

  // A bare expression is y = f(x).
  return { t: "function", expr: t };
}
