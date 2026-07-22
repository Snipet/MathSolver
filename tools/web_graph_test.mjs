// Browser verification of the Desmos-style graphing calculator (Workbench
// "Plot" tab). Requires `npm run build` in web/ first.
//   node tools/web_graph_test.mjs
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
    [...document.querySelectorAll('[role="tab"]')].find((b) => b.textContent.trim() === "Plot")?.click();
  });
  await page.waitForSelector(".calc .graph canvas", { timeout: 8000 });
  check("Plot tab renders the graphing calculator", true);

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
