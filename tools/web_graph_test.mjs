// Browser verification of the Desmos-style graphing calculator (top-level
// "Graph" mode). Requires `npm run build` in web/ first.
//   node tools/web_graph_test.mjs
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";
import { spawn } from "node:child_process";
import net from "node:net";
import { encodeState, decodeState } from "../web/src/lib/graph/share.ts";

const WEB = fileURLToPath(new URL("../web", import.meta.url));
const require = createRequire(`${WEB}/package.json`);
const puppeteer = require("puppeteer-core");
const CHROME =
  process.env.CHROME ?? "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome";

const freePort = () =>
  new Promise((res, rej) => {
    const s = net.createServer();
    s.listen(0, "127.0.0.1", () => {
      const p = s.address().port;
      s.close(() => res(p));
    });
    s.on("error", rej);
  });

let pass = 0;
let fail = 0;
const check = (name, cond, extra = "") => {
  if (cond) {
    pass++;
    console.log(`PASS  ${name}`);
  } else {
    fail++;
    console.log(`FAIL  ${name}${extra ? `  [${extra}]` : ""}`);
  }
};

const port = await freePort();
const server = spawn(
  process.execPath,
  [`${WEB}/node_modules/vite/bin/vite.js`, "preview", "--host", "127.0.0.1", "--port", String(port), "--strictPort"],
  { cwd: WEB, stdio: "ignore" },
);
const url = `http://127.0.0.1:${port}/`;
for (let i = 0; i < 80; i++) {
  try {
    const r = await fetch(url);
    if (r.ok) break;
  } catch {}
  await new Promise((r) => setTimeout(r, 250));
}

const browser = await puppeteer.launch({
  executablePath: CHROME,
  headless: true,
  args: ["--no-sandbox", "--disable-gpu"],
  defaultViewport: { width: 1400, height: 950 },
});
const page = await browser.newPage();
const pageErrors = [];
const consoleErrors = [];
page.on("pageerror", (e) => pageErrors.push(String(e)));
page.on("console", (m) => {
  if (m.type() === "error") consoleErrors.push(m.text());
});

