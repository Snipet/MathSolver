// Graph viewport math — the coordinate transform between world (math) space
// and screen pixels, plus pan/zoom operations and nice axis ticks.
//
// DOM-free and pure so it is unit-testable (tools/graph_viewport_test.mjs) and
// shared by the interactive GraphCanvas. The view is center + uniform scale:
// `scale` is pixels per world unit, so units stay square (Desmos default) and
// a wider canvas simply shows a wider x-range.

export interface View {
  /** World x at the canvas horizontal center. */
  cx: number;
  /** World y at the canvas vertical center. */
  cy: number;
  /** Pixels per world unit (> 0). */
  scale: number;
}

/** Inequality shading: a sign mask over a world-space grid (row-major ny×nx). */
export interface RegionMask {
  x0: number;
  x1: number;
  y0: number;
  y1: number;
  nx: number;
  ny: number;
  /** 1 where the inequality holds. */
  mask: Uint8Array;
}

/** How one coordinate of a plotted point can be moved by dragging it. */
export type AxisSource =
  | { kind: "literal" } // a numeric literal in the row text — rewrite it
  | { kind: "var"; name: string } // a session variable — write it (moves app-wide)
  | { kind: "expr" }; // a non-atomic expression — locked (not draggable)

/** A draggable point handle, aligned index-for-index with a points series. */
export interface PointHandle {
  rowId: number;
  coordIndex: number; // index into the row's coords list
  x: AxisSource;
  y: AxisSource;
}

/** A drawable in world space: a polyline, a set of points, or a shaded region. */
export interface DrawSeries {
  id: string;
  color: string;
  visible: boolean;
  kind: "line" | "points" | "region";
  /** For "line"/"points": aligned world coords; a null in either breaks the
   *  polyline (x can be null for x=f(y) where f is undefined). */
  xs: (number | null)[];
  ys: (number | null)[];
  /** For "region": the inequality shading grid. */
  region?: RegionMask;
}

/**
 * Break a sampled polyline across likely poles. Between two consecutive finite
 * samples that straddle zero (opposite signs) with a jump far larger than the
 * visible span — the signature of a `1/x`, `tan`, `sec`, `csc`, `cot`
 * asymptote — null the sample nearer the pole so the renderer lifts the pen
 * instead of drawing a spurious near-vertical connector across the canvas.
 * `span` is the visible extent of the SAMPLED axis (the y-span for `y=f(x)`,
 * the x-span for `x=f(y)`). A genuinely steep but continuous segment (jump ≤
 * ~8 screen-heights) is left intact. Returns a new array; the input is
 * untouched. Same-sign spikes (e.g. `1/x²`) are intentionally left as drawn.
 */
export function breakDiscontinuities(
  ys: (number | null)[],
  span: number,
): (number | null)[] {
  const out = ys.slice();
  if (!(span > 0) || !Number.isFinite(span)) return out;
  const thresh = span * 8;
  for (let i = 0; i + 1 < ys.length; i++) {
    const a = ys[i];
    const b = ys[i + 1];
    if (a === null || b === null || !Number.isFinite(a) || !Number.isFinite(b)) continue;
    // A pole's signature: the two samples straddle zero, BOTH are well off the
    // visible span (so the connector would sweep the whole canvas), and the
    // jump dwarfs the span. Requiring both magnitudes large avoids breaking the
    // steep-but-real recovery segment just past a pole (one side small).
    if (
      a * b < 0 &&
      Math.abs(a - b) > thresh &&
      Math.min(Math.abs(a), Math.abs(b)) > span
    ) {
      // Null the sample nearer the asymptote (larger magnitude), keeping as
      // much of each branch as possible while breaking the connector.
      if (Math.abs(a) >= Math.abs(b)) out[i] = null;
      else out[i + 1] = null;
    }
  }
  return out;
}

// Deep zoom is allowed, but bounded so float precision stays usable.
export const MIN_SCALE = 1e-6;
export const MAX_SCALE = 1e9;

function clampScale(s: number): number {
  return s < MIN_SCALE ? MIN_SCALE : s > MAX_SCALE ? MAX_SCALE : s;
}

export function xToPx(x: number, v: View, w: number): number {
  return w / 2 + (x - v.cx) * v.scale;
}
export function yToPx(y: number, v: View, h: number): number {
  return h / 2 - (y - v.cy) * v.scale;
}
export function pxToX(px: number, v: View, w: number): number {
  return v.cx + (px - w / 2) / v.scale;
}
export function pxToY(py: number, v: View, h: number): number {
  return v.cy - (py - h / 2) / v.scale;
}

/** Visible world x-range [min, max] for a canvas of width w. */
export function xRange(v: View, w: number): [number, number] {
  return [pxToX(0, v, w), pxToX(w, v, w)];
}
/** Visible world y-range [min, max] for a canvas of height h. */
export function yRange(v: View, h: number): [number, number] {
  return [pxToY(h, v, h), pxToY(0, v, h)];
}

/** Pan the view by a screen-pixel delta (drag). */
export function panned(v: View, dxPx: number, dyPx: number): View {
  return { cx: v.cx - dxPx / v.scale, cy: v.cy + dyPx / v.scale, scale: v.scale };
}

/**
 * Zoom by `factor` about the screen point (px, py), keeping the world point
 * under the cursor fixed — the natural wheel/pinch behavior.
 */
export function zoomedAt(
  v: View,
  factor: number,
  px: number,
  py: number,
  w: number,
  h: number,
): View {
  const wx = pxToX(px, v, w);
  const wy = pxToY(py, v, h);
  const scale = clampScale(v.scale * factor);
  return {
    scale,
    cx: wx - (px - w / 2) / scale,
    cy: wy - (h / 2 - py) / scale,
  };
}

/** A "nice" step (1, 2 or 5 × 10^k) giving about `target` ticks over `range`. */
export function niceStep(range: number, target: number): number {
  const raw = Math.abs(range) / Math.max(1, target);
  if (!(raw > 0) || !Number.isFinite(raw)) return 1;
  const mag = 10 ** Math.floor(Math.log10(raw));
  const n = raw / mag;
  const mult = n < 1.5 ? 1 : n < 3.5 ? 2 : n < 7.5 ? 5 : 10;
  return mult * mag;
}

/** Tick values at multiples of `step` covering [lo, hi] (inclusive-ish). */
export function ticks(lo: number, hi: number, step: number): number[] {
  if (!(step > 0) || !Number.isFinite(step)) return [];
  const out: number[] = [];
  const start = Math.ceil(lo / step) * step;
  // Guard against pathological ranges producing millions of ticks.
  const count = Math.floor((hi - start) / step);
  if (count > 10000 || count < 0) return [];
  for (let i = 0; i <= count; i++) out.push(start + i * step);
  return out;
}

/**
 * Decimal places needed to render tick labels at `step` without redundant
 * or lost precision.
 */
export function tickDecimals(step: number): number {
  if (!(step > 0)) return 0;
  const d = -Math.floor(Math.log10(step));
  return d > 0 ? Math.min(d, 8) : 0;
}
