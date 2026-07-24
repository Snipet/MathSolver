// Browser verification of the line-by-line Console (App.svelte "console" mode):
// the programmatic command grammar (bare expr/equation, solve/diff/integrate/
// eval/factor/system), `:=` assignments feeding the shared environment on later
// lines, the session commands (vars/help), error cards, and cross-reload
// persistence. Requires `npm run build` in web/ first; drives the built app in
// system Chrome via web/'s puppeteer-core devDependency.
//
//   node tools/web_console_test.mjs
//   CHROME=/usr/bin/chromium node tools/web_console_test.mjs
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";
import { spawn } from "node:child_process";
import net from "node:net";

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

let passCount = 0;
let failCount = 0;
function check(name, cond, extra = "") {
  if (cond) {
    passCount++;
    console.log(`PASS  ${name}`);
  } else {
    failCount++;
    console.log(`FAIL  ${name}${extra ? `  [${extra}]` : ""}`);
  }
}

const port = await freePort();
const server = spawn(
  process.execPath,
  [
    `${WEB}/node_modules/vite/bin/vite.js`,
    "preview",
    "--host",
    "127.0.0.1",
    "--port",
    String(port),
    "--strictPort",
  ],
  { cwd: WEB, stdio: ["ignore", "pipe", "pipe"] },
);
server.stderr.on("data", (d) => process.stderr.write(`[vite] ${d}`));
const url = `http://127.0.0.1:${port}/`;
let up = false;
for (let i = 0; i < 80; i++) {
  try {
    const r = await fetch(url);
    if (r.ok) {
      up = true;
      break;
    }
  } catch {}
  await new Promise((r) => setTimeout(r, 250));
}
if (!up) {
  console.error("preview server failed to start");
  process.exit(2);
}

const browser = await puppeteer.launch({
  executablePath: CHROME,
  headless: true,
  args: ["--no-sandbox", "--disable-gpu", "--window-size=1400,1000"],
  defaultViewport: { width: 1400, height: 1000 },
});
const page = await browser.newPage();
const pageErrors = [];
const consoleErrors = [];
page.on("pageerror", (e) => pageErrors.push(String(e)));
page.on("console", (m) => {
  if (m.type() === "error") consoleErrors.push(m.text());
});

const TA = ".console .prompt textarea";

async function run(cmd) {
  const before = await page.$$eval(".cells .cell", (els) => els.length);
  await page.click(TA);
  await page.type(TA, cmd);
  await page.keyboard.press("Enter");
  await page.waitForFunction(
    (n) => {
      const cells = document.querySelectorAll(".cells .cell");
      if (cells.length !== n + 1) return false;
      const last = cells[cells.length - 1];
      return last && !(last.textContent || "").includes("computing…");
    },
    { timeout: 20000 },
    before,
  );
  // textContent, not innerText: the Plain/LaTeX source rows live inside a
  // collapsed <details> and must stay assertable without expanding it.
  return page.$eval(".cells .cell:last-child", (el) => el.textContent);
}