try {
  await page.goto(url, { waitUntil: "load" });
  await page.waitForFunction(
    () => document.querySelector(".version-chip")?.textContent?.match(/\d+\.\d+\.\d+/),
    { timeout: 30000 },
  );
  await page.evaluate(() => {
    [...document.querySelectorAll('.mode-switch [role="tab"]')].find((b) => b.textContent.trim() === "Graph")?.click();
  });
  await page.waitForSelector(".calc .graph canvas", { timeout: 8000 });
  check("Graph mode renders the graphing calculator", true);

  const typeRow = async (i, text) => {
    const inputs = await page.$$(".calc .expr");
    await inputs[i].click({ clickCount: 3 });
    await inputs[i].type(text);
  };

  // A function with a free variable → auto-slider + session variable.
  await typeRow(0, "a*sin(x)");
  await page.waitForSelector(".calc .sliders .slot", { timeout: 6000 });
  check("free variable auto-creates a slider", true);
  const varName = await page.$eval(".calc .sliders .slot-name", (el) => el.textContent.trim());
  check("slider is labeled with the variable", varName === "a", varName);
  const inPanel = await page.evaluate(() =>
    [...document.querySelectorAll(".sidebar [data-testid='vars-panel'] li, .sidebar li[data-var-id]")].some(
      (li) => li.textContent.includes("a"),
    ),
  );
  check("auto-variable appears in the session Variables panel (app-integrated)", inPanel);

  // Add a second expression.
  await page.click(".calc .add-row");
  await typeRow(1, "x^2/4 - 3");
  await new Promise((r) => setTimeout(r, 700));
  const rowCount = await page.$$eval(".calc .rows .row", (els) => els.length);
  check("add-expression grows the list", rowCount === 2, `rows=${rowCount}`);

  // Drag the slider → session variable follows.
  await page.$eval(".calc .sliders input[type=range]", (el) => {
    el.value = "3";
    el.dispatchEvent(new Event("input", { bubbles: true }));
    el.dispatchEvent(new Event("change", { bubbles: true }));
  });
  await page.waitForFunction(
    () => {
      const rows = [...document.querySelectorAll(".sidebar li[data-var-id]")];
      const r = rows.find((li) => li.querySelector("input.name")?.value === "a");
      return r?.querySelector("input.value")?.value === "3";
    },
    { timeout: 6000 },
  );
  check("slider drag writes through to the session variable", true);

  // Pan + zoom must not throw.
  const box = await page.$eval(".calc .graph canvas", (el) => {
    const r = el.getBoundingClientRect();
    return { x: r.x, y: r.y, w: r.width, h: r.height };
  });
  await page.mouse.move(box.x + box.w * 0.5, box.y + box.h * 0.5);
  await page.mouse.down();
  await page.mouse.move(box.x + box.w * 0.6, box.y + box.h * 0.55, { steps: 5 });
  await page.mouse.up();
  await page.evaluate(
    (b) => document.querySelector(".calc .graph canvas").dispatchEvent(
      new WheelEvent("wheel", { deltaY: -120, clientX: b.x + b.w / 2, clientY: b.y + b.h / 2, bubbles: true }),
    ),
    box,
  );
  await new Promise((r) => setTimeout(r, 300));
  check("pan and zoom do not throw", true);

  // Delete a row.
  await page.hover(".calc .rows .row");
  await page.click(".calc .rows .row .row-del");
  await new Promise((r) => setTimeout(r, 300));
  const afterDel = await page.$$eval(".calc .rows .row", (els) => els.length);
  check("delete removes a row", afterDel === 1, `rows=${afterDel}`);

  // The full Desmos surface: implicit, inequality, polar, parametric all plot
  // without a per-row error.
  const surface = [
    "x^2 + y^2 = 9", // implicit curve
    "y <= x^2 - 4", // inequality region
    "r = 2*sin(2*theta)", // polar rose
    "(3*cos(t), 3*sin(t))", // parametric circle
  ];
  // Reset to a single fresh row.
  await page.evaluate(() => {
    try {
      localStorage.removeItem("mathsolver.graph");
    } catch {}
  });
  await page.reload({ waitUntil: "networkidle0" });
  await page.evaluate(() => {
    [...document.querySelectorAll('.mode-switch [role="tab"]')].find((b) => b.textContent.trim() === "Graph")?.click();
  });
  await page.waitForSelector(".calc .graph canvas", { timeout: 8000 });
  for (let i = 0; i < surface.length; i++) {
    if (i > 0) await page.click(".calc .add-row");
    const inputs = await page.$$(".calc .expr");
    await inputs[i].click({ clickCount: 3 });
    await inputs[i].type(surface[i]);
  }
  await new Promise((r) => setTimeout(r, 1500));
  const rowErrs = await page.$$eval(".calc .row-err", (els) => els.map((e) => e.textContent));
  check(
    "implicit, inequality, polar & parametric all plot without error",
    rowErrs.length === 0,
    rowErrs.join(" | "),
  );

  // Named definitions + calculus operators: `f = x^2` becomes a session
  // variable; diff(f) and integral(f) plot its derivative / antiderivative.
  await page.evaluate(() => {
    try {
      localStorage.removeItem("mathsolver.graph");
    } catch {}
  });
  await page.reload({ waitUntil: "networkidle0" });
  await page.evaluate(() => {
    [...document.querySelectorAll('.mode-switch [role="tab"]')].find((b) => b.textContent.trim() === "Graph")?.click();
  });
  await page.waitForSelector(".calc .graph canvas", { timeout: 8000 });
  const calc = ["f = x^2", "diff(f)", "integral(f)"];
  for (let i = 0; i < calc.length; i++) {
    if (i > 0) await page.click(".calc .add-row");
    const inputs = await page.$$(".calc .expr");
    await inputs[i].click({ clickCount: 3 });
    await inputs[i].type(calc[i]);
  }
  await new Promise((r) => setTimeout(r, 1800));
  const calcErrs = await page.$$eval(".calc .row-err", (els) => els.map((e) => e.textContent));
  check("definition + diff/integral plot without error", calcErrs.length === 0, calcErrs.join(" | "));
  const fDefined = await page.evaluate(() =>
    [...document.querySelectorAll(".sidebar li[data-var-id] input.name")].some((el) => el.value === "f"),
  );
  check("`f = x^2` registers as a session variable", fDefined);
  // The definition row itself must not spawn a slider (x is the graph axis).
  const sliderNames = await page.$$eval(".calc .sliders .slot-name", (els) =>
    els.map((e) => e.textContent.trim()),
  );
  check("definition does not create a stray slider", !sliderNames.includes("f") && !sliderNames.includes("x"), sliderNames.join(","));

  // Domain restrictions: a clipped function, an extended-domain spiral, and a
  // parametric arc — all plot without error.
  await page.evaluate(() => {
    try {
      localStorage.removeItem("mathsolver.graph");
    } catch {}
  });
  await page.reload({ waitUntil: "networkidle0" });
  await page.evaluate(() => {
    [...document.querySelectorAll('.mode-switch [role="tab"]')].find((b) => b.textContent.trim() === "Graph")?.click();
  });
  await page.waitForSelector(".calc .graph canvas", { timeout: 8000 });
  const restricted = [
    "x {0 < x < 5}", // a line segment
    "r = theta {0 <= theta <= 6*pi}", // 3-turn spiral (domain past 2pi)
    "(cos(t), sin(t)) {0 <= t <= pi}", // upper semicircle
  ];
  for (let i = 0; i < restricted.length; i++) {
    if (i > 0) await page.click(".calc .add-row");
    const inputs = await page.$$(".calc .expr");
    await inputs[i].click({ clickCount: 3 });
    await inputs[i].type(restricted[i]);
  }
  await new Promise((r) => setTimeout(r, 1800));
  const restrErrs = await page.$$eval(".calc .row-err", (els) => els.map((e) => e.textContent));
  check("domain restrictions {…} plot without error", restrErrs.length === 0, restrErrs.join(" | "));
  // The restriction must not spawn a slider for the sample var / output var.
  const rSliders = await page.$$eval(".calc .sliders .slot-name", (els) => els.map((e) => e.textContent.trim()));
  check("restriction does not create x/y/theta/t sliders", !["x", "y", "theta", "t"].some((n) => rSliders.includes(n)), rSliders.join(","));

  // --- draggable points ------------------------------------------------------
  const clickGraph = async () => {
    await page.evaluate(() => {
      [...document.querySelectorAll('.mode-switch [role="tab"]')].find((b) => b.textContent.trim() === "Graph")?.click();
    });
    await page.waitForSelector(".calc .graph canvas", { timeout: 8000 });
  };
  // world (wx,wy) → screen px for a canvas with a known view {cx:0,cy:0,scale}
  const worldToScreen = (box, wx, wy, scale) => ({
    x: box.x + box.w / 2 + wx * scale,
    y: box.y + box.h / 2 - wy * scale,
  });
  const canvasBox = () =>
    page.$eval(".calc .graph canvas", (el) => {
      const r = el.getBoundingClientRect();
      return { x: r.x, y: r.y, w: r.width, h: r.height };
    });
  const varVal = (name) =>
    page.evaluate((n) => {
      const li = [...document.querySelectorAll(".sidebar li[data-var-id]")].find(
        (l) => l.querySelector("input.name")?.value === n,
      );
      return li?.querySelector("input.value")?.value ?? null;
    }, name);
  const dragOnCanvas = async (from, to) => {
    await page.mouse.move(from.x, from.y);
    await page.mouse.down();
    for (let s = 1; s <= 8; s++) {
      await page.mouse.move(from.x + ((to.x - from.x) * s) / 8, from.y + ((to.y - from.y) * s) / 8);
      await new Promise((r) => setTimeout(r, 20));
    }
    await page.mouse.up();
  };

  // Flagship: a variable-backed point (a, b) — dragging writes the session vars.
  await page.evaluate(() => {
    localStorage.clear();
    localStorage.setItem(
      "mathsolver.graph",
      JSON.stringify({ rows: [{ text: "(a, b)", color: "#2563eb", visible: true }], view: { cx: 0, cy: 0, scale: 40 } }),
    );
  });
  await page.reload({ waitUntil: "networkidle0" });
  await clickGraph();
  await page.waitForFunction(
    () => document.querySelectorAll(".calc .sliders .slot").length >= 2,
    { timeout: 6000 },
  );
  {
    const box = await canvasBox();
    const from = worldToScreen(box, 1, 1, 40); // (a,b) start at 1,1
    const to = worldToScreen(box, 3, 2, 40);
    await dragOnCanvas(from, to);
    await page.waitForFunction(
      () => {
        const li = [...document.querySelectorAll(".sidebar li[data-var-id]")].find((l) => l.querySelector("input.name")?.value === "a");
        return Math.abs(Number(li?.querySelector("input.value")?.value) - 3) < 0.3;
      },
      { timeout: 6000 },
    ).catch(() => {});
    const a = Number(await varVal("a"));
    const b = Number(await varVal("b"));
    check("dragging (a,b) moves the session variables app-wide", Math.abs(a - 3) < 0.35 && Math.abs(b - 2) < 0.35, `a=${a} b=${b}`);

    // Release parity: set a := 7 externally; the graph must honor it (override
    // released, not pinned) — the slider slot value should follow to 7.
    await page.evaluate(() => {
      const li = [...document.querySelectorAll(".sidebar li[data-var-id]")].find((l) => l.querySelector("input.name")?.value === "a");
      const inp = li?.querySelector("input.value");
      if (inp) {
        inp.value = "7";
        inp.dispatchEvent(new Event("input", { bubbles: true }));
        inp.dispatchEvent(new Event("change", { bubbles: true }));
      }
    });
    const readSlotA = () =>
      page.evaluate(() => {
        const s = [...document.querySelectorAll(".calc .sliders .slot")].find((el) => el.querySelector(".slot-name")?.textContent.trim() === "a");
        return s?.querySelector(".slot-val")?.value ?? null;
      });
    let slotA = false;
    try {
      await page.waitForFunction(
        () => {
          const s = [...document.querySelectorAll(".calc .sliders .slot")].find((el) => el.querySelector(".slot-name")?.textContent.trim() === "a");
          const v = Number(s?.querySelector(".slot-val")?.value);
          return Math.abs(v - 7) < 0.01;
        },
        { timeout: 6000 },
      );
      slotA = true;
    } catch {
      slotA = false;
    }
    check("drag override is released, not pinned (external a:=7 honored)", slotA, `slotA=${await readSlotA()}, panelA=${await varVal("a")}`);
  }

  // Literal point: dragging rewrites the row text.
  await page.evaluate(() => {
    localStorage.clear();
    localStorage.setItem(
      "mathsolver.graph",
      JSON.stringify({ rows: [{ text: "(1, 2)", color: "#dc2626", visible: true }], view: { cx: 0, cy: 0, scale: 40 } }),
    );
  });
  await page.reload({ waitUntil: "networkidle0" });
  await clickGraph();
  await new Promise((r) => setTimeout(r, 600));
  {
    const box = await canvasBox();
    const from = worldToScreen(box, 1, 2, 40);
    const to = worldToScreen(box, -2, 4, 40);
    await dragOnCanvas(from, to);
    await new Promise((r) => setTimeout(r, 400));
    const text = await page.$eval(".calc .expr", (el) => el.value);
    const m = /\(\s*(-?\d+\.?\d*)\s*,\s*(-?\d+\.?\d*)\s*\)/.exec(text);
    const okLit = m && Math.abs(Number(m[1]) + 2) < 0.35 && Math.abs(Number(m[2]) - 4) < 0.35;
    check("dragging a literal point rewrites the row text", !!okLit, text);
  }

  // Locked point: both coords are expressions → grab falls through to pan.
  await page.evaluate(() => {
    localStorage.clear();
    localStorage.setItem(
      "mathsolver.graph",
      JSON.stringify({ rows: [{ text: "(a+1, b+1)", color: "#16a34a", visible: true }], view: { cx: 0, cy: 0, scale: 40 } }),
    );
  });
  await page.reload({ waitUntil: "networkidle0" });
  await clickGraph();
  await new Promise((r) => setTimeout(r, 700));
  {
    const before = await page.$eval(".calc .expr", (el) => el.value);
    const box = await canvasBox();
    // (a+1, b+1) with a=b=1 → point at (2,2)
    const from = worldToScreen(box, 2, 2, 40);
    const to = { x: from.x + 120, y: from.y + 40 };
    await dragOnCanvas(from, to);
    await new Promise((r) => setTimeout(r, 300));
    const after = await page.$eval(".calc .expr", (el) => el.value);
    check("locked point (expr coords) is not rewritten — drag pans instead", before === after, `${before} -> ${after}`);
  }

  // Same-variable point (a, a): one DOF — drag projects onto y=x, single write.
  await page.evaluate(() => {
    localStorage.clear();
    localStorage.setItem(
      "mathsolver.graph",
      JSON.stringify({ rows: [{ text: "(a, a)", color: "#9333ea", visible: true }], view: { cx: 0, cy: 0, scale: 40 } }),
    );
  });
  await page.reload({ waitUntil: "networkidle0" });
  await clickGraph();
  await page.waitForFunction(() => document.querySelectorAll(".calc .sliders .slot").length >= 1, { timeout: 6000 });
  {
    const box = await canvasBox();
    const from = worldToScreen(box, 1, 1, 40); // a=1 → (1,1)
    const to = worldToScreen(box, 4, 2, 40); // projects to a=(4+2)/2=3
    await dragOnCanvas(from, to);
    await new Promise((r) => setTimeout(r, 500));
    const a = Number(await varVal("a"));
    check("same-variable point (a,a) drags along y=x (single value)", Math.abs(a - 3) < 0.4, `a=${a}`);
  }

  // Keyboard navigation: focusing the graph paper lets arrows pan, +/= zoom,
  // and 0 reset the viewport — persisted to localStorage like mouse pan/zoom.
  await page.evaluate(() => {
    localStorage.clear();
    localStorage.setItem(
      "mathsolver.graph",
      JSON.stringify({ rows: [{ text: "y=x", color: "#2563eb", visible: true }], view: { cx: 0, cy: 0, scale: 40 } }),
    );
  });
  await page.reload({ waitUntil: "networkidle0" });
  await clickGraph();
  await new Promise((r) => setTimeout(r, 400));
  {
    const readView = () =>
      page.evaluate(() => JSON.parse(localStorage.getItem("mathsolver.graph")).view);
    await page.$eval(".calc .graph canvas", (el) => el.focus());

    await page.keyboard.press("ArrowRight");
    await new Promise((r) => setTimeout(r, 350));
    let v = await readView();
    check("ArrowRight pans the viewport +x", v.cx > 0 && Math.abs(v.cy) < 1e-9, JSON.stringify(v));

    await page.keyboard.press("ArrowUp");
    await new Promise((r) => setTimeout(r, 350));
    v = await readView();
    check("ArrowUp pans the viewport +y", v.cy > 0, JSON.stringify(v));

    await page.keyboard.press("=");
    await new Promise((r) => setTimeout(r, 350));
    v = await readView();
    check("'=' zooms in (scale increases)", v.scale > 40, JSON.stringify(v));

    await page.keyboard.press("0");
    await new Promise((r) => setTimeout(r, 350));
    v = await readView();
    check("'0' resets the view to default", v.cx === 0 && v.cy === 0 && v.scale === 40, JSON.stringify(v));
  }

  // Share link: a #g=… hash restores the graph rows, view, and variables and
  // opens Graph mode, then clears the hash.
  {
    const shared = encodeState({
      v: 1,
      rows: [
        { text: "a*sin(x)", color: "#2563eb", visible: true },
        { text: "(a, 2)", color: "#dc2626", visible: true },
      ],
      view: { cx: 0, cy: 0, scale: 50 },
      vars: [{ name: "a", value: "4" }],
    });
    await page.evaluate(() => localStorage.clear());
    await page.goto("about:blank"); // force a real document load for the hash URL
    await page.goto(`${url}#g=${shared}`, { waitUntil: "networkidle0" });
    await page.waitForSelector(".calc .graph canvas", { timeout: 8000 });
    const rowsText = await page.$$eval(".calc .expr", (els) => els.map((e) => e.value));
    check(
      "shared link restores the graph rows and opens Graph mode",
      rowsText.includes("a*sin(x)") && rowsText.some((t) => t.replace(/\s/g, "") === "(a,2)"),
      rowsText.join(" | "),
    );
    const aVal = await page.waitForFunction(
      () => {
        const li = [...document.querySelectorAll(".sidebar li[data-var-id]")].find((l) => l.querySelector("input.name")?.value === "a");
        const v = li?.querySelector("input.value")?.value;
        return Number(v) === 4 ? v : false;
      },
      { timeout: 6000 },
    ).then(() => true).catch(() => false);
    check("shared link restores variables (a=4)", aVal, `a=${await varVal("a")}`);
    const hash = await page.evaluate(() => location.hash);
    check("shared link hash is cleared after import", hash === "", hash);
  }

  // Share button gathers the graph's variables at their resolved values.
  {
    await page.evaluate(() => {
      localStorage.clear();
      localStorage.setItem(
        "mathsolver.graph",
        JSON.stringify({ rows: [{ text: "a*sin(x)", color: "#2563eb", visible: true }], view: { cx: 0, cy: 0, scale: 40 } }),
      );
      localStorage.setItem("mathsolver.mode", "graph");
    });
    await page.goto("about:blank");
    await page.goto(url, { waitUntil: "networkidle0" });
    await page.waitForSelector(".calc .graph canvas", { timeout: 8000 });
    await page.waitForFunction(() => document.querySelectorAll(".calc .sliders .slot").length >= 1, { timeout: 6000 });
    await page.evaluate(() => {
      window.__shared = null;
      Object.defineProperty(navigator, "clipboard", {
        value: { writeText: (t) => { window.__shared = t; return Promise.resolve(); } },
        configurable: true,
      });
      [...document.querySelectorAll(".calc-toolbar .tool-btn")].find((b) => b.textContent.trim() === "Share")?.click();
    });
    const sharedUrl = await page.waitForFunction(() => window.__shared, { timeout: 6000 }).then((h) => h.jsonValue()).catch(() => null);
    const payload = sharedUrl && sharedUrl.split("#g=")[1];
    const decoded = payload ? decodeState(payload) : null;
    check(
      "Share encodes the graph's variables at their resolved values",
      !!decoded && decoded.vars.some((v) => v.name === "a" && Number(v.value) === 1),
      JSON.stringify(decoded?.vars),
    );
  }

  // Continuous curve trace: hovering along a y=f(x) curve follows it (the new
  // interpolate-at-x path) and shows a coordinate readout, without throwing.
  await page.evaluate(() => {
    localStorage.clear();
    localStorage.setItem(
      "mathsolver.graph",
      JSON.stringify({ rows: [{ text: "y=x^2", color: "#2563eb", visible: true }], view: { cx: 0, cy: 0, scale: 40 } }),
    );
    localStorage.setItem("mathsolver.mode", "graph");
  });
  await page.goto("about:blank");
  await page.goto(url, { waitUntil: "networkidle0" });
  await page.waitForSelector(".calc .graph canvas", { timeout: 8000 });
  await new Promise((r) => setTimeout(r, 400));
  {
    const box = await page.$eval(".calc .graph canvas", (el) => {
      const r = el.getBoundingClientRect();
      return { x: r.x, y: r.y, w: r.width, h: r.height };
    });
    // Sweep the cursor along the parabola: at world x the curve sits at y=x^2,
    // which near the center is close to the horizontal midline, so a few points
    // just above center should attach the trace. Just assert it never throws.
    let readoutSeen = false;
    for (const frac of [0.42, 0.5, 0.58]) {
      await page.mouse.move(box.x + box.w * frac, box.y + box.h * 0.5, { steps: 3 });
      await new Promise((r) => setTimeout(r, 60));
      readoutSeen ||= await page.$eval(".calc .graph", (el) => !!el.querySelector(".readout")).catch(() => false);
    }
    check("hovering a y=f(x) curve traces without error", true);
    check("a coordinate readout appears on hover", readoutSeen);
  }

  // Data table: column names are editable + persisted, and the copy button
  // exports tab-separated values (with the renamed header) to the clipboard.
  {
    await page.evaluate(() => {
      localStorage.clear();
      localStorage.setItem(
        "mathsolver.graph",
        JSON.stringify({
          rows: [{ kind: "table", color: "#2563eb", visible: true, fit: "", points: [{ x: "1", y: "2" }, { x: "3", y: "4" }] }],
          view: { cx: 0, cy: 0, scale: 40 },
        }),
      );
      localStorage.setItem("mathsolver.mode", "graph");
    });
    await page.goto("about:blank");
    await page.goto(url, { waitUntil: "networkidle0" });
    await page.waitForSelector(".calc .head-name", { timeout: 8000 });
    // Rename the x column to "time".
    await page.evaluate(() => {
      const inp = document.querySelector(".calc .head-name");
      inp.value = "time";
      inp.dispatchEvent(new Event("input", { bubbles: true }));
    });
    await new Promise((r) => setTimeout(r, 400)); // debounced persist
    const xName = await page.evaluate(
      () => JSON.parse(localStorage.getItem("mathsolver.graph")).rows[0].xName,
    );
    check("renaming a table column persists", xName === "time", String(xName));

    // Copy → TSV with the renamed header on the clipboard.
    const tsv = await page.evaluate(async () => {
      window.__copied = null;
      Object.defineProperty(navigator, "clipboard", {
        value: { writeText: (t) => { window.__copied = t; return Promise.resolve(); } },
        configurable: true,
      });
      document.querySelector(".calc .copy").click();
      await new Promise((r) => setTimeout(r, 50));
      return window.__copied;
    });
    check("copy exports TSV with the renamed header", tsv === "time\ty\n1\t2\n3\t4", JSON.stringify(tsv));
  }

  check("no page errors", pageErrors.length === 0, pageErrors.join(" | "));
  check("no console errors", consoleErrors.length === 0, consoleErrors.join(" | "));
} catch (e) {
  console.error("FATAL", e);
  fail++;
} finally {
  await browser.close();
  server.kill();
}

console.log(`\n${fail === 0 ? "ALL PASS" : "FAILURES"}: ${pass} passed, ${fail} failed`);
process.exit(fail === 0 ? 0 : 1);
