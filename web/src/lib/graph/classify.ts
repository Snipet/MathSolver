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
  | { t: "pointish"; coords: [string, string][]; restrict?: string[] } // points or parametric
  | { t: "define"; name: string; expr: string; restrict?: string[] } // name = expr
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

export function classifyRow(text: string): RowKind {
  const { body, restrict } = splitRestrictions(text);
  const kind = classifyBody(body);
  return restrict.length && kind.t !== "empty" ? { ...kind, restrict } : kind;
}

function classifyBody(text: string): RowKind {
  const t = text.trim();
  if (!t) return { t: "empty" };

  const pts = parsePointish(t);
  if (pts) return { t: "pointish", coords: pts };

  const rel = splitRelation(t);
  if (rel) {
    const { lhs, op, rhs } = rel;
    if (op === "=") {
      // A chained `y = x = 2` — the right side still holds a relation. Treat the
      // whole row as a relation rather than folding the second '=' into a body.
      if (splitRelation(rhs)) return { t: "relation", lhs: lhs.trim(), rhs: rhs.trim(), op };
      if (isVar(lhs, "y")) return { t: "function", expr: rhs.trim() };
      if (isVar(rhs, "y")) return { t: "function", expr: lhs.trim() };
      if (isVar(lhs, "x")) return { t: "functionY", expr: rhs.trim() };
      if (isVar(rhs, "x")) return { t: "functionY", expr: lhs.trim() };
      if (isVar(lhs, "r")) return { t: "polar", expr: rhs.trim() };
      if (isVar(rhs, "r")) return { t: "polar", expr: lhs.trim() };
      // A definition: `name = expr` or `name(args) = expr` where `name` is a
      // bare identifier other than the graph variables — becomes a reusable
      // named value/expression (a session variable).
      const dm = /^([A-Za-z][A-Za-z0-9_]*)\s*(?:\([^()]*\))?$/.exec(lhs.trim());
      if (dm && !["x", "y", "r"].includes(dm[1])) {
        return { t: "define", name: dm[1], expr: rhs.trim() };
      }
      return { t: "relation", lhs: lhs.trim(), rhs: rhs.trim(), op };
    }
    return { t: "relation", lhs: lhs.trim(), rhs: rhs.trim(), op };
  }

  // A bare expression is y = f(x).
  return { t: "function", expr: t };
}
