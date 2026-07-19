// Shared session-environment logic: `:=` assignment recognition/validation and
// application of the stored variable environment to an input before it reaches
// a computing verb (docs/proposals/variable-assignment.md §2, §4, §5, §8).
//
// Extracted from App.svelte so both the tabbed workbench and the line-by-line
// console drive identical resolution semantics. Free of Svelte-component state
// — everything here reads the shared `vars` store and the engine `call` client.
import { call } from "../engine";
import type { Rendered } from "../engine/types";
import { splitTopLevel } from "../format";
import { vars } from "../vars.svelte";
import {
  closure,
  serializeAssignments,
  findCycle,
  cycleMessage,
  equationRefMessage,
  type VarBinding,
} from "./resolve";
import {
  nameVerdict,
  valueVerdict,
  normalizeTypedName,
  EMPTY_VALUE_ERROR,
} from "./validate";

// --- `:=` assignment recognition (spec §2: the input layer, not the parser) --
export interface AssignParts {
  name: string;
  value: string;
}

export interface AssignPreview extends AssignParts {
  error: string | null;
  /** Caret span into `value` for parse errors. */
  span?: { begin?: number; end?: number };
  latex?: string;
  commit?: {
    symbol: string;
    nameLatex: string;
    kind: "expression" | "equation";
    valuePlain: string;
    valueLatex: string;
    symbols: string[];
  };
}

/**
 * The first `:=` with a non-empty left part makes the line an assignment.
 * (`:=` with an empty left side falls through to the parser and keeps its
 * existing `':'` lex error; an empty right side is the §2.3 value error.)
 */
export function splitAssignment(text: string): AssignParts | null {
  const i = text.indexOf(":=");
  if (i < 0) return null;
  const name = text.slice(0, i).trim();
  if (!name) return null;
  return { name, value: text.slice(i + 2).trim() };
}

export async function buildAssignPreview(
  parts: AssignParts,
): Promise<AssignPreview> {
  const name = normalizeTypedName(parts.name);
  const nv = nameVerdict(name, await call("analyze", [name]));
  if (!nv.ok) return { ...parts, error: nv.error };
  if (!parts.value) return { ...parts, error: EMPTY_VALUE_ERROR };
  const vv = valueVerdict(await call("analyze", [parts.value]));
  if (!vv.ok)
    return { ...parts, error: vv.error, span: { begin: vv.begin, end: vv.end } };
  // Definition-time cycle check (§5.2) against the environment as it would
  // become; redefinition replaces, so the old binding of this name is out.
  const others = vars.active.filter((b) => b.name !== nv.symbol);
  const path = findCycle(nv.symbol, vv.symbols, others);
  if (path) return { ...parts, error: cycleMessage(path) };
  return {
    ...parts,
    error: null,
    latex: `${nv.latex} \\mathrel{:=} ${vv.latex}`,
    commit: {
      symbol: nv.symbol,
      nameLatex: nv.latex,
      kind: vv.kind,
      valuePlain: vv.plain,
      valueLatex: vv.latex,
      symbols: vv.symbols,
    },
  };
}

/**
 * §4 equation-name placement: a whole `;`-segment that is exactly an
 * equation-valued name denotes the stored equation. Swapping is textual over
 * plain-printed values (round-trip-safe, §5) and happens before the engine
 * ever sees the text — `analyze`/`solveSystem` require an `=` in every segment.
 */
export function swapEqSegments(text: string): { text: string; swapped: boolean } {
  const act = vars.active;
  let swapped = false;
  const segments = splitTopLevel(text).map((seg) => {
    const b = act.find((x) => x.kind === "equation" && x.name === seg);
    if (!b) return seg;
    swapped = true;
    return b.value;
  });
  return swapped ? { text: segments.join("; "), swapped } : { text, swapped };
}

/** Resolution failure that should surface as an ordinary error card. */
export class EnvError extends Error {}

