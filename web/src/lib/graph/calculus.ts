// Calculus operators inside graph expressions: diff(...) / integral(...) /
// series(...). These aren't engine functions — GraphCalculator expands them by
// calling the engine's derivative/integrate/series, so "diff(f)",
// "integral(sin(x))", or "series(sin(x), 0, 5)" plot. This module holds the
// pure text scanning (unit-tested); the async expansion lives in the component.
import { splitTopLevelCommas } from "./classify";

export const CALC_FNS = [
  "diff",
  "derivative",
  "integral",
  "antiderivative",
  "series",
  "taylor",
];

function nameAt(text: string, i: number, name: string): boolean {
  if (text.slice(i, i + name.length) !== name) return false;
  const prev = text[i - 1];
  if (prev && /[A-Za-z0-9_]/.test(prev)) return false; // word boundary
  let j = i + name.length;
  while (j < text.length && /\s/.test(text[j])) j++;
  return text[j] === "(";
}

export interface CalcCall {
  name: string;
  /** Raw argument list inside the parentheses. */
  inner: string;
  /** Index of the function name / char after the closing paren. */
  start: number;
  end: number;
}

/**
 * The innermost diff/integral call (one whose argument contains no further
 * calc call), scanning left to right. Null when there are none.
 */
export function findInnermostCall(text: string): CalcCall | null {
  for (let i = 0; i < text.length; i++) {
    for (const name of CALC_FNS) {
      if (!nameAt(text, i, name)) continue;
      let j = i + name.length;
      while (j < text.length && /\s/.test(text[j])) j++;
      // j is at '('. Find the matching close.
      let depth = 0;
      let k = j;
      for (; k < text.length; k++) {
        if (text[k] === "(") depth++;
        else if (text[k] === ")") {
          depth--;
          if (depth === 0) break;
        }
      }
      if (depth !== 0) continue; // unbalanced
      const inner = text.slice(j + 1, k);
      if (!containsCalcCall(inner)) {
        return { name, inner, start: i, end: k + 1 };
      }
    }
  }
  return null;
}

function containsCalcCall(text: string): boolean {
  for (let i = 0; i < text.length; i++) {
    for (const name of CALC_FNS) if (nameAt(text, i, name)) return true;
  }
  return false;
}

/**
 * Replace every diff/integral call with its (first argument), for free-symbol
 * analysis — so a row like "diff(f) + g" analyzes as "(f) + g" without the
 * engine choking on the unknown "diff" function.
 */
export function stripCalc(text: string): string {
  let s = text;
  for (let guard = 0; guard < 24; guard++) {
    const c = findInnermostCall(s);
    if (!c) break;
    const firstArg = splitTopLevelCommas(c.inner)[0] ?? "";
    s = s.slice(0, c.start) + "(" + firstArg + ")" + s.slice(c.end);
  }
  return s;
}
