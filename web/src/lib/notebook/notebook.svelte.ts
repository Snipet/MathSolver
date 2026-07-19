// Console session: an ordered list of evaluated cells, persisted to
// localStorage so the notebook survives a reload (like a Mathematica notebook
// document). Cell results are the plain-data CellResult union, so they
// serialize directly and restore without re-evaluation (re-running would
// double-commit `:=` assignments, which the shared `vars` store already
// persists on its own).
import { runLine, type CellResult } from "./run";
import { buildConsolePreview } from "./preview";
import { splitTopLevelCommas } from "../format";
import { vars } from "../vars.svelte";
import {
  overrideValue,
  splitAssignment,
  type EnvOverrides,
} from "../vars/session";

/**
 * One manipulable variable of a cell: a session binding with a numeric value
 * that the cell's computation resolves. The slider IS the variable — dragging
 * writes through to the session binding (the Variables panel follows) and
 * re-runs the cell; editing the binding in the panel moves the slider.
 */
export interface CellSlider {
  name: string;
  value: number;
  lo: number;
  hi: number;
}

export interface Cell {
  id: number;
  /** The raw line as entered (echoed as the In[] prompt). */
  input: string;
  /** Typeset (LaTeX) rendering of the parsed input; null when the line has
   *  no math form (help, vars, plugin calls, parse errors). */
  inputLatex: string | null;
  /** null while the engine is working; set once evaluated. */
  result: CellResult | null;
  /** Slider-manipulable variables this cell references (may be empty). */
  sliders: CellSlider[];
  /** Tall-output fold state: explicit user choice, or null = automatic
   *  (fresh runs render expanded; restored cells render collapsed). */
  collapsed: boolean | null;
  /** True when this cell came from localStorage, not a live run. */
  restored: boolean;
  ts: number;
}

const KEY = "mathsolver.notebook";
/** Keep the last N cells; older ones scroll out of persistence. */
const CAP = 80;

let nextId = 1;

function load(): Cell[] {
  try {
    const raw = localStorage.getItem(KEY);
    if (!raw) return [];
    const store: unknown = JSON.parse(raw);
    if (!store || typeof store !== "object") return [];
    const arr = (store as Record<string, unknown>).cells;
    if (!Array.isArray(arr)) return [];
    const cells: Cell[] = [];
    for (const c of arr) {
      if (!c || typeof c !== "object") continue;
      const o = c as Record<string, unknown>;
      if (typeof o.input !== "string" || !o.result || typeof o.result !== "object")
        continue;
      cells.push({
        id: nextId++,
        input: o.input,
        inputLatex: typeof o.inputLatex === "string" ? o.inputLatex : null,
        result: o.result as CellResult,
        sliders: loadSliders(o.sliders),
        collapsed: typeof o.collapsed === "boolean" ? o.collapsed : null,
        restored: true,
        ts: typeof o.ts === "number" ? o.ts : Date.now(),
      });
    }
    return cells.slice(-CAP);
  } catch {
    return [];
  }
}

function loadSliders(raw: unknown): CellSlider[] {
  if (!Array.isArray(raw)) return [];
  const out: CellSlider[] = [];
  for (const s of raw) {
    if (!s || typeof s !== "object") continue;
    const o = s as Record<string, unknown>;
    if (
      typeof o.name === "string" &&
      typeof o.value === "number" &&
      typeof o.lo === "number" &&
      typeof o.hi === "number"
    )
      out.push({ name: o.name, value: o.value, lo: o.lo, hi: o.hi });
  }
  return out;
}

// Verbs that never resolve the session environment: no slider can affect them.
const NO_ENV_VERBS = new Set([
  "dsolve",
  "latex",
  "rsolve",
  "seq",
  "stirling",
  "help",
  "vars",
  "clear",
  "unset",
  "plugins",
  "quit",
  "exit",
]);

function defaultRange(v: number): { lo: number; hi: number } {
  if (v === 0) return { lo: -1, hi: 1 };
  return { lo: Math.min(0, 2 * v), hi: Math.max(0, 2 * v) };
}