export interface Applied {
  text: string;
  computedFrom: Rendered | null;
}

/** Numeric value overrides (cell sliders): shadow expression bindings. */
export type EnvOverrides = Record<string, number>;

/** Plain-printed override value the engine can re-parse. */
export function overrideValue(v: number): string {
  return String(Number(v.toPrecision(8)));
}

/**
 * The environment with slider overrides applied: an overridden expression
 * binding becomes the numeric literal (and loses its outgoing dependencies —
 * overriding `b` in `b := a + 1` disconnects it from `a`). A name absent from
 * the active set is added — sliders write through to the store, whose
 * debounced re-validation briefly drops the row from `active`; the override
 * must stand in for it during that window.
 */
function withOverrides(act: VarBinding[], ov?: EnvOverrides): VarBinding[] {
  if (!ov || Object.keys(ov).length === 0) return act;
  const out = act.map((b) =>
    b.kind === "expression" && b.name in ov
      ? { ...b, value: overrideValue(ov[b.name]), symbols: [] }
      : b,
  );
  const have = new Set(act.map((b) => b.name));
  for (const [name, v] of Object.entries(ov)) {
    if (!have.has(name))
      out.push({
        name,
        value: overrideValue(v),
        symbols: [],
        kind: "expression",
      });
  }
  return out;
}

/**
 * Apply the environment to `text` (§5 resolve + §8 one `subs` call per
 * equation segment), returning the resolved input for the operation and its
 * rendering for the "computed from" line. Returns `text` unchanged when
 * nothing applies.
 */
export async function applyEnv(
  text: string,
  excluded: string[],
  mode: "expr" | "solve",
  overrides?: EnvOverrides,
): Promise<Applied> {
  const act = withOverrides(vars.active, overrides);
  if (act.length === 0) return { text, computedFrom: null };
  let segments = [text];
  let swapped = false;
  if (mode === "solve") {
    const sw = swapEqSegments(text);
    swapped = sw.swapped;
    segments = splitTopLevel(sw.text);
    if (segments.length === 0) segments = [sw.text];
  }
  const joined = segments.join("; ");
  // The verb reports parse errors on the (swapped) text it will receive.
  const a = await call("analyze", [joined]);
  if (!a.ok) return { text: joined, computedFrom: null };
  const env = closure(a.symbols, act, excluded);
  // §4 placement rule: an equation name is an error inside an expression
  // (always when referenced from a binding's value; on the Solve path also
  // when it is not a whole segment). Other operations leave it un-applied.
  if (env.nestedEquationRefs.length > 0)
    throw new EnvError(equationRefMessage(env.nestedEquationRefs[0]));
  if (mode === "solve" && env.directEquationRefs.length > 0)
    throw new EnvError(equationRefMessage(env.directEquationRefs[0]));
  if (env.active.length === 0 && !swapped) return { text, computedFrom: null };

  const outs: Rendered[] = [];
  if (env.active.length === 0) {
    for (const seg of segments) {
      const sa = await call("analyze", [seg]);
      if (!sa.ok || sa.kind === "system")
        return { text: segments.join("; "), computedFrom: null };
      outs.push({ plain: sa.plain, latex: sa.latex });
    }
  } else {
    const csv = serializeAssignments(env.active);
    for (const seg of segments) {
      // simplifyResult=false (§8): "computed from" must show the resolved
      // input un-simplified (x + x + 7, not 2x + 7); the operation itself
      // simplifies downstream as usual.
      const r = await call("subs", [seg, csv, false]);
      if (!r.ok) throw new EnvError(r.error);
      outs.push({ plain: r.plain, latex: r.latex });
    }
  }
  return {
    text: outs.map((o) => o.plain).join("; "),
    computedFrom: {
      plain: outs.map((o) => o.plain).join("; "),
      latex: outs.map((o) => o.latex).join(" ;\\; "),
    },
  };
}
