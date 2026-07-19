// Console session: an ordered list of evaluated cells, persisted to
// localStorage so the notebook survives a reload (like a Mathematica notebook
// document). Cell results are the plain-data CellResult union, so they
// serialize directly and restore without re-evaluation (re-running would
// double-commit `:=` assignments, which the shared `vars` store already
// persists on its own).
import { runLine, type CellResult } from "./run";
import { buildConsolePreview } from "./preview";

export interface Cell {
  id: number;
  /** The raw line as entered (echoed as the In[] prompt). */
  input: string;
  /** Typeset (LaTeX) rendering of the parsed input; null when the line has
   *  no math form (help, vars, plugin calls, parse errors). */
  inputLatex: string | null;
  /** null while the engine is working; set once evaluated. */
  result: CellResult | null;
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
        ts: typeof o.ts === "number" ? o.ts : Date.now(),
      });
    }
    return cells.slice(-CAP);
  } catch {
    return [];
  }
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
