// Graphing-calculator document: an ordered list of expression rows plus the
// viewport, persisted to localStorage. Most rows hold raw text (parsed and
// sampled reactively in GraphCalculator.svelte); a "table" row instead holds a
// list of (x, y) data points and an optional regression model, plotted as
// points with a fitted curve overlaid.
import type { View } from "./viewport";

/** One (x, y) data point in a table row; kept as raw strings the user typed. */
export interface DataPoint {
  x: string;
  y: string;
}

/** Regression model for a table row ("" = plot the points only). */
export type FitModelName = "" | "linear" | "quadratic" | "cubic" | "exp" | "power" | "log";

import { LINE_STYLES, LINE_WEIGHTS, type LineStyle, type LineWeight } from "./style";
export { LINE_STYLES, LINE_WEIGHTS, type LineStyle, type LineWeight };

export interface ExprRow {
  id: number;
  /** "expr" rows plot `text`; "table" rows plot `points` (+ optional `fit`). */
  kind: "expr" | "table";
  /** Raw expression as typed, e.g. "a*sin(x)" or "x^2 = 4 - y^2" (expr rows). */
  text: string;
  /** Data points (table rows); always ends with a blank row for entry. */
  points: DataPoint[];
  /** Regression model overlaid on the points (table rows). */
  fit: FitModelName;
  /** Series color. */
  color: string;
  /** Line dash style and stroke/point weight (per-row, Desmos-like). */
  lineStyle: LineStyle;
  weight: LineWeight;
  /** Optional text label drawn at the point(s) / along the curve (empty = off). */
  label: string;
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
const POINT_CAP = 200; // max data points in one table
const DEFAULT_VIEW: View = { cx: 0, cy: 0, scale: 40 };

let nextId = 1;

interface PersistedRow {
  text: string;
  color: string;
  visible: boolean;
  lineStyle?: string;
  weight?: string;
  label?: string;
  kind?: "expr" | "table";
  points?: DataPoint[];
  /** Loose `string` (not FitModelName) so decoded share data is assignable;
   *  normalizeRow validates it against FIT_MODELS. */
  fit?: string;
}
interface Persisted {
  rows: PersistedRow[];
  view: View;
}

const FIT_MODELS: FitModelName[] = ["", "linear", "quadratic", "cubic", "exp", "power", "log"];

/** Coerce arbitrary persisted/shared data into a clean point list + trailing blank. */
export function sanitizePoints(raw: unknown): DataPoint[] {
  const out: DataPoint[] = [];
  if (Array.isArray(raw)) {
    for (const p of raw.slice(0, POINT_CAP)) {
      if (!p || typeof p !== "object") continue;
      const r = p as Record<string, unknown>;
      const x = typeof r.x === "string" ? r.x.slice(0, 64) : "";
      const y = typeof r.y === "string" ? r.y.slice(0, 64) : "";
      out.push({ x, y });
    }
  }
  return withTrailingBlank(out);
}

function withTrailingBlank(points: DataPoint[]): DataPoint[] {
  const last = points[points.length - 1];
  if (!last || last.x.trim() !== "" || last.y.trim() !== "") points.push({ x: "", y: "" });
  return points;
}

function normalizeRow(r: PersistedRow): ExprRow {
  const kind = r.kind === "table" ? "table" : "expr";
  return {
    id: nextId++,
    kind,
    text: typeof r.text === "string" ? r.text : "",
    points: kind === "table" ? sanitizePoints(r.points) : [],
    fit: kind === "table" && FIT_MODELS.includes(r.fit as FitModelName) ? (r.fit as FitModelName) : "",
    color: typeof r.color === "string" ? r.color : GRAPH_COLORS[0],
    lineStyle: LINE_STYLES.includes(r.lineStyle as LineStyle) ? (r.lineStyle as LineStyle) : "solid",
    weight: LINE_WEIGHTS.includes(r.weight as LineWeight) ? (r.weight as LineWeight) : "normal",
    label: typeof r.label === "string" ? r.label.slice(0, 60) : "",
    visible: r.visible !== false,
  };
}

function load(): { rows: ExprRow[]; view: View } {
  try {
    const raw = localStorage.getItem(KEY);
    if (!raw) return { rows: [seedRow()], view: { ...DEFAULT_VIEW } };
    const p = JSON.parse(raw) as Partial<Persisted>;
    const rows: ExprRow[] = Array.isArray(p.rows)
      ? p.rows
          .filter((r): r is PersistedRow => !!r && (typeof r.text === "string" || r.kind === "table"))
          .slice(0, CAP)
          .map(normalizeRow)
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
  return { id: nextId++, kind: "expr", text: "", points: [], fit: "", color: GRAPH_COLORS[0], lineStyle: "solid", weight: "normal", label: "", visible: true };
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
    const row: ExprRow = { id: nextId++, kind: "expr", text, points: [], fit: "", color: this.#nextColor(), lineStyle: "solid", weight: "normal", label: "", visible: true };
    this.rows.push(row);
    this.persist();
    return row.id;
  }

  /** Add an empty data table (three blank point rows to start). */
  addTable(): number {
    if (this.rows.length >= CAP) return -1;
    const row: ExprRow = {
      id: nextId++,
      kind: "table",
      text: "",
      points: [
        { x: "", y: "" },
        { x: "", y: "" },
        { x: "", y: "" },
      ],
      fit: "",
      color: this.#nextColor(),
      lineStyle: "solid",
      weight: "normal",
      label: "",
      visible: true,
    };
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
      this.persistSoon(); // coalesce keystrokes into one write
    }
  }

  /** Edit one coordinate of a table point, keeping a trailing blank row. */
  setPoint(id: number, index: number, patch: { x?: string; y?: string }): void {
    const r = this.rows.find((x) => x.id === id);
    if (!r || r.kind !== "table") return;
    const p = r.points[index];
    if (!p) return;
    if (patch.x !== undefined) p.x = patch.x;
    if (patch.y !== undefined) p.y = patch.y;
    // Auto-grow: keep exactly one trailing blank row so there is always an
    // empty slot to type into (capped).
    const last = r.points[r.points.length - 1];
    if ((last.x.trim() !== "" || last.y.trim() !== "") && r.points.length < POINT_CAP) {
      r.points.push({ x: "", y: "" });
    }
    this.persistSoon();
  }

  /** Delete a table point (never leaves the table without a blank entry row). */
  removePoint(id: number, index: number): void {
    const r = this.rows.find((x) => x.id === id);
    if (!r || r.kind !== "table") return;
    r.points.splice(index, 1);
    if (r.points.length === 0) r.points.push({ x: "", y: "" });
    this.persist();
  }

  setFit(id: number, fit: FitModelName): void {
    const r = this.rows.find((x) => x.id === id);
    if (r && r.kind === "table") {
      r.fit = fit;
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
  /** Update a row's line dash style and/or stroke weight. */
  setStyle(id: number, patch: { lineStyle?: LineStyle; weight?: LineWeight }): void {
    const r = this.rows.find((x) => x.id === id);
    if (!r) return;
    if (patch.lineStyle) r.lineStyle = patch.lineStyle;
    if (patch.weight) r.weight = patch.weight;
    this.persist();
  }
  /** Set a row's display label (empty string clears it). */
  setLabel(id: number, label: string): void {
    const r = this.rows.find((x) => x.id === id);
    if (!r) return;
    r.label = label.slice(0, 60);
    this.persistSoon();
  }
  toggleVisible(id: number): void {
    const r = this.rows.find((x) => x.id === id);
    if (r) {
      r.visible = !r.visible;
      this.persist();
    }
  }

  #rowSnapshot(r: ExprRow): PersistedRow {
    const style = { lineStyle: r.lineStyle, weight: r.weight, label: r.label };
    return r.kind === "table"
      ? { kind: "table", text: "", color: r.color, visible: r.visible, ...style, points: r.points.map((p) => ({ x: p.x, y: p.y })), fit: r.fit }
      : { text: r.text, color: r.color, visible: r.visible, ...style };
  }

  /** The rows + viewport as a plain snapshot (for a share link). */
  snapshot(): { rows: PersistedRow[]; view: View } {
    return { rows: this.rows.map((r) => this.#rowSnapshot(r)), view: { ...this.view } };
  }
  /** Replace the whole document (from a shared link). */
  replaceAll(rows: PersistedRow[], view: View): void {
    this.rows = rows.length ? rows.slice(0, CAP).map(normalizeRow) : [seedRow()];
    this.view = {
      cx: Number.isFinite(view.cx) ? view.cx : DEFAULT_VIEW.cx,
      cy: Number.isFinite(view.cy) ? view.cy : DEFAULT_VIEW.cy,
      scale: Number.isFinite(view.scale) && view.scale > 0 ? view.scale : DEFAULT_VIEW.scale,
    };
    this.persist();
  }

  #persistTimer: ReturnType<typeof setTimeout> | null = null;
  /** Debounced write for high-frequency changes (typing, pan/zoom frames). */
  persistSoon(): void {
    if (this.#persistTimer !== null) clearTimeout(this.#persistTimer);
    this.#persistTimer = setTimeout(() => {
      this.#persistTimer = null;
      this.persist();
    }, 250);
  }

  persist(): void {
    if (this.#persistTimer !== null) {
      clearTimeout(this.#persistTimer);
      this.#persistTimer = null;
    }
    try {
      const data: Persisted = { rows: this.rows.map((r) => this.#rowSnapshot(r)), view: this.view };
      localStorage.setItem(KEY, JSON.stringify(data));
    } catch {
      /* storage unavailable */
    }
  }
}

export const graph = new GraphStore();