/**
 * Which session variables this cell can manipulate: numeric expression
 * bindings whose name appears in the input, minus variables-of-operation
 * (bare-identifier arguments after the first — `x` in `solve …, x`, `k` in
 * `sum …, k, 1, n` — which resolution excludes by design). `integrate`
 * resolves its bound arguments, so only its variable slot is excluded.
 */
function sliderCandidates(input: string, result: CellResult | null): CellSlider[] {
  if (
    !result ||
    result.kind === "message" ||
    result.kind === "assignment" ||
    result.kind === "error"
  )
    return [];
  if (splitAssignment(input)) return [];
  const m = /^\s*(\S+)\s*([\s\S]*)$/.exec(input);
  const head = (m?.[1] ?? "").toLowerCase();
  if (NO_ENV_VERBS.has(head)) return [];
  const trailing = splitTopLevelCommas(m?.[2] ?? "")
    .slice(1)
    .map((t) => t.trim())
    .filter((t) => /^[A-Za-z_][A-Za-z0-9_]*$/.test(t));
  // Plugin commands (dsp.butter …) resolve every argument — exclude nothing.
  const opVars = new Set(
    head.includes(".")
      ? []
      : head === "integrate"
        ? trailing.slice(0, 1)
        : trailing,
  );
  const tokens = new Set(input.match(/[A-Za-z_][A-Za-z0-9_]*/g) ?? []);
  const out: CellSlider[] = [];
  for (const b of vars.active) {
    if (b.kind !== "expression") continue;
    const v = Number(b.value);
    if (!Number.isFinite(v)) continue;
    if (!tokens.has(b.name) || opVars.has(b.name)) continue;
    out.push({ name: b.name, value: v, ...defaultRange(v) });
  }
  return out;
}

class NotebookStore {
  cells = $state<Cell[]>(load());

  get inputs(): string[] {
    return this.cells.map((c) => c.input);
  }

  /** Evaluate a line, appending a cell and filling its result in place. */
  async run(raw: string): Promise<void> {
    const input = raw.trim();
    if (!input) return;
    const cell: Cell = {
      id: nextId++,
      input,
      inputLatex: null,
      result: null,
      sliders: [],
      collapsed: null,
      restored: false,
      ts: Date.now(),
    };
    this.cells.push(cell);
    if (this.cells.length > CAP) this.cells = this.cells.slice(-CAP);

    // Typeset the input as parsed (∫ … dx, d/dx …, lim …) alongside the
    // evaluation; lines with no math form (help, plugins) stay text-only.
    const latexPromise = buildConsolePreview(input)
      .then((p) => (p.kind === "math" ? p.latex : null))
      .catch(() => null);
    const [inputLatex, result] = await Promise.all([latexPromise, runLine(input)]);
    const c = this.cells.find((x) => x.id === cell.id);
    if (!c) return; // cleared mid-flight
    c.inputLatex = inputLatex;
    c.result = result;
    c.sliders = sliderCandidates(input, result);
    this.#persist();
  }

  // --- cell sliders (Manipulate-style re-runs) -----------------------------

  /** In-flight recompute per cell: latest slider position always wins. */
  #pending = new Map<number, { running: boolean; dirty: boolean }>();

  /** Re-evaluate a cell in place with its current slider overrides. */
  recompute(id: number): void {
    const st = this.#pending.get(id) ?? { running: false, dirty: false };
    if (st.running) {
      st.dirty = true;
      this.#pending.set(id, st);
      return;
    }
    st.running = true;
    this.#pending.set(id, st);
    void (async () => {
      do {
        st.dirty = false;
        const c = this.cells.find((x) => x.id === id);
        if (!c) break;
        // Slider values pass as overrides even though they also write through
        // to the session binding: the store's debounced re-validation may lag
        // a drag, and the override keeps this run correct regardless.
        const ov: EnvOverrides = {};
        for (const s of c.sliders) ov[s.name] = s.value;
        const result = await runLine(c.input, ov);
        const cc = this.cells.find((x) => x.id === id);
        if (!cc) break;
        cc.result = result;
      } while (st.dirty);
      st.running = false;
      this.#persist();
    })();
  }

