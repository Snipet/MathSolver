// Unit tests for the graph viewport math (web/src/lib/graph/viewport.ts).
//   node tools/graph_viewport_test.mjs
import {
  xToPx,
  yToPx,
  pxToX,
  pxToY,
  xRange,
  yRange,
  panned,
  zoomedAt,
  niceStep,
  ticks,
  MAX_SCALE,
} from "../web/src/lib/graph/viewport.ts";

let pass = 0;
let fail = 0;
function check(name, cond, extra = "") {
  if (cond) {
    pass++;
    console.log(`PASS  ${name}`);
  } else {
    fail++;
    console.log(`FAIL  ${name}${extra ? `  [${extra}]` : ""}`);
  }
}
const near = (a, b, eps = 1e-6) => Math.abs(a - b) < eps;

const W = 800;
const H = 500;
const v = { cx: 1.5, cy: -2, scale: 40 };

// --- transforms round-trip -------------------------------------------------
{
  let ok = true;
  for (const x of [-10, -1.5, 0, 1.5, 7.3]) ok &&= near(pxToX(xToPx(x, v, W), v, W), x);
  for (const y of [-10, -2, 0, 3.3, 9]) ok &&= near(pxToY(yToPx(y, v, H), v, H), y);
  check("world↔screen round-trips", ok);
}

// --- center maps to canvas center ------------------------------------------
check("center maps to canvas center", near(xToPx(v.cx, v, W), W / 2) && near(yToPx(v.cy, v, H), H / 2));

// --- y axis points up ------------------------------------------------------
check("increasing world-y moves up (smaller py)", yToPx(5, v, H) < yToPx(4, v, H));

// --- visible ranges --------------------------------------------------------
{
  const [x0, x1] = xRange(v, W);
  const [y0, y1] = yRange(v, H);
  check(
    "visible range width = w/scale, centered on c",
    near(x1 - x0, W / v.scale) && near((x0 + x1) / 2, v.cx) && near((y0 + y1) / 2, v.cy),
    `x=[${x0},${x1}]`,
  );
}

// --- pan -------------------------------------------------------------------
{
  const p = panned(v, 80, -40); // drag right 80px, up 40px
  check(
    "pan shifts center opposite the drag, in world units",
    near(p.cx, v.cx - 80 / v.scale) && near(p.cy, v.cy + -40 / v.scale) && p.scale === v.scale,
    `cx=${p.cx}`,
  );
}

// --- zoom keeps the cursor's world point fixed -----------------------------
{
  const px = 620;
  const py = 130;
  const wx = pxToX(px, v, W);
  const wy = pxToY(py, v, H);
  for (const f of [1.25, 0.8, 2, 0.5]) {
    const z = zoomedAt(v, f, px, py, W, H);
    const okScale = near(z.scale, v.scale * f);
    const stayX = near(xToPx(wx, z, W), px, 1e-4);
    const stayY = near(yToPx(wy, z, H), py, 1e-4);
    check(
      `zoom ×${f} keeps the point under the cursor fixed`,
      okScale && stayX && stayY,
      `px=${xToPx(wx, z, W).toFixed(3)} py=${yToPx(wy, z, H).toFixed(3)}`,
    );
  }
}

// --- zoom clamps scale -----------------------------------------------------
{
  let z = { cx: 0, cy: 0, scale: MAX_SCALE };
  z = zoomedAt(z, 100, W / 2, H / 2, W, H);
  check("zoom clamps scale at MAX_SCALE", z.scale <= MAX_SCALE + 1);
}

// --- nice steps ------------------------------------------------------------
check("niceStep picks 1/2/5·10^k", niceStep(10, 5) === 2 && niceStep(1, 5) === 0.2 && niceStep(100, 5) === 20);

// --- ticks -----------------------------------------------------------------
{
  const t = ticks(-1, 1, 0.5);
  check("ticks cover the range at the step", t.length === 5 && near(t[0], -1) && near(t[4], 1), JSON.stringify(t));
  check("ticks guards against explosive counts", ticks(-1e12, 1e12, 1e-6).length === 0);
}

console.log(`\n${fail === 0 ? "ALL PASS" : "FAILURES"}: ${pass} passed, ${fail} failed`);
process.exit(fail === 0 ? 0 : 1);
