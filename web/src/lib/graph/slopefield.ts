// Slope field + numeric ODE integration for the grapher, all pure and DOM-free
// (unit-tested in tools/graph_slopefield_test.mjs).
//
// Given the RHS f(x, y) of a first-order ODE y' = f(x, y) sampled on a
// row-major (y outer) grid over [x0, x1] × [y0, y1] — the same layout the
// engine's `sampleGrid` returns — this builds the direction-field line segments
// and integrates solution curves with classic RK4 over a *bilinear interpolant*
// of the grid. Sampling f once and interpolating keeps the curves cheap enough
// to re-integrate live while an initial point is dragged.

/** A scalar field f(x, y) sampled on a regular grid (row-major, y outer). */
export interface Grid {
  /** g[j * nx + i] is f at (x0 + i·dx, y0 + j·dy); null where f is undefined. */
  g: (number | null)[];
  nx: number;
  ny: number;
  x0: number;
  x1: number;
  y0: number;
  y1: number;
}

/** Bilinear interpolation of f at (x, y). Null outside the grid or when any of
 *  the four surrounding samples is undefined (a pole / domain edge). */
export function sampleAt(grid: Grid, x: number, y: number): number | null {
  const { g, nx, ny, x0, x1, y0, y1 } = grid;
  if (!(x >= x0 && x <= x1 && y >= y0 && y <= y1)) return null;
  const dx = (x1 - x0) / (nx - 1);
  const dy = (y1 - y0) / (ny - 1);
  let i = Math.floor((x - x0) / dx);
  let j = Math.floor((y - y0) / dy);
  if (i >= nx - 1) i = nx - 2;
  if (j >= ny - 1) j = ny - 2;
  if (i < 0) i = 0;
  if (j < 0) j = 0;
  const gx0 = x0 + i * dx;
  const gy0 = y0 + j * dy;
  const tx = dx === 0 ? 0 : (x - gx0) / dx;
  const ty = dy === 0 ? 0 : (y - gy0) / dy;
  const g00 = g[j * nx + i];
  const g10 = g[j * nx + i + 1];
  const g01 = g[(j + 1) * nx + i];
  const g11 = g[(j + 1) * nx + i + 1];
  if (g00 == null || g10 == null || g01 == null || g11 == null) return null;
  if (!(Number.isFinite(g00) && Number.isFinite(g10) && Number.isFinite(g01) && Number.isFinite(g11)))
    return null;
  const a = g00 * (1 - tx) + g10 * tx;
  const b = g01 * (1 - tx) + g11 * tx;
  return a * (1 - ty) + b * ty;
}

/**
 * Direction-field segments: a `cols`×`rows` lattice of short line segments,
 * each centred on its cell and pointing along (1, f)/‖(1, f)‖. Returned as
 * polyline coordinates with a `null` between segments so a single broken
 * polyline draws them all. Units are square in the grapher, so a world-space
 * normalized direction renders at the correct on-screen angle.
 */
export function fieldSegments(
  grid: Grid,
  cols: number,
  rows: number,
): { xs: (number | null)[]; ys: (number | null)[] } {
  const { x0, x1, y0, y1 } = grid;
  const xs: (number | null)[] = [];
  const ys: (number | null)[] = [];
  const cellW = (x1 - x0) / cols;
  const cellH = (y1 - y0) / rows;
  const halfLen = 0.36 * Math.min(cellW, cellH);
  for (let r = 0; r < rows; r++) {
    for (let c = 0; c < cols; c++) {
      const cx = x0 + (c + 0.5) * cellW;
      const cy = y0 + (r + 0.5) * cellH;
      const m = sampleAt(grid, cx, cy);
      if (m === null || !Number.isFinite(m)) continue;
      const inv = 1 / Math.sqrt(1 + m * m);
      const ux = inv; // (1, m) normalized
      const uy = m * inv;
      xs.push(cx - halfLen * ux, cx + halfLen * ux, null);
      ys.push(cy - halfLen * uy, cy + halfLen * uy, null);
    }
  }
  return { xs, ys };
}

interface IntegrateOptions {
  /** Signed x-step (world units). */
  step: number;
  /** Max steps per direction. */
  maxSteps: number;
  /** Stop if |y| leaves [y0 − yPad, y1 + yPad]; defaults to no extra pad. */
  yPad?: number;
}

/** One RK4 step of y' = f(x, y) from (x, y) by h; null if f is undefined at any
 *  stage (curve ran off the grid / into a pole). */
function rk4(grid: Grid, x: number, y: number, h: number): [number, number] | null {
  const k1 = sampleAt(grid, x, y);
  if (k1 === null) return null;
  const k2 = sampleAt(grid, x + h / 2, y + (h / 2) * k1);
  if (k2 === null) return null;
  const k3 = sampleAt(grid, x + h / 2, y + (h / 2) * k2);
  if (k3 === null) return null;
  const k4 = sampleAt(grid, x + h, y + h * k3);
  if (k4 === null) return null;
  const ny = y + (h / 6) * (k1 + 2 * k2 + 2 * k3 + k4);
  if (!Number.isFinite(ny)) return null;
  return [x + h, ny];
}

/** March from (sx, sy) in one direction, returning the points *after* the
 *  start (in integration order). Stops at the grid edge or maxSteps. */
function march(grid: Grid, sx: number, sy: number, opts: IntegrateOptions): [number, number][] {
  const pts: [number, number][] = [];
  const yLo = grid.y0 - (opts.yPad ?? 0);
  const yHi = grid.y1 + (opts.yPad ?? 0);
  let x = sx;
  let y = sy;
  for (let n = 0; n < opts.maxSteps; n++) {
    const next = rk4(grid, x, y, opts.step);
    if (!next) break;
    [x, y] = next;
    if (y < yLo || y > yHi) break;
    pts.push([x, y]);
    if (x < grid.x0 || x > grid.x1) break;
  }
  return pts;
}

/**
 * The solution curve of y' = f(x, y) through (sx, sy): integrate backward and
 * forward from the seed and join them. Returned as polyline coordinates; empty
 * when the seed lies off the grid.
 */
export function integrateCurve(
  grid: Grid,
  sx: number,
  sy: number,
  opts: IntegrateOptions,
): { xs: number[]; ys: number[] } {
  if (sampleAt(grid, sx, sy) === null) return { xs: [], ys: [] };
  const back = march(grid, sx, sy, { ...opts, step: -opts.step });
  const fwd = march(grid, sx, sy, opts);
  const xs: number[] = [];
  const ys: number[] = [];
  for (let k = back.length - 1; k >= 0; k--) {
    xs.push(back[k][0]);
    ys.push(back[k][1]);
  }
  xs.push(sx);
  ys.push(sy);
  for (const [px, py] of fwd) {
    xs.push(px);
    ys.push(py);
  }
  return { xs, ys };
}
