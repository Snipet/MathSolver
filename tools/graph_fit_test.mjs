// Unit tests for zoom-to-fit bounds + view framing (web/src/lib/graph/viewport.ts).
import { seriesBounds, fitView, xRange, yRange, MIN_SCALE, MAX_SCALE } from "../web/src/lib/graph/viewport.ts";

let pass = 0, fail = 0;
const check = (n, c) => { if (c) { pass++; console.log("PASS  " + n); } else { fail++; console.log("FAIL  " + n); } };
const near = (a, b, e = 1e-6) => Math.abs(a - b) < e;
const S = (kind, xs, ys, extra = {}) => ({ id: "s", color: "#000", visible: true, kind, xs, ys, ...extra });

// --- seriesBounds ----------------------------------------------------------
check("bounds over a line", (() => {
  const b = seriesBounds([S("line", [-1, 0, 2], [3, 1, 5])]);
  return b && b.xmin === -1 && b.xmax === 2 && b.ymin === 1 && b.ymax === 5;
})());
check("skips nulls / non-finite", (() => {
  const b = seriesBounds([S("line", [0, null, Infinity, 4], [0, 2, 9, 1])]);
  return b && b.xmin === 0 && b.xmax === 4 && b.ymin === 0 && b.ymax === 1;
})());
check("unions multiple visible series", (() => {
  const b = seriesBounds([S("points", [1], [1]), S("line", [-3, 5], [-2, 8])]);
  return b && b.xmin === -3 && b.xmax === 5 && b.ymin === -2 && b.ymax === 8;
})());
check("ignores hidden series", (() => {
  const b = seriesBounds([S("line", [0, 1], [0, 1], { visible: false }), S("points", [9], [9])]);
  return b && b.xmin === 9 && b.xmax === 9;
})());
check("ignores region/field fills", seriesBounds([S("region", [0], [0]), S("field", [1], [1])]) === null);
check("null when nothing plottable", seriesBounds([S("line", [null], [null])]) === null);

// --- fitView ---------------------------------------------------------------
const W = 800, H = 600;
check("frames the extent centered", (() => {
  const v = fitView({ xmin: -2, xmax: 6, ymin: 0, ymax: 4 }, W, H);
  return near(v.cx, 2) && near(v.cy, 2);
})());
check("keeps units square (single scale)", (() => {
  const v = fitView({ xmin: 0, xmax: 10, ymin: 0, ymax: 10 }, W, H);
  // wide-ish data in a 800x600 canvas → limited by height (600) here since dy=dx=10
  return typeof v.scale === "number" && v.scale > 0;
})());
check("the extent fits inside the canvas with margin", (() => {
  const b = { xmin: -5, xmax: 5, ymin: -1, ymax: 1 };
  const v = fitView(b, W, H, 0.1);
  const [xl, xh] = xRange(v, W);
  const [yl, yh] = yRange(v, H);
  // data range sits strictly within the visible range (margin present)...
  const inside = xl < b.xmin && xh > b.xmax && yl < b.ymin && yh > b.ymax;
  // ...and one axis is snug against the margin (the limiting one).
  const spanX = xh - xl, spanY = yh - yl;
  const snug = near((b.xmax - b.xmin) / spanX, 0.8, 1e-3) || near((b.ymax - b.ymin) / spanY, 0.8, 1e-3);
  return inside && snug;
})());
check("single point → a finite window centered on it", (() => {
  const v = fitView({ xmin: 3, xmax: 3, ymin: -2, ymax: -2 }, W, H);
  return near(v.cx, 3) && near(v.cy, -2) && Number.isFinite(v.scale) && v.scale > 0;
})());
check("horizontal extent (dy=0) stays square, finite scale", (() => {
  const v = fitView({ xmin: 0, xmax: 8, ymin: 5, ymax: 5 }, W, H);
  return near(v.cx, 4) && near(v.cy, 5) && Number.isFinite(v.scale) && v.scale <= MAX_SCALE;
})());
check("scale is clamped to the allowed range", (() => {
  const tiny = fitView({ xmin: 0, xmax: 1e12, ymin: 0, ymax: 1e12 }, W, H);
  const huge = fitView({ xmin: 0, xmax: 1e-12, ymin: 0, ymax: 1e-12 }, W, H);
  return tiny.scale >= MIN_SCALE && huge.scale <= MAX_SCALE;
})());

console.log(`\n${fail === 0 ? "ALL PASS" : "FAILURES"}: ${pass} passed, ${fail} failed`);
process.exit(fail === 0 ? 0 : 1);
