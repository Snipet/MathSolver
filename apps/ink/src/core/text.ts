// Text utilities mirroring the top-level splitting the REPL and WASM bindings
// use (apps/main.cpp, wasm/bindings.cpp): commas/semicolons/relational
// operators are only significant outside (), [], {}.

export function trim(s: string): string {
  return s.replace(/^\s+|\s+$/g, "");
}

/** Split at `delim` characters that are not nested inside (), {}, or []. Each
 *  part is trimmed. Always returns at least one element. */
export function splitTopLevel(s: string, delim: string): string[] {
  const parts: string[] = [];
  let depth = 0;
  let current = "";
  for (const c of s) {
    if (c === "(" || c === "{" || c === "[") depth++;
    else if (c === ")" || c === "}" || c === "]") depth--;
    if (c === delim && depth <= 0) {
      parts.push(trim(current));
      current = "";
    } else {
      current += c;
    }
  }
  parts.push(trim(current));
  return parts;
}

export const splitCommas = (s: string): string[] => splitTopLevel(s, ",");

export function hasTopLevelSemicolon(s: string): boolean {
  return splitTopLevel(s, ";").length > 1;
}

/** True when `s` contains a bare `=` at bracket depth 0 that is not part of a
 *  relational operator (`<=`, `>=`, `!=`) or the assignment token (`:=`). */
export function hasTopLevelEquals(s: string): boolean {
  let depth = 0;
  for (let i = 0; i < s.length; i++) {
    const c = s[i]!;
    if (c === "(" || c === "{" || c === "[") depth++;
    else if (c === ")" || c === "}" || c === "]") depth--;
    else if (c === "=" && depth === 0) {
      const prev = s[i - 1];
      if (prev !== "<" && prev !== ">" && prev !== "!" && prev !== ":" && prev !== "=") {
        return true;
      }
    }
  }
  return false;
}

export type IneqOp = "<" | "<=" | ">" | ">=";

export interface ParsedInequality {
  op: IneqOp;
  lhs: string;
  rhs: string;
}

/** Find a top-level relational operator (`<`, `>`, `<=`, `>=`, or Unicode
 *  `≤`/`≥`) outside any brackets, splitting the string around it. Mirrors
 *  find_inequality() in apps/main.cpp. Returns null when none is present. */
export function findInequality(s: string): ParsedInequality | null {
  let depth = 0;
  for (let i = 0; i < s.length; i++) {
    const c = s[i]!;
    if (c === "(" || c === "[" || c === "{") depth++;
    else if (c === ")" || c === "]" || c === "}") depth--;
    else if (depth === 0) {
      if (c === "≤") return { op: "<=", lhs: s.slice(0, i), rhs: s.slice(i + 1) };
      if (c === "≥") return { op: ">=", lhs: s.slice(0, i), rhs: s.slice(i + 1) };
      if (c === "<" || c === ">") {
        const eq = s[i + 1] === "=";
        const op: IneqOp = c === "<" ? (eq ? "<=" : "<") : eq ? ">=" : ">";
        return { op, lhs: s.slice(0, i), rhs: s.slice(eq ? i + 2 : i + 1) };
      }
    }
  }
  return null;
}

/** A bindable/solvable variable name: a leading letter then letters, digits,
 *  or underscores. Matches is_symbol_name() in apps/main.cpp. */
export function isSymbolName(s: string): boolean {
  return /^[A-Za-z][A-Za-z0-9_]*$/.test(s);
}

/** Parse an integer argument (used for series order, stirling terms). Returns
 *  null when the text is not a finite integer. */
export function parseIntArg(text: string): number | null {
  const t = trim(text);
  if (t === "") return null;
  const v = Number(t);
  return Number.isFinite(v) && Number.isInteger(v) ? v : null;
}
