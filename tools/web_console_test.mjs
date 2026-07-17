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
  return page.$eval(".cells .cell:last-child", (el) => el.innerText);
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
  const chartCount = await page.$eval(
    ".cells .cell:last-child",
    (el) => el.querySelectorAll("canvas").length,
  );
  check("magnitude + phase + time charts", chartCount === 3, `canvases: ${chartCount}`);

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
  const sysCharts = await page.$eval(
    ".cells .cell:last-child",
    (el) => el.querySelectorAll("canvas").length,
  );
  check("sys.tf has 4 charts", sysCharts === 4, `canvases: ${sysCharts}`);

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
