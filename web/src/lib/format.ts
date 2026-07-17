// Small formatting and parsing helpers shared across components.

/** Compact human-friendly number: up to 6 significant digits, no noise. */
export function fmt(n: number): string {
  if (!Number.isFinite(n)) return String(n);
  if (Number.isInteger(n) && Math.abs(n) < 1e15) return String(n);
  return String(parseFloat(n.toPrecision(6)));
}

/** Coerce a possibly-null/NaN numeric input value to a number, else default. */
export function numOr(v: unknown, fallback: number): number {
  return typeof v === "number" && Number.isFinite(v) ? v : fallback;
}

/** True if the string contains a `;` outside any bracket nesting. */
export function hasTopLevelSemicolon(s: string): boolean {
  let depth = 0;
  for (const ch of s) {
    if (ch === "(" || ch === "[" || ch === "{") depth++;
    else if (ch === ")" || ch === "]" || ch === "}") depth = Math.max(0, depth - 1);
    else if (ch === ";" && depth === 0) return true;
  }
  return false;
}

/** Non-empty top-level `;`-separated parts (equations in a system), trimmed. */
export function splitTopLevel(s: string): string[] {
  const parts: string[] = [];
  let depth = 0;
  let cur = "";
  for (const ch of s) {
    if (ch === "(" || ch === "[" || ch === "{") depth++;
    else if (ch === ")" || ch === "]" || ch === "}") depth = Math.max(0, depth - 1);
    if (ch === ";" && depth === 0) {
      if (cur.trim()) parts.push(cur.trim());
      cur = "";
    } else {
      cur += ch;
    }
  }
  if (cur.trim()) parts.push(cur.trim());
  return parts;
}

/** Number of non-empty top-level `;`-separated parts (equations in a system). */
export function countTopLevelParts(s: string): number {
  let depth = 0;
  let count = 0;
  let cur = "";
  for (const ch of s) {
    if (ch === "(" || ch === "[" || ch === "{") depth++;
    else if (ch === ")" || ch === "]" || ch === "}") depth = Math.max(0, depth - 1);
    if (ch === ";" && depth === 0) {
      if (cur.trim()) count++;
      cur = "";
    } else {
      cur += ch;
    }
  }
  if (cur.trim()) count++;
  return count;
}

/** Symbol name as a KaTeX fragment (multi-char names set upright). */
export function varLatex(name: string): string {
  return name.length > 1 ? `\\mathrm{${name}}` : name;
}
