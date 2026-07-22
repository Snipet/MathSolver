// Unit tests for marching-squares contouring (web/src/lib/graph/contour.ts).
import { marchingSquares, inequalityMask } from "../web/src/lib/graph/contour.ts";

let pass = 0, fail = 0;
const check = (n, c, e = "") => { if (c) { pass++; console.log("PASS  " + n); } else { fail++; console.log(`FAIL  ${n}${e ? "  [" + e + "]" : ""}`); } };

// Sample g = x^2 + y^2 - 4 (circle radius 2) over [-3,3]^2.
const NX = 121, NY = 121, X0 = -3, X1 = 3, Y0 = -3, Y1 = 3;
const g = new Array(NX * NY);
for (let j = 0; j < NY; j++)
  for (let i = 0; i < NX; i++) {
    const x = X0 + ((X1 - X0) * i) / (NX - 1);
    const y = Y0 + ((Y1 - Y0) * j) / (NY - 1);
    g[j * NX + i] = x * x + y * y - 4;
  }

const c = marchingSquares(g, NX, NY, X0, X1, Y0, Y1, 0);
// Every contour vertex should lie on radius 2 (within a grid cell).
let maxErr = 0, count = 0;
for (let k = 0; k < c.xs.length; k++) {
  if (c.xs[k] === null) continue;
  const r = Math.hypot(c.xs[k], c.ys[k]);
  maxErr = Math.max(maxErr, Math.abs(r - 2));
  count++;
}
check("circle contour lands on radius 2", count > 100 && maxErr < 0.05, `pts=${count} maxErr=${maxErr.toFixed(4)}`);

// The contour should roughly close: bounding box ≈ [-2,2] in both axes.
let xmin = 1e9, xmax = -1e9, ymin = 1e9, ymax = -1e9;
for (let k = 0; k < c.xs.length; k++) {
  if (c.xs[k] === null) continue;
  xmin = Math.min(xmin, c.xs[k]); xmax = Math.max(xmax, c.xs[k]);
  ymin = Math.min(ymin, c.ys[k]); ymax = Math.max(ymax, c.ys[k]);
}
check("contour spans the full circle", Math.abs(xmin + 2) < 0.05 && Math.abs(xmax - 2) < 0.05 && Math.abs(ymin + 2) < 0.05 && Math.abs(ymax - 2) < 0.05, `x=[${xmin.toFixed(2)},${xmax.toFixed(2)}]`);

// No contour where the field never crosses zero (all positive).
const gp = g.map((v) => v + 100);
const c2 = marchingSquares(gp, NX, NY, X0, X1, Y0, Y1, 0);
check("no contour when the field never crosses the level", c2.xs.every((v) => v === null) || c2.xs.length === 0);

// Inequality mask: inside the circle (g<0) is the disk of radius 2.
const mask = inequalityMask(g, "<");
let inside = 0, outside = 0;
for (let j = 0; j < NY; j++)
  for (let i = 0; i < NX; i++) {
    const x = X0 + ((X1 - X0) * i) / (NX - 1);
    const y = Y0 + ((Y1 - Y0) * j) / (NY - 1);
    const m = mask[j * NX + i];
    if (Math.hypot(x, y) < 1.9 && m) inside++;
    if (Math.hypot(x, y) > 2.1 && !m) outside++;
  }
check("inequality mask marks the interior disk", inside > 500 && outside > 500, `in=${inside} out=${outside}`);
check("mask handles NaN/null cells", inequalityMask([1, null, -1, NaN], "<")[2] === 1 && inequalityMask([1, null], "<")[1] === 0);

console.log(`\n${fail === 0 ? "ALL PASS" : "FAILURES"}: ${pass} passed, ${fail} failed`);
process.exit(fail === 0 ? 0 : 1);