  setSlider(id: number, name: string, value: number): void {
    const c = this.cells.find((x) => x.id === id);
    const s = c?.sliders.find((x) => x.name === name);
    if (!c || !s || !Number.isFinite(value)) return;
    s.value = value;
    // A typed value outside the range grows the range to keep it draggable.
    if (value < s.lo) s.lo = value;
    if (value > s.hi) s.hi = value;
    // Write through to the session binding: the Variables panel follows the
    // slider. Sibling cells' sliders follow via syncFromVars once the store
    // re-validates.
    const row = vars.rows.find((r) => r.status.symbol === name);
    if (row) vars.edit(row.id, { value: overrideValue(value) });
    this.recompute(id);
  }

  setSliderBounds(id: number, name: string, lo: number, hi: number): void {
    const c = this.cells.find((x) => x.id === id);
    const s = c?.sliders.find((x) => x.name === name);
    if (!c || !s || !Number.isFinite(lo) || !Number.isFinite(hi) || lo >= hi)
      return;
    s.lo = lo;
    s.hi = hi;
    if (s.value < lo || s.value > hi) {
      this.setSlider(id, name, Math.min(Math.max(s.value, lo), hi));
    } else {
      this.#persist();
    }
  }

  /**
   * The panel edited (or unset) a binding: move every cell slider of that
   * name to the new value and re-run its cell; drop sliders whose binding is
   * gone or no longer numeric. No-ops when already in sync, so the write-back
   * from setSlider cannot loop.
   */
  syncFromVars(): void {
    const numeric = new Map<string, number>();
    const bound = new Set<string>();
    for (const b of vars.active) {
      if (b.kind !== "expression") continue;
      bound.add(b.name);
      const v = Number(b.value);
      if (Number.isFinite(v)) numeric.set(b.name, v);
    }
    for (const c of this.cells) {
      if (c.sliders.length === 0) continue;
      let changed = false;
      const kept = c.sliders.filter((s) => {
        const v = numeric.get(s.name);
        if (v !== undefined) {
          if (v !== s.value) {
            s.value = v;
            if (v < s.lo) s.lo = v;
            if (v > s.hi) s.hi = v;
            changed = true;
          }
          return true;
        }
        // Bound but no longer numeric (a := x + 1): the slider is meaningless.
        if (bound.has(s.name)) {
          changed = true;
          return false;
        }
        // Not in the active set: keep while its row still exists (rows are
        // mid-validation at startup); drop only when truly unset.
        const rowExists = vars.rows.some(
          (r) => r.status.symbol === s.name || r.name === s.name,
        );
        if (rowExists) return true;
        changed = true;
        return false;
      });
      if (kept.length !== c.sliders.length) c.sliders = kept;
      if (changed) this.recompute(c.id);
    }
  }

  /** Fold or unfold a tall output (explicit choice persists). */
  setCollapsed(id: number, v: boolean): void {
    const c = this.cells.find((x) => x.id === id);
    if (!c) return;
    c.collapsed = v;
    this.#persist();
  }

  clear(): void {
    this.cells = [];
    this.#persist();
  }

  #persist(): void {
    try {
      const cells = this.cells
        .filter((c) => c.result !== null)
        .slice(-CAP)
        .map((c) => ({
          input: c.input,
          inputLatex: c.inputLatex,
          result: c.result,
          sliders: c.sliders,
          collapsed: c.collapsed,
          ts: c.ts,
        }));
      localStorage.setItem(KEY, JSON.stringify({ v: 1, cells }));
    } catch {
      /* storage unavailable or quota exceeded */
    }
  }
}

export const notebook = new NotebookStore();
export { runLine };
export type { CellResult };
