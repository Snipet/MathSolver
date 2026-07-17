// Pure §2.3 validation verdicts, computed from `analyze` envelopes so the
// grammar stays the single source of truth (no client-side re-lexing).
import type { AnalyzeResult } from "../engine/types";
import { symbolToTyped } from "./resolve";

export const EMPTY_VALUE_ERROR = "assignment needs a value (e.g. x := 2)";
export const TARGET_RULE =
  "assignment target must be a single variable name (e.g. x, alpha, E_1)";
export const WORD_GUARD_HINT =
  "assignment targets follow the same rule — try a subscripted name like s_max := 5";

export type NameVerdict =
  | { ok: true; symbol: string; latex: string }
  | { ok: false; error: string };

/**
 * Name-field sugar: a bare multi-character subscript (`v_max`) is braced to
 * the grammar's single-symbol spelling (`v_{max}`) before being analyzed.
 * This applies only where a whole field/target is one name — expression
 * contexts keep the frozen §4 grammar untouched.
 */
export function normalizeTypedName(typed: string): string {
  const t = typed.replace(/\s+/g, "");
  const m = /^([A-Za-z]+)_([A-Za-z0-9]{2,})$/.exec(t);
  return m ? `${m[1]}_{${m[2]}}` : t;
}

/**
 * Validate an assignment target: it must lex as exactly one Symbol under the
 * §4 grammar. `analysis` is `analyze(typed)` for the typed name.
 */
export function nameVerdict(typed: string, analysis: AnalyzeResult): NameVerdict {
  const glued = typed.replace(/\s+/g, "");
  if (!analysis.ok) {
    const fn = /^function '([^']+)' has no argument$/.exec(analysis.error);
    if (fn) return { ok: false, error: `cannot assign to the function name '${fn[1]}'` };
    if (analysis.error.startsWith("unknown name"))
      return { ok: false, error: `${analysis.error}; ${WORD_GUARD_HINT}` };
    return { ok: false, error: TARGET_RULE };
  }
  if (analysis.kind !== "expression") return { ok: false, error: TARGET_RULE };
  if (analysis.symbols.length === 0) {
    // A symbol-free name that parses is a constant (pi, e) or a number.
    if (/^[a-zA-Zπ]+$/.test(glued))
      return { ok: false, error: `cannot assign to the constant '${glued}'` };
    return { ok: false, error: TARGET_RULE };
  }
  const sym = analysis.symbols[0];
  if (
    analysis.symbols.length === 1 &&
    (glued === sym || glued === symbolToTyped(sym))
  ) {
    return { ok: true, symbol: sym, latex: analysis.latex };
  }
  // The E1 trap (§2.3): letters+digits silently reads as a product.
  const prod = /^([a-zA-Z])(\d+)$/.exec(glued);
  if (prod) {
    return {
      ok: false,
      error: `${TARGET_RULE} — '${glued}' reads as ${prod[1]}*${prod[2]}; did you mean ${prod[1]}_${prod[2]}?`,
    };
  }
  return { ok: false, error: TARGET_RULE };
}

export type ValueVerdict =
  | {
      ok: true;
      kind: "expression" | "equation";
      plain: string;
      latex: string;
      symbols: string[];
    }
  | { ok: false; error: string; begin?: number; end?: number };

/** Validate a binding value: one expression or one equation (§4). */
export function valueVerdict(analysis: AnalyzeResult): ValueVerdict {
  if (!analysis.ok)
    return { ok: false, error: analysis.error, begin: analysis.begin, end: analysis.end };
  if (analysis.kind === "system")
    return { ok: false, error: "a variable value must be a single expression or equation" };
  return {
    ok: true,
    kind: analysis.kind,
    plain: analysis.plain,
    latex: analysis.latex,
    symbols: analysis.symbols,
  };
}
