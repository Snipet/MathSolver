// Unit tests for graph/slopefield.ts: bilinear sampling, field segments, and
// RK4 solution-curve integration. Run: node tools/graph_slopefield_test.mjs
import { sampleAt, fieldSegments, integrateCurve } from "../web/src/lib/graph/slopefield.ts";

let pass = 0,
  fail = 0;
const check = (n, c, e = "") => {
  if (c) {
    pass++;
    console.log("PASS  " + n);
  } else {
    fail++;
    console.log(`FAIL  ${n}${e ? "  [" + e + "]" : ""}`);
  }
};
const near = (a, b, tol) => Math.abs(a - b) <= tol;

// Build a grid of f(x,y) over [x0,x1]×[y0,y1] (row-major, y outer).
function makeGrid(f, x0, x1, y0, y1, nx, ny) {
  const g = [];
  const dx = (x1 - x0) / (nx - 1);
  const dy = (y1 - y0) / (ny - 1);
  for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) g.push(f(x0 + i * dx, y0 + j * dy));
  return { g, nx, ny, x0, x1, y0, y1 };
}

// --- sampleAt: f(x,y) = x (independent of y) ---
const gx = makeGrid((x) => x, 0, 4, 0, 4, 5, 5);
check("sampleAt interpolates f=x", near(sampleAt(gx, 2.5, 1.3), 2.5, 1e-9), sampleAt(gx, 2.5, 1.3));
check("sampleAt at a node", near(sampleAt(gx, 3, 3), 3, 1e-9));
check("sampleAt out of bounds → null", sampleAt(gx, 9, 0) === null);
// bilinear of f(x,y) = x + y
const gxy = makeGrid((x, y) => x + y, 0, 4, 0, 4, 5, 5);
check("sampleAt interpolates f=x+y", near(sampleAt(gxy, 1.5, 2.5), 4.0, 1e-9), sampleAt(gxy, 1.5, 2.5));
// a null corner makes the cell undefined
const gnull = makeGrid(() => 1, 0, 4, 0, 4, 5, 5);
gnull.g[0] = null; // node (0,0)
check("sampleAt null corner → null", sampleAt(gnull, 0.5, 0.5) === null);
check("sampleAt away from null corner ok", sampleAt(gnull, 3.5, 3.5) === 1);

// --- fieldSegments: one segment per cell, 3 entries each (2 pts + null) ---
const seg = fieldSegments(gx, 6, 4);
check("fieldSegments count", seg.xs.length === 6 * 4 * 3, String(seg.xs.length));
check("fieldSegments breaks between segments", seg.xs[2] === null && seg.ys[2] === null);
// horizontal slope (f=0) → segments are horizontal (equal y endpoints)
const g0 = makeGrid(() => 0, -2, 2, -2, 2, 9, 9);
const seg0 = fieldSegments(g0, 4, 4);
check("fieldSegments f=0 are horizontal", near(seg0.ys[0], seg0.ys[1], 1e-9));

// --- integrateCurve: dy/dx = 1 → straight line y = x + c ---
const g1 = makeGrid(() => 1, -5, 5, -5, 5, 41, 41);
const line = integrateCurve(g1, 0, 0, { step: 0.02, maxSteps: 400 });
const iMid = line.xs.findIndex((x) => near(x, 2, 0.03));
check("integrateCurve dy/dx=1 passes through origin", near(line.xs[line.xs.indexOf(0)] ?? 0, 0, 1e-9));
check("integrateCurve dy/dx=1 gives y≈x", iMid >= 0 && near(line.ys[iMid], line.xs[iMid], 1e-6), iMid >= 0 ? line.ys[iMid] : "no x≈2");

// --- integrateCurve: dy/dx = y → y = y0·e^x (through (0,1) → e at x=1) ---
const gY = makeGrid((x, y) => y, -3, 3, -3, 3, 121, 121);
const exp = integrateCurve(gY, 0, 1, { step: 0.01, maxSteps: 400 });
let best = -1,
  bestErr = Infinity;
for (let k = 0; k < exp.xs.length; k++) {
  const e = Math.abs(exp.xs[k] - 1);
  if (e < bestErr) {
    bestErr = e;
    best = k;
  }
}
check("integrateCurve dy/dx=y ≈ e at x=1", best >= 0 && near(exp.ys[best], Math.E, 0.03 * Math.E), best >= 0 ? exp.ys[best].toFixed(4) : "none");
// curve stops when it runs off the grid (never exceeds the y-range)
check("integrateCurve stays within grid y-range", exp.ys.every((y) => y >= -3.0001 && y <= 3.0001));
// seed off the grid → empty
check("integrateCurve off-grid seed → empty", integrateCurve(gY, 99, 99, { step: 0.01, maxSteps: 10 }).xs.length === 0);

console.log(`\n${fail === 0 ? "ALL PASS" : "FAILURES"}: ${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