try {
  await page.goto(url, { waitUntil: "load" });
  await page.waitForFunction(
    () => {
      const el = document.querySelector(".version-chip");
      return el && /\d+\.\d+\.\d+/.test(el.textContent || "");
    },
    { timeout: 30000 },
  );

  // Switch to Console mode.
  await page.evaluate(() => {
    const btn = [...document.querySelectorAll(".mode-switch button")].find(
      (b) => b.textContent.trim() === "Console",
    );
    btn.click();
  });
  await page.waitForSelector(TA, { timeout: 5000 });
  check("console renders", true);

  check("bare expression simplifies", (await run("2x + 3x")).includes("5*x"));
  check("derivative verb", (await run("diff sin(x^2), x")).includes("2*x*cos(x^2)"));
  check(
    "indefinite integral verb",
    (await run("integrate x*sin(x), x")).includes("-x*cos(x)"),
  );

  // New cell chrome: the typeset input line and the collapsed source rows.
  check(
    "cell typesets its input (∫ … dx)",
    await page
      .$eval(
        ".cells .cell:last-child [data-testid='cell-input-typeset'] annotation",
        (el) => /\\int/.test(el.textContent || ""),
      )
      .catch(() => false),
  );
  const srcState = await page
    .$eval(".cells .cell:last-child details.sources", (d) => ({
      open: d.open,
      fields: d.querySelectorAll(".copy-field").length,
    }))
    .catch(() => null);
  check(
    "source rows collapsed by default",
    !!srcState && !srcState.open && srcState.fields === 2,
    JSON.stringify(srcState),
  );
  await page.click(".cells .cell:last-child details.sources summary");
  check(
    "source rows expand on click",
    await page
      .$eval(
        ".cells .cell:last-child details.sources",
        (d) => d.open && d.querySelectorAll(".copy-field").length === 2,
      )
      .catch(() => false),
  );
  check(
    "apart verb",
    (await run("apart (3x+2)/((x+1)(x+2))")).includes("4/(x + 2)"),
  );

  // steps verb: a worked, rule-by-rule derivative renders as an ordered list of
  // rule-tagged steps (innermost-first), closing on the answer.
  const stepsOut = await run("steps sin(x^2), x");
  check(
    "steps verb shows the final derivative",
    stepsOut.includes("2*x*cos(x^2)"),
    stepsOut.replace(/\n/g, " ").slice(0, 80),
  );
  const stepsUi = await page.$eval(".cells .cell:last-child", (el) => ({
    items: el.querySelectorAll("ol.steps li").length,
    rules: [...el.querySelectorAll("ol.steps .rule")].map((r) =>
      r.textContent.trim().toLowerCase(),
    ),
  }));
  check(
    "steps renders an ordered list with rule chips",
    stepsUi.items >= 2 &&
      stepsUi.rules.some((r) => r.includes("power")) &&
      stepsUi.rules.some((r) => r.includes("chain")),
    JSON.stringify(stepsUi),
  );
  // The variable can be inferred when omitted.
  check(
    "steps infers the variable",
    (await run("steps x^3")).includes("3*x^2"),
  );

  // `steps integrate` works the integral instead: structural steps (linearity,
  // constant multiple) with each leaf tagged by the technique the engine used.
  const istepsOut = await run("steps integrate x^2 + sin(x), x");
  check(
    "steps integrate shows the antiderivative with + C",
    istepsOut.includes("x^3/3 - cos(x) + C"),
    istepsOut.replace(/\n/g, " ").slice(0, 80),
  );
  const istepsUi = await page.$eval(".cells .cell:last-child", (el) => ({
    items: el.querySelectorAll("ol.steps li").length,
    rules: [...el.querySelectorAll("ol.steps .rule")].map((r) =>
      r.textContent.trim().toLowerCase(),
    ),
    label: el.querySelector(".answer-label")?.textContent.trim() ?? "",
  }));
  check(
    "steps integrate renders rule chips and an integral label",
    istepsUi.items >= 3 &&
      istepsUi.rules.includes("linearity") &&
      istepsUi.label.startsWith("∫"),
    JSON.stringify(istepsUi),
  );
  // A non-elementary integral is an answer, not an error (like `integrate`).
  check(
    "steps integrate is honest about no closed form",
    (await run("steps integrate e^(x^2), x")).includes("No closed form found"),
  );
  check(
    "laplace verb",
    (await run("laplace e^(-t) sin(2t)")).includes("(s + 1)^2 + 4"),
  );
  check(
    "ilaplace verb",
    (await run("ilaplace s/(s^2 + 9)")).includes("cos(3*t)"),
  );
  const dsolveOut = await run("dsolve y'' + 3y' + 2y = e^(-t), y(0)=1, y'(0)=0");
  check(
    "dsolve verb solves an IVP",
    dsolveOut.includes("t*e^(-t)") && dsolveOut.includes("Y(s)"),
    dsolveOut.replace(/\n/g, " ").slice(0, 80),
  );
  check(
    "series verb expands",
    (await run("series sin(x), x, 0, 5")).includes("x^5/120"),
  );
  check(
    "grad verb",
    (await run("grad x^2 + y^2, x, y")).includes("(2*x, 2*y)"),
  );
  check("limit verb exact", (await run("limit sin(x)/x, x, 0")).includes("1"));
  check(
    "limit verb divergence message",
    (await run("limit 1/x, x, 0, right")).includes("+∞"),
  );
  check(
    "sum verb Faulhaber",
    (await run("sum k^2, k, 1, n")).includes("n^3/3"),
  );
  check(
    "rsolve verb Binet",
    (await run("rsolve a(n+2) = a(n+1) + a(n), a(0)=0, a(1)=1")).includes(
      "sqrt(5)",
    ),
  );
  const laOut = await run("linalg.solve [2 1; 1 3], [3 5]");
  check(
    "linalg.solve renders",
    laOut.includes("(0.8, 1.4)"),
    laOut.replace(/\n/g, " ").slice(0, 80),
  );
  check(
    "linalg symbolic det",
    (await run("linalg.det [a b; c d]")).includes("a*d"),
  );
  const pdeOut = await run("pde.heat 1, 1, x*(1-x)");
  check(
    "pde.heat renders profiles",
    pdeOut.includes("heat equation") && pdeOut.includes("Temperature profiles"),
    pdeOut.replace(/\n/g, " ").slice(0, 80),
  );
  check(
    "series at infinity",
    (await run("series (x+1)/(x-1), x, inf, 3")).includes("2/x"),
  );
  const mlOut = await run("mlimit x*y/(x^2+y^2), x, 0, y, 0");
  check(
    "mlimit reports nonexistence with witnesses",
    mlOut.includes("does not exist") && mlOut.includes("diagonal"),
    mlOut.replace(/\n/g, " ").slice(0, 80),
  );
  const eigOut = await run("linalg.eig [2 1; 1 2]");
  check(
    "exact eigendecomposition renders",
    eigOut.includes("Characteristic polynomial") && eigOut.includes("(1, 1)"),
    eigOut.replace(/\n/g, " ").slice(0, 80),
  );
  check(
    "structured trisolve renders",
    (await run("linalg.trisolve [-1], [2 2], [-1], [1 1]")).includes("Thomas"),
  );
  check(
    "gamma folds exactly in console",
    (await run("simplify gamma(5) + gamma(1/2)")).includes("24"),
  );
  check(
    "gaussian integral gives erf",
    (await run("integrate e^(-x^2), x")).includes("erf"),
  );
  check(
    "binomial folds through gamma",
    (await run("binomial(10, 5)")).includes("252"),
  );
  const seqOut = await run("seq 0, 1, 1, 2, 3, 5, 8");
  check(
    "seq recognizes Fibonacci",
    seqOut.includes("Fibonacci") && seqOut.includes("13, 21, 34"),
    seqOut.replace(/\n/g, " ").slice(0, 80),
  );
  const stirOut = await run("stirling x, 3");
  check(
    "stirling series with notes",
    stirOut.includes("Gamma") && stirOut.includes("ln Gamma(10)"),
    stirOut.replace(/\n/g, " ").slice(0, 80),
  );
  const femOut = await run("fem.bvp 1, 0, pi^2*sin(pi*x), 0, 1, u=0, u=0");
  check(
    "fem.bvp renders with convergence order",
    femOut.includes("Observed convergence order") &&
      femOut.includes("Solution u(x)"),
    femOut.replace(/\n/g, " ").slice(0, 80),
  );
  check(
    "fem.modes renders eigenvalues",
    (await run("fem.modes 1, 0, 1, 0, pi")).includes("Eigenvalues"),
  );
  const simOut = await run("pde.simulate 10, 1, u*(1-u), 0.5*sin(pi*x/10), 8");
  check(
    "pde.simulate renders profiles",
    simOut.includes("reaction") && simOut.includes("Concentration profiles"),
    simOut.replace(/\n/g, " ").slice(0, 80),
  );
  const ieOut = await run("ie.fredholm x*t, x, 1, 0, 1");
  check(
    "ie.fredholm renders the solution",
    ieOut.includes("Fredholm integral equation") && ieOut.includes("1.5"),
    ieOut.replace(/\n/g, " ").slice(0, 80),
  );
  check(
    "ie.volterra renders",
    (await run("ie.volterra 1, 1, 1, 0, 1")).includes("trapezoidal marching"),
  );
  const hybOut = await run("hyb.sim v; -9.81, x, x; -0.8*v, 1, 0, 2");
  check(
    "hyb.sim renders events",
    hybOut.includes("hybrid simulation") && hybOut.includes("Events"),
    hybOut.replace(/\n/g, " ").slice(0, 80),
  );
  check(
    "hyb.sim Zeno note",
    (await run("hyb.sim v; -9.81, x, x; -0.8*v, 1, 0, 5")).includes("Zeno"),
  );
  const dsysOut = await run("dsolve x' = y; y' = -x, x(0)=1, y(0)=0");
  check(
    "dsolve system in console",
    dsysOut.includes("cos") && dsysOut.includes("sin"),
    dsysOut.replace(/\n/g, " ").slice(0, 80),
  );
  check(
    "curl verb",
    (await run("curl -y; x; 0, x, y, z")).includes("(0, 0, 2)"),
  );
  const vfOut = await run("vecfield -y; x");
  const vfCanvas = await page.$eval(
    ".cells .cell:last-child",
    (el) => el.querySelectorAll("svg").length,
  );
  check("vecfield renders an svg quiver", vfCanvas >= 1, `svgs: ${vfCanvas}`);

  const solveOut = await run("solve x^2 = 4, x");
  check("solve equation", solveOut.includes("x = -2; x = 2"), solveOut.replace(/\n/g, " ").slice(0, 60));

  const sysOut = await run("solve x + y = 3; x - y = 1, x, y");
  check(
    "linear system",
    sysOut.includes("x = 2") && sysOut.includes("y = 1"),
    sysOut.replace(/\n/g, " ").slice(0, 60),
  );

  // Assignment feeds the shared environment on a later line.
  const asg = await run("a := 2");
  check("assignment echo", asg.includes("saved to the Variables panel") || asg.includes(":="));
  check("environment applies to later line", (await run("a*5 + 1")).includes("11"));
  check("vars lists the binding", (await run("vars")).includes("a := 2"));
  check("help lists the grammar", (await run("help")).includes("Console commands"));

  // A parse error renders an error card rather than throwing.
  await run("2 + \\fraq{1}{2}");
  const hasErr = await page.$eval(
    ".cells .cell:last-child",
    (el) => !!el.querySelector(".error-msg"),
  );
  check("parse error card", hasErr);

  check("eval with explicit bindings", (await run("eval x^2 + y, x=3, y=0.5")).includes("9.5"));

  // Plugins (docs/PLUGINS.md): catalog, a real dsp.butter design (kv + table
  // + chart), and plugin error handling.
  const cat = await run("plugins");
  check("plugins catalog lists dsp", cat.includes("dsp.butter"), cat.replace(/\n/g, " ").slice(0, 60));
  const butterOut = await run("dsp.butter lowpass, 4, 1000, 48000");
  check(
    "dsp.butter renders design",
    butterOut.includes("Butterworth") && butterOut.includes("Gain at cutoff") && butterOut.includes("-3.0"),
    butterOut.replace(/\n/g, " ").slice(0, 80),
  );
  const hasChart = await page.$eval(
    ".cells .cell:last-child",
    (el) => !!el.querySelector("canvas") && !!el.querySelector("table"),
  );
  check("dsp.butter has table + chart", hasChart);
  const chebyOut = await run("dsp.cheby1 bandpass, 3, 1, 500, 2000, 48000");
  check(
    "dsp.cheby1 bandpass renders",
    chebyOut.includes("Chebyshev I") && chebyOut.includes("Gain at f1") &&
      chebyOut.includes("Phase response"),
    chebyOut.replace(/\n/g, " ").slice(0, 80),
  );
  // Multiple charts group into a tab strip: one visible canvas, one tab each.
  const chartTabs = await page.$eval(".cells .cell:last-child", (el) => ({
    tabs: el.querySelectorAll(".chart-tabs .chart-tab").length,
    canvases: el.querySelectorAll("canvas").length,
  }));
  check(
    "magnitude + phase + time charts grouped in tabs",
    chartTabs.tabs === 3 && chartTabs.canvases === 1,
    JSON.stringify(chartTabs),
  );
  await page.click(".cells .cell:last-child .chart-tab:nth-child(2)");
  check(
    "chart tab switches the visible chart",
    await page.$eval(
      ".cells .cell:last-child .chart-tabs",
      (el) =>
        el.querySelectorAll(".chart-tab")[1].classList.contains("active") &&
        el.querySelectorAll("canvas").length === 1,
    ),
  );

  const firOut = await run("dsp.fir lowpass, 63, 1000, 48000");
  check(
    "dsp.fir renders",
    firOut.includes("FIR windowed-sinc") && firOut.includes("linear phase") &&
      firOut.includes("Time response"),
    firOut.replace(/\n/g, " ").slice(0, 80),
  );

  const ellipOut = await run("dsp.ellip lowpass, 5, 1, 60, 1000, 48000");
  check(
    "dsp.ellip renders",
    ellipOut.includes("Elliptic") && ellipOut.includes("-1.00 dB"),
    ellipOut.replace(/\n/g, " ").slice(0, 80),
  );

  const remezOut = await run("dsp.remez lowpass, 31, 1000, 1500, 8000");
  check(
    "dsp.remez renders",
    remezOut.includes("Parks") && remezOut.includes("Passband ripple") &&
      remezOut.includes("Time response"),
    remezOut.replace(/\n/g, " ").slice(0, 80),
  );

  await run("dsp.butter lowpass, 4, 30000, 48000");
  const pluginErr = await page.$eval(
    ".cells .cell:last-child",
    (el) => el.querySelector(".error-msg")?.textContent ?? "",
  );
  check("dsp error card", pluginErr.includes("(0, fs/2)"), pluginErr);

  // --- complex numbers (v0.6) and the sys plugin ----------------------------
  const cplxOut = await run("solve x^2 + 2x + 5 = 0, x");
  check(
    "complex quadratic roots",
    cplxOut.includes("complex roots") && cplxOut.includes("2*i - 1"),
    cplxOut.replace(/\n/g, " ").slice(0, 80),
  );

  const tfOut = await run("sys.tf s+1, s^2+3s+2");
  check(
    "sys.tf renders analysis",
    tfOut.includes("stable") && tfOut.includes("Pole-zero map") &&
      tfOut.includes("Bode magnitude") && tfOut.includes("Time response"),
    tfOut.replace(/\n/g, " ").slice(0, 80),
  );
  const sysCharts = await page.$eval(".cells .cell:last-child", (el) => ({
    tabs: el.querySelectorAll(".chart-tabs .chart-tab").length,
    canvases: el.querySelectorAll("canvas").length,
  }));
  check(
    "sys.tf has 4 charts grouped in tabs",
    sysCharts.tabs === 4 && sysCharts.canvases === 1,
    JSON.stringify(sysCharts),
  );

  const odeOut = await run("sys.ode y'' + 3y' + 2y = u' + u");
  check(
    "sys.ode derives H(s)",
    odeOut.includes("s^2 + 3 s + 2") && odeOut.includes("Derived from"),
    odeOut.replace(/\n/g, " ").slice(0, 80),
  );

  const fbOut = await run("sys.feedback 1, s+1, 3");
  check(
    "sys.feedback closes the loop",
    fbOut.includes("Closed loop") && fbOut.includes("0.75") &&
      fbOut.includes("Gain margin"),
    fbOut.replace(/\n/g, " ").slice(0, 80),
  );

  const rlOut = await run("sys.rlocus 1, s^3 + 3s^2 + 2s");
  check(
    "sys.rlocus renders and finds critical gain",
    rlOut.includes("Root locus") && rlOut.includes("unstable near K"),
    rlOut.replace(/\n/g, " ").slice(0, 80),
  );

  const tfzOut = await run("sys.tfz z, z^2 - 0.5z + 0.06, 8000");
  check(
    "sys.tfz renders z-plane analysis",
    tfzOut.includes("|z| = 1") && tfzOut.includes("Pole-zero map"),
    tfzOut.replace(/\n/g, " ").slice(0, 80),
  );

  const plotOut = await run("plot sin(x)/x, -20, 20");
  const plotHasChart = await page.$eval(
    ".cells .cell:last-child",
    (el) => !!el.querySelector("canvas"),
  );
  check(
    "plot verb charts an expression",
    plotOut.includes("plot") && plotHasChart,
    plotOut.replace(/\n/g, " ").slice(0, 60),
  );

  await run("f_c := 2000");
  const fcOut = await run("dsp.butter lowpass, 4, f_c, 48000");
  check(
    "session variable resolves into plugin args",
    fcOut.includes("2000 Hz") && fcOut.includes("Butterworth"),
    fcOut.replace(/\n/g, " ").slice(0, 80),
  );

  // --- visual input: live typeset preview + symbol palette ------------------
  const clearPrompt = () =>
    page.$eval(TA, (el) => {
      el.value = "";
      el.dispatchEvent(new Event("input", { bubbles: true }));
    });

  await page.click(TA);
  await page.type(TA, "diff sin(x^2), x");
  await page.waitForSelector("[data-testid='console-preview'] .katex", {
    timeout: 6000,
  });
  const previewTex = await page.$eval(
    "[data-testid='console-preview'] annotation",
    (el) => el.textContent ?? "",
  );
  check(
    "typeset preview renders diff as d/dx",
    previewTex.includes("\\frac{d}{dx}") && previewTex.includes("\\sin"),
    previewTex.slice(0, 60),
  );
  await clearPrompt();

  await page.click(TA);
  await page.type(TA, "integrate x*sin(x), x, 0, pi");
  await page.waitForFunction(
    () =>
      document
        .querySelector("[data-testid='console-preview'] annotation")
        ?.textContent?.includes("\\int_{0}^{\\pi}") ?? false,
    { timeout: 6000 },
  );
  check("typeset preview renders definite integral bounds", true);
  await clearPrompt();

  await page.click(TA);
  await page.type(TA, "2 + \\fraq{1}{2}");
  await page.waitForSelector("[data-testid='console-preview'].has-error", {
    timeout: 6000,
  });
  check("preview surfaces parse errors with a caret", true);
  await clearPrompt();

  // A recognized verb that has no bespoke typeset branch (gcd, rootcount, …)
  // must NOT flash a false "unknown name" error — the preview stays quiet
  // instead of mis-reading the verb head as a variable product.
  for (const verb of ["gcd 12, 18", "rootcount x^5 - 3x + 1", "bezout x^2-1, x^3-1"]) {
    await page.click(TA);
    await page.type(TA, verb);
    await new Promise((r) => setTimeout(r, 350));
    // NONE renders no preview element at all; a false warning would render one
    // with .has-error. Either "no element" or "element without error" passes.
    const errEl = await page.$("[data-testid='console-preview'].has-error");
    check(`verb preview does not flash a false error: ${verb.split(" ")[0]}`, errEl === null);
    await clearPrompt();
  }

  // rootcount leads its result with the count, not the literal verb name.
  // (Locally the prebuilt wasm may lack rootcount — emcc isn't available to
  // rebuild it — so the result is an engine-unavailable error and this check
  // is skipped; CI/deploy rebuild the wasm and exercise it.)
  await run("rootcount x^5 - 3x + 1");
  const rcTitle = await page
    .$eval(".cells .cell:last-child .message-title", (el) => el.textContent.trim())
    .catch(() => null);
  if (rcTitle !== null) {
    check("rootcount leads with the count, not 'rootcount'", /distinct real roots/.test(rcTitle) && rcTitle !== "rootcount", rcTitle);
  } else {
    console.log("SKIP  rootcount headline (local wasm lacks rootcount)");
  }

  await page.click(TA);
  await page.click(".palette .palette-chip"); // π
  await page.evaluate(() => {
    const chips = [...document.querySelectorAll(".palette .palette-chip")];
    const sqrt = chips.find((c) => c.textContent.trim() === "√");
    sqrt.dispatchEvent(new MouseEvent("mousedown", { bubbles: true }));
  });
  const paletteValue = await page.$eval(TA, (el) => el.value);
  check(
    "palette inserts at the cursor",
    paletteValue.includes("π") && paletteValue.includes("√("),
    JSON.stringify(paletteValue),
  );
  await clearPrompt();

  // --- verb suggestions for a bare expression ------------------------------
  await page.click(TA);
  await page.type(TA, "x^2 - 5x + 6");
  await page.waitForSelector("[data-testid='verb-suggest'] .verb-chip", {
    timeout: 6000,
  });
  const suggestChips = await page.$$eval(
    "[data-testid='verb-suggest'] .verb-chip",
    (els) => els.map((e) => e.textContent.trim()),
  );
  check(
    "bare expression suggests verbs",
    ["factor", "solve = 0", "diff", "integrate"].every((v) =>
      suggestChips.includes(v),
    ),
    JSON.stringify(suggestChips),
  );
  // Clicking a prompt suggestion runs `<verb> <expr>` as a new cell.
  const cellsBefore = await page.$$eval(".cells .cell", (els) => els.length);
  await page.evaluate(() => {
    const chip = [
      ...document.querySelectorAll("[data-testid='verb-suggest'] .verb-chip"),
    ].find((c) => c.textContent.trim() === "factor");
    chip.click();
  });
  await page.waitForFunction(
    (n) => document.querySelectorAll(".cells .cell").length === n + 1,
    { timeout: 20000 },
    cellsBefore,
  );
  const factored = await page.$eval(
    ".cells .cell:last-child",
    (el) => el.textContent,
  );
  check(
    "clicking a suggestion runs the verb",
    factored.includes("(x - 3)") && factored.includes("(x - 2)"),
    factored.slice(0, 80),
  );
  await clearPrompt();

  // Running a bare expression offers "next" steps on its result cell; the
  // "solve = 0" chip carries the ` = 0` suffix so solve gets an equation.
  const beforeBare = await page.$$eval(".cells .cell", (els) => els.length);
  await page.click(TA);
  await page.type(TA, "x^2 - 5x + 6");
  await page.keyboard.press("Enter");
  await page.waitForFunction(
    (n) => document.querySelectorAll(".cells .cell").length === n + 1,
    { timeout: 20000 },
    beforeBare,
  );
  await page.waitForSelector(".cells .cell:last-child [data-testid='cell-next'] .next-chip", {
    timeout: 6000,
  });
  const nextChips = await page.$$eval(
    ".cells .cell:last-child [data-testid='cell-next'] .next-chip",
    (els) => els.map((e) => e.textContent.trim()),
  );
  check(
    "result cell offers next-step suggestions",
    nextChips.includes("factor") && nextChips.includes("solve = 0"),
    JSON.stringify(nextChips),
  );
  const beforeNext = await page.$$eval(".cells .cell", (els) => els.length);
  await page.evaluate(() => {
    const chip = [
      ...document.querySelectorAll(
        ".cells .cell:last-child [data-testid='cell-next'] .next-chip",
      ),
    ].find((c) => c.textContent.trim() === "solve = 0");
    chip.click();
  });
  await page.waitForFunction(
    (n) => document.querySelectorAll(".cells .cell").length === n + 1,
    { timeout: 20000 },
    beforeNext,
  );
  const solved = await page.$eval(
    ".cells .cell:last-child",
    (el) => el.textContent,
  );
  check(
    "'solve = 0' next chip finds the roots",
    solved.includes("x = 2") && solved.includes("x = 3"),
    solved.slice(0, 80),
  );
  await clearPrompt();

  // A line that already names a verb shows no suggestions.
  await page.click(TA);
  await page.type(TA, "factor x^2 - 5x + 6");
  await page.waitForSelector("[data-testid='console-preview'] .katex", {
    timeout: 6000,
  });
  const verbTypedSuggest = await page.$$eval(
    "[data-testid='verb-suggest'] .verb-chip",
    (els) => els.length,
  );
  check("no suggestions when a command is already typed", verbTypedSuggest === 0);
  await clearPrompt();

  // An ODE-shaped line (prime notation) reads as a parse error, but the
  // dsolve chip rescues it.
  await page.click(TA);
  await page.type(TA, "y' = -2t*y, y(0)=1");
  await page.waitForSelector("[data-testid='verb-suggest'] .verb-chip", {
    timeout: 6000,
  });
  const odeChips = await page.$$eval(
    "[data-testid='verb-suggest'] .verb-chip",
    (els) => els.map((e) => e.textContent.trim()),
  );
  check(
    "an ODE suggests dsolve",
    odeChips.length === 1 && odeChips[0] === "dsolve",
    JSON.stringify(odeChips),
  );
  await clearPrompt();

  // Tab accepts the first suggestion by filling `<verb> <line>` (Enter runs).
  await page.click(TA);
  await page.type(TA, "x^3 - x");
  await page.waitForSelector("[data-testid='verb-suggest'] .verb-chip", {
    timeout: 6000,
  });
  await page.keyboard.press("Tab");
  const afterTab = await page.$eval(TA, (el) => el.value);
  check(
    "Tab fills the first suggestion",
    afterTab.startsWith("factor ") && afterTab.includes("x^3 - x"),
    JSON.stringify(afterTab),
  );
  await clearPrompt();

  // --- ghost argument hints at the caret -----------------------------------
  const ghostText = () =>
    page
      .$eval("[data-testid='ghost-hint'] .ghost-text", (el) => el.textContent)
      .catch(() => null);
  await page.click(TA);
  await page.type(TA, "integrate ");
  check("ghost hints <expr> after the verb", (await ghostText()) === "<expr>");
  await page.type(TA, "x*sin(x)");
  check("ghost hidden while typing an argument", (await ghostText()) === null);
  await page.type(TA, ", ");
  check(
    "ghost hints <var> after the comma",
    (await ghostText()) === "<var>",
    JSON.stringify(await ghostText()),
  );
  await clearPrompt();
  await page.type(TA, "solve ");
  const solveGhost = await ghostText();
  check(
    "ghost joins alternatives with |",
    !!solveGhost && solveGhost.includes(" | "),
    JSON.stringify(solveGhost),
  );
  await clearPrompt();

  // --- cell sliders: Manipulate-style re-runs ------------------------------
  await run("k_a := 2");
  const sliderOut = await run("simplify k_a*x + k_a");
  check(
    "slider cell computes with the session value",
    sliderOut.includes("2*x + 2"),
    sliderOut.replace(/\s+/g, " ").slice(0, 60),
  );
  check(
    "numeric binding gets a slider",
    !!(await page.$(
      ".cells .cell:last-child [data-testid='cell-sliders'] .s-range",
    )),
  );
  await page.$eval(".cells .cell:last-child .s-range", (el) => {
    el.value = "3";
    el.dispatchEvent(new Event("input", { bubbles: true }));
  });
  await page.waitForFunction(
    () =>
      document
        .querySelector(".cells .cell:last-child")
        ?.textContent?.includes("3*x + 3"),
    { timeout: 10000 },
  );
  check("slider drag re-runs the cell in place", true);
  const panelValue = () =>
    page.evaluate(() => {
      const rows = [...document.querySelectorAll(".sidebar li[data-var-id]")];
      const row = rows.find((li) => li.querySelector("input.name")?.value === "k_a");
      return row?.querySelector("input.value")?.value ?? null;
    });
  // Mid-drag (input events only): the panel must NOT follow yet.
  check(
    "panel does not update mid-drag",
    (await panelValue()) === "2",
    JSON.stringify(await panelValue()),
  );
  // Mouse-up (change event): the linked session variable follows.
  await page.$eval(".cells .cell:last-child .s-range", (el) => {
    el.dispatchEvent(new Event("change", { bubbles: true }));
  });
  check(
    "slider release writes through to the Variables panel",
    (await panelValue()) === "3",
    JSON.stringify(await panelValue()),
  );
  // Panel → slider: editing the variable moves the slider and re-runs.
  await page.evaluate(() => {
    const rows = [...document.querySelectorAll(".sidebar li[data-var-id]")];
    const row = rows.find((li) => li.querySelector("input.name")?.value === "k_a");
    const input = row.querySelector("input.value");
    Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, "value").set.call(
      input,
      "5",
    );
    input.dispatchEvent(new Event("input", { bubbles: true }));
  });
  await page.waitForFunction(
    () =>
      document
        .querySelector(".cells .cell:last-child")
        ?.textContent?.includes("5*x + 5") &&
      document.querySelector(".cells .cell:last-child .s-range")?.value === "5",
    { timeout: 10000 },
  );
  check("panel edit moves the slider and re-runs the cell", true);
  // Fractional values plain-print as exact rationals ("5/2"): the slider
  // must survive the store's re-validation (regression: it used to vanish).
  await page.$eval(".cells .cell:last-child .s-range", (el) => {
    el.value = "2.5";
    el.dispatchEvent(new Event("input", { bubbles: true }));
    el.dispatchEvent(new Event("change", { bubbles: true }));
  });
  await page.waitForFunction(
    () =>
      document
        .querySelector(".cells .cell:last-child")
        ?.textContent?.includes("5*x/2"),
    { timeout: 10000 },
  );
  await new Promise((r) => setTimeout(r, 800)); // re-validation window
  check(
    "fractional slider survives re-validation",
    await page.evaluate(
      () => !!document.querySelector(".cells .cell:last-child .s-range"),
    ),
  );
  const varsOut = await run("vars");
  check(
    "session binding reflects the linked slider",
    varsOut.includes("k_a := 5/2"),
    varsOut.replace(/\s+/g, " ").slice(0, 80),
  );

  // --- tall-output folding -------------------------------------------------
  await run("dsp.butter lowpass, 4, 1000, 48000");
  await page.waitForFunction(
    () => !!document.querySelector(".cells .cell:last-child .fold"),
    { timeout: 10000 },
  );
  check("tall output offers a collapse control", true);
  await page.click(".cells .cell:last-child .fold");
  check(
    "collapse clips the output",
    await page.$eval(".cells .cell:last-child .out-body", (el) =>
      el.classList.contains("clipped"),
    ),
  );
  check(
    "collapsed content stays in the DOM",
    await page.$eval(".cells .cell:last-child", (el) =>
      (el.textContent || "").includes("Butterworth"),
    ),
  );
  await page.click(".cells .cell:last-child .unfold");
  check(
    "show-all expands again",
    await page.$eval(
      ".cells .cell:last-child .out-body",
      (el) => !el.classList.contains("clipped"),
    ),
  );

  // Fractional session values resolve to exact-rational text ("150187/100");
  // plugin numeric args must accept that spelling (regression).
  await run("k_f := 1501.87");
  const fracOut = await run("dsp.butter lowpass, 4, k_f, 48000");
  check(
    "fractional session variable feeds a plugin arg",
    fracOut.includes("Butterworth") && fracOut.includes("1501.87"),
    fracOut.replace(/\s+/g, " ").slice(0, 90),
  );

  // --- export: download the session as a Markdown transcript --------------
  // Stub the blob-download plumbing (jsdom/headless can't really download) to
  // capture the file name and the serialized text.
  await run("2x + 3");
  await page.evaluate(() => {
    window.__dl = {};
    window.__orig = {
      create: URL.createObjectURL,
      revoke: URL.revokeObjectURL,
      click: HTMLAnchorElement.prototype.click,
    };
    URL.createObjectURL = (blob) => {
      window.__dlBlob = blob;
      return "blob:stub";
    };
    URL.revokeObjectURL = () => {};
    HTMLAnchorElement.prototype.click = function () {
      window.__dl.download = this.download;
    };
  });
  await page.click(".console-head .export-btn");
  const exported = await page.evaluate(async () => ({
    download: window.__dl.download,
    text: window.__dlBlob ? await window.__dlBlob.text() : null,
  }));
  // Restore the real download plumbing so later tests are unaffected.
  await page.evaluate(() => {
    URL.createObjectURL = window.__orig.create;
    URL.revokeObjectURL = window.__orig.revoke;
    HTMLAnchorElement.prototype.click = window.__orig.click;
  });
  check(
    "Export downloads a .md transcript",
    exported.download === "mathsolver-session.md",
    String(exported.download),
  );
  check(
    "the transcript contains the In/Out of a run",
    !!exported.text &&
      exported.text.includes("# MathSolver console session") &&
      exported.text.includes("In[") &&
      exported.text.includes("2*x + 3"),
    (exported.text || "").slice(0, 80),
  );

  // --- copy: put the same transcript on the clipboard ---------------------
  // Stub navigator.clipboard.writeText (unreliable headless) to record the text.
  await page.evaluate(() => {
    window.__clip = { called: false, text: null };
    window.__origWrite = navigator.clipboard?.writeText?.bind(navigator.clipboard);
    navigator.clipboard = navigator.clipboard || {};
    navigator.clipboard.writeText = async (t) => {
      window.__clip.called = true;
      window.__clip.text = t;
    };
  });
  await page.click(".console-head .copy-btn");
  await new Promise((r) => setTimeout(r, 200));
  const copied = await page.evaluate(() => window.__clip);
  const copyLabel = await page.$eval(".console-head .copy-btn", (el) => el.textContent.trim());
  await page.evaluate(() => {
    if (window.__origWrite) navigator.clipboard.writeText = window.__origWrite;
  });
  check(
    "Copy writes the transcript to the clipboard",
    copied.called &&
      !!copied.text &&
      copied.text.includes("# MathSolver console session") &&
      copied.text.includes("2*x + 3"),
    (copied.text || "").slice(0, 80),
  );
  check("Copy reports success", copyLabel === "✓ Copied", copyLabel);

  // --- notebook documents: save / open / run in a fresh scope --------------
  await page.click(".console-head .clear-btn");
  await page.waitForFunction(
    () => document.querySelectorAll(".cells .cell").length === 0,
    { timeout: 5000 },
  );
  await run("s_a := 7");
  check("scoped setup computes", (await run("s_a*x + 1")).includes("7*x + 1"));
  const savedOut = await run("save demo");
  check(
    "save stores the session as a notebook",
    savedOut.includes("saved notebook 'demo' (2 commands)"),
    savedOut.replace(/\s+/g, " ").slice(0, 80),
  );
  check("notebooks lists the saved notebook", (await run("notebooks")).includes("demo"));
  // Remove the session binding: the run must work from its own scope.
  await run("unset s_a");
  await page.click(TA);
  await page.type(TA, "run demo");
  await page.keyboard.press("Enter");
  await page.waitForFunction(
    () => {
      const cells = document.querySelectorAll(".cells .cell");
      const last = cells[cells.length - 1];
      return last && (last.textContent || "").includes("7*x + 1");
    },
    { timeout: 20000 },
  );
  const runText = await page.$eval(".cells", (el) => el.textContent);
  check(
    "run replays the notebook in a fresh scope",
    runText.includes("fresh scope") && runText.includes("7*x + 1"),
  );
  check(
    "notebook scope does not leak into the session",
    !(await run("vars")).includes("s_a"),
  );
  await page.click(TA);
  await page.type(TA, "open demo");
  await page.keyboard.press("Enter");
  await page.waitForFunction(
    () => {
      const cells = document.querySelectorAll(".cells .cell");
      return (
        cells.length === 2 &&
        (cells[0].textContent || "").includes("loaded from 'demo'")
      );
    },
    { timeout: 10000 },
  );
  const opened = await page.$$eval(".cells .cell", (els) =>
    els.map((e) => (e.textContent || "").replace(/\s+/g, " ")),
  );
  check(
    "open loads the commands unevaluated",
    opened[0].includes("s_a := 7") && opened[1].includes("s_a*x + 1"),
    JSON.stringify(opened.map((t) => t.slice(0, 60))),
  );
  check(
    "notebooks panel lists the saved notebook",
    await page
      .$eval("[data-testid='notebooks-panel']", (el) =>
        (el.textContent || "").includes("demo"),
      )
      .catch(() => false),
  );

  // --- wave tool: console cell + workbench tab -----------------------------
  await run("wave 96, absorbing");
  const waveCanvas = await page.$(".cells .cell:last-child .wave canvas");
  check("wave cell renders an interactive canvas", waveCanvas !== null);
  // Energy injection must not throw: dispatch a pointer drag on the canvas.
  if (waveCanvas) {
    const box = await waveCanvas.boundingBox();
    await page.mouse.move(box.x + box.width * 0.4, box.y + box.height * 0.5);
    await page.mouse.down();
    await page.mouse.move(box.x + box.width * 0.6, box.y + box.height * 0.55, { steps: 6 });
    await page.mouse.up();
  }
  await new Promise((r) => setTimeout(r, 200));
  check("wave cell has no sliders (scoped, non-numeric)", (await page.$(".cells .cell:last-child .s-range")) === null);

  // Frequency source + source-mode controls.
  check(
    "wave cell exposes a Freq control",
    await page
      .$eval(".cells .cell:last-child .wave", (el) =>
        [...el.querySelectorAll(".ctl span")].some((s) => s.textContent.trim() === "Freq"),
      )
      .catch(() => false),
  );
  check(
    "wave cell exposes Ripple/Drive source modes",
    await page
      .$eval(".cells .cell:last-child .wave", (el) =>
        [...el.querySelectorAll(".seg-btn")].some((b) => b.textContent.trim() === "Drive"),
      )
      .catch(() => false),
  );
  // Selecting the filtered boundary reveals the filter controls.
  await page.evaluate(() => {
    [...document.querySelectorAll(".cells .cell:last-child .wave .seg-btn")]
      .find((b) => b.textContent.trim() === "Filter")
      ?.click();
  });
  await new Promise((r) => setTimeout(r, 120));
  check(
    "filtered boundary reveals filter controls (LP/HP + cutoff + reflect)",
    await page
      .$eval(".cells .cell:last-child .wave", (el) => {
        const spans = [...el.querySelectorAll(".ctl span")].map((s) => s.textContent.trim());
        const segs = [...el.querySelectorAll(".seg-btn")].map((b) => b.textContent.trim());
        return (
          spans.includes("Cutoff") &&
          spans.includes("Reflect") &&
          segs.includes("LP") &&
          segs.includes("HP")
        );
      })
      .catch(() => false),
  );

  // Switch to the Workbench "Wave" tab: full canvas, no expression input.
  await page.evaluate(() => {
    const btn = [...document.querySelectorAll(".mode-switch button")].find(
      (b) => b.textContent.trim() === "Workbench",
    );
    btn?.click();
  });
  await page.waitForSelector("#workbench-panel", { timeout: 5000 });
  await page.evaluate(() => {
    const t = [...document.querySelectorAll('[role="tab"]')].find(
      (b) => b.textContent.trim() === "Wave",
    );
    t?.click();
  });
  await page.waitForFunction(
    () => document.querySelector("#workbench-panel .wave canvas") !== null,
    { timeout: 5000 },
  );
  check("workbench Wave tab renders a canvas", true);
  check(
    "Wave tab has no expression input",
    (await page.$("#workbench-panel textarea")) === null,
  );
  // Leaving the tab must tear the animation down (no detached-canvas paints).
  await page.evaluate(() => {
    const t = [...document.querySelectorAll('[role="tab"]')].find(
      (b) => b.textContent.trim() === "Simplify",
    );
    t?.click();
  });
  await new Promise((r) => setTimeout(r, 200));
  check("Wave tab canvas removed after leaving", (await page.$("#workbench-panel .wave canvas")) === null);
  // Return to the console for the terminal no-error checks.
  await page.evaluate(() => {
    const btn = [...document.querySelectorAll(".mode-switch button")].find(
      (b) => b.textContent.trim() === "Console",
    );
    btn?.click();
  });
  await page.waitForSelector(TA, { timeout: 5000 });

  // --- console UX overhaul: reference panel, autocomplete, cell actions ----
  await page.waitForFunction(
    () =>
      document
        .querySelector(".sidebar .cmd-ref")
        ?.textContent?.includes("dsp.butter") ?? false,
    { timeout: 10000 },
  );
  check("command reference lists plugin commands", true);

  await page.evaluate(() => {
    const items = [...document.querySelectorAll(".sidebar .cmd-ref .ref-item")];
    items.find((b) => b.textContent.includes("solve <equation>")).click();
  });
  const inserted = await page.$eval(TA, (el) => el.value);
  check("reference click inserts into prompt", inserted.startsWith("solve"), inserted);
  await page.$eval(TA, (el) => {
    el.value = "";
    el.dispatchEvent(new Event("input", { bubbles: true }));
  });

  // Every reference entry — builtin and plugin — carries a runnable example.
  const exampleAudit = await page.evaluate(() => {
    const entries = [...document.querySelectorAll(".sidebar .cmd-ref .ref-entry")];
    const missing = entries
      .filter((li) => !li.querySelector(".ref-example code")?.textContent?.trim())
      .map((li) => li.querySelector(".ref-usage")?.textContent ?? "?");
    return { total: entries.length, missing };
  });
  check(
    "every reference entry has an example",
    exampleAudit.total > 50 && exampleAudit.missing.length === 0,
    `total ${exampleAudit.total}, missing: ${exampleAudit.missing.join("; ")}`,
  );

  // Clicking an example inserts the full runnable line, and it runs. The
  // helper submits an already-inserted prompt and waits for the new cell.
  const submitPrompt = async () => {
    const before = await page.$$eval(".cells .cell", (els) => els.length);
    await page.click(TA);
    await page.keyboard.press("Enter");
    await page.waitForFunction(
      (n) => {
        const cells = document.querySelectorAll(".cells .cell");
        if (cells.length !== n + 1) return false;
        const last = cells[cells.length - 1];
        return last && !(last.textContent || "").includes("computing…");
      },
      { timeout: 20000 },
      before,
    );
    return page.$eval(".cells .cell:last-child", (el) => el.textContent);
  };
  await page.evaluate(() => {
    const egs = [...document.querySelectorAll(".sidebar .cmd-ref .ref-example")];
    egs.find((b) => b.textContent.includes("factor x^2 - 5x + 6")).click();
  });
  const egInserted = await page.$eval(TA, (el) => el.value);
  check(
    "example click inserts the runnable line",
    egInserted === "factor x^2 - 5x + 6",
    JSON.stringify(egInserted),
  );
  const egOut = await submitPrompt();
  check("inserted example runs to a result", egOut.includes("(x - 3)"), egOut.slice(0, 60));

  // A plugin example works end to end the same way.
  await page.evaluate(() => {
    const egs = [...document.querySelectorAll(".sidebar .cmd-ref .ref-example")];
    egs.find((b) => b.textContent.includes("pde.heat 1, 1, x*(1-x)")).click();
  });
  const egPlugin = await submitPrompt();
  check(
    "plugin example runs to a result",
    egPlugin.includes("Temperature profiles"),
    egPlugin.slice(0, 60),
  );

  await page.click(TA);
  await page.type(TA, "integ");
  await page.waitForSelector(".suggest-item", { timeout: 5000 });
  const sugg = await page.$eval(".suggest-item", (el) => el.textContent);
  check("autocomplete suggests integrate", sugg.includes("integrate"), sugg);
  await page.keyboard.press("Tab");
  const completed = await page.$eval(TA, (el) => el.value);
  check("tab completes the command", completed === "integrate ", JSON.stringify(completed));
  await page.waitForSelector(".usage-hint", { timeout: 5000 });
  const usage = await page.$eval(".usage-hint", (el) => el.textContent ?? "");
  check("usage hint shown after completion", usage.includes("integrate <expr>"), usage);
  await page.$eval(TA, (el) => {
    el.value = "";
    el.dispatchEvent(new Event("input", { bubbles: true }));
  });

  const beforeRerun = await page.$$eval(".cells .cell", (e) => e.length);
  await page.evaluate(() => {
    document.querySelector(".cells .cell .action[aria-label^='Run input']").click();
  });
  await page.waitForFunction(
    (n) => document.querySelectorAll(".cells .cell").length === n + 1,
    { timeout: 20000 },
    beforeRerun,
  );
  check("cell rerun appends a new cell", true);

  // The console session survives a reload (localStorage).
  const before = await page.$$eval(".cells .cell", (e) => e.length);
  await page.reload({ waitUntil: "load" });
  await page.waitForSelector(".console .cells .cell", { timeout: 10000 });
  const after = await page.$$eval(".cells .cell", (e) => e.length);
  check("session persists across reload", after === before, `${before} -> ${after}`);

  check("no page errors", pageErrors.length === 0, pageErrors.join(" | "));
  check("no console errors", consoleErrors.length === 0, consoleErrors.join(" | "));
} finally {
  await browser.close();
  server.kill();
}

console.log(`\n${failCount === 0 ? "ALL PASS" : "FAILURES"}: ${passCount} passed, ${failCount} failed`);
process.exit(failCount === 0 ? 0 : 1);
