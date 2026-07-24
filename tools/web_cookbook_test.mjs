// Browser check for the console Cookbook panel. Recipes and their steps often
// repeat a command string (e.g. showing a value before and after a
// redefinition), which crashed a keyed {#each} keyed by value. This drives the
// UI: open every category and assert it renders with no page error.
//   node tools/web_cookbook_test.mjs   (after `npm run build` in web/)
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
    if ((await fetch(url)).ok) break;
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
page.on("pageerror", (e) => pageErrors.push(String(e)));

try {
  await page.goto(url, { waitUntil: "load" });
  await page.waitForFunction(
    () => /\d+\.\d+\.\d+/.test(document.querySelector(".version-chip")?.textContent || ""),
    { timeout: 30000 },
  );
  // Console mode → Cookbook tab.
  await page.evaluate(() =>
    [...document.querySelectorAll(".mode-switch button")].find((b) => b.textContent.trim() === "Console")?.click(),
  );
  await page.evaluate(() =>
    [...document.querySelectorAll(".console-ref .seg [role='tab']")].find((b) => b.textContent.trim() === "Cookbook")?.click(),
  );
  await page.waitForSelector(".cb-section-head", { timeout: 6000 });

  // Expand every category (previously the "Session Variables & Notebooks"
  // section threw each_key_duplicate the moment it opened).
  const sections = await page.$$eval(".cb-section-head", (els) => {
    els.forEach((b) => b.click());
    return els.length;
  });
  await new Promise((r) => setTimeout(r, 500));
  const recipes = await page.$$eval(".cb-recipe", (els) => els.length);
  const steps = await page.$$eval(".cb-step", (els) => els.length);

  check("all cookbook categories expand", sections > 0, `sections=${sections}`);
  check("recipes render across categories", recipes > 0, `recipes=${recipes}`);
  check("steps render (incl. repeated command strings)", steps > 0, `steps=${steps}`);
  check("no page errors (no each_key_duplicate crash)", pageErrors.length === 0, pageErrors.join(" | "));
} catch (e) {
  console.error("FATAL", e);
  fail++;
} finally {
  await browser.close();
  server.kill();
}

console.log(`\n${fail === 0 ? "ALL PASS" : "FAILURES"}: ${pass} passed, ${fail} failed`);
process.exit(fail === 0 ? 0 : 1);
