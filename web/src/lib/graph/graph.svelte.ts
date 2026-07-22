// Graphing-calculator document: an ordered list of expression rows plus the
// viewport, persisted to localStorage. Rows hold raw text only; parsing,
// sampling, and free-variable detection happen reactively in
// GraphCalculator.svelte (which needs the async engine + $effect).
import type { View } from "./viewport";

export interface ExprRow {
  id: number;
  /** Raw expression as typed, e.g. "a*sin(x)" or "x^2 = 4 - y^2". */
  text: string;
  /** Series color. */
  color: string;
  visible: boolean;
}

// Desmos-like default palette, cycled as rows are added.
export const GRAPH_COLORS = [
  "#2563eb", // blue
  "#dc2626", // red
  "#16a34a", // green
  "#9333ea", // purple
  "#ea580c", // orange
  "#0891b2", // teal
  "#c026d3", // magenta
  "#65a30d", // olive
];

const KEY = "mathsolver.graph";
const CAP = 40;
const DEFAULT_VIEW: View = { cx: 0, cy: 0, scale: 40 };

let nextId = 1;

interface Persisted {
  rows: { text: string; color: string; visible: boolean }[];
  view: View;
}

function load(): { rows: ExprRow[]; view: View } {
  try {
    const raw = localStorage.getItem(KEY);
    if (!raw) return { rows: [seedRow()], view: { ...DEFAULT_VIEW } };
    const p = JSON.parse(raw) as Partial<Persisted>;
    const rows: ExprRow[] = Array.isArray(p.rows)
      ? p.rows
          .filter((r) => r && typeof r.text === "string")
          .slice(0, CAP)
          .map((r) => ({
            id: nextId++,
            text: r.text,
            color: typeof r.color === "string" ? r.color : GRAPH_COLORS[0],
            visible: r.visible !== false,
          }))
      : [];
    const v = p.view;
    const view: View =
      v && Number.isFinite(v.cx) && Number.isFinite(v.cy) && Number.isFinite(v.scale) && v.scale > 0
        ? { cx: v.cx, cy: v.cy, scale: v.scale }
        : { ...DEFAULT_VIEW };
    return { rows: rows.length ? rows : [seedRow()], view };
  } catch {
    return { rows: [seedRow()], view: { ...DEFAULT_VIEW } };
  }
}

function seedRow(): ExprRow {
  return { id: nextId++, text: "", color: GRAPH_COLORS[0], visible: true };
}

class GraphStore {
  rows = $state<ExprRow[]>([]);
  view = $state<View>({ ...DEFAULT_VIEW });

  constructor() {
    const { rows, view } = load();
    this.rows = rows;
    this.view = view;
  }

  #nextColor(): string {
    const used = this.rows.map((r) => r.color);
    return GRAPH_COLORS.find((c) => !used.includes(c)) ?? GRAPH_COLORS[this.rows.length % GRAPH_COLORS.length];
  }

  addRow(text = ""): number {
    if (this.rows.length >= CAP) return -1;
    const row: ExprRow = { id: nextId++, text, color: this.#nextColor(), visible: true };
    this.rows.push(row);
    this.persist();
    return row.id;
  }

  removeRow(id: number): void {
    this.rows = this.rows.filter((r) => r.id !== id);
    if (this.rows.length === 0) this.rows = [seedRow()];
    this.persist();
  }

  setText(id: number, text: string): void {
    const r = this.rows.find((x) => x.id === id);
    if (r) {
      r.text = text;
      this.persist();
    }
  }
  setColor(id: number, color: string): void {
    const r = this.rows.find((x) => x.id === id);
    if (r) {
      r.color = color;
      this.persist();
    }
  }
  toggleVisible(id: number): void {
    const r = this.rows.find((x) => x.id === id);
    if (r) {
      r.visible = !r.visible;
      this.persist();
    }
  }

  setView(v: View): void {
    this.view = v;
  }

  persist(): void {
    try {
      const data: Persisted = {
        rows: this.rows.map((r) => ({ text: r.text, color: r.color, visible: r.visible })),
        view: this.view,
      };
      localStorage.setItem(KEY, JSON.stringify(data));
    } catch {
      /* storage unavailable */
    }
  }
}

export const graph = new GraphStore();
