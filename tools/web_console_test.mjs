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
  await run("dsp.butter lowpass, 4, 30000, 48000");
  const pluginErr = await page.$eval(
    ".cells .cell:last-child",
    (el) => el.querySelector(".error-msg")?.textContent ?? "",
  );
  check("dsp error card", pluginErr.includes("Nyquist"), pluginErr);

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
