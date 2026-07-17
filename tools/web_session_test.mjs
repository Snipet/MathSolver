// Browser verification of the variable-assignment web feature
// (docs/proposals/variable-assignment.md): Part A = the spec's §12.2 worked
// web session, Part B = per-verb semantics, the main-input `:=` flow, and
// error-message pins. Requires `npm run build` in web/ first; drives the
// built app in system Chrome via web/'s puppeteer-core devDependency.
//
//   node tools/web_session_test.mjs
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
server.stdout.on("data", (d) => process.stdout.write(`[vite] ${d}`));
server.stderr.on("data", (d) => process.stderr.write(`[vite] ${d}`));
const url = `http://127.0.0.1:${port}/`;
// Wait for the server.
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
  args: ["--window-size=1400,1000"],
  defaultViewport: { width: 1400, height: 1000 },
});
const page = await browser.newPage();
const pageErrors = [];
const consoleErrors = [];
page.on("pageerror", (e) => pageErrors.push(String(e)));
page.on("console", (m) => {
  if (m.type() === "error") consoleErrors.push(m.text());
});

const SEL = {
  ta: "#workbench-panel textarea",
  compute: "#workbench-panel .compute",
  panel: '.sidebar [data-testid="vars-panel"]',
  rows: '.sidebar [data-testid=\"vars-panel\"] li',
  chips: ".chips .chip",
};

async function setField(sel, text) {
  await page.$eval(
    sel,
    (el, t) => {
      const proto =
        el instanceof HTMLTextAreaElement
          ? HTMLTextAreaElement.prototype
          : HTMLInputElement.prototype;
      Object.getOwnPropertyDescriptor(proto, "value").set.call(el, t);
      el.dispatchEvent(new Event("input", { bubbles: true }));
    },
    text,
  );
}

const wait = (fn, ...args) =>
  page.waitForFunction(fn, { timeout: 15000, polling: 100 }, ...args);

async function waitChip(substr, kind = null) {
  await wait(
    (s, k) =>
      [...document.querySelectorAll(".chips .chip")].some(
        (c) => c.textContent.includes(s) && (!k || c.classList.contains(k)),
      ),
    substr,
    kind,
  );
}

const chipTexts = () =>
  page.$$eval(SEL.chips, (els) => els.map((e) => e.textContent.trim()));

async function rowByName(name) {
  return page.evaluateHandle((n) => {
    for (const li of document.querySelectorAll('.sidebar [data-testid=\"vars-panel\"] li')) {
      if (li.querySelector("input.name")?.value === n) return li;
    }
    return null;
  }, name);
}

async function rowState(name) {
  return page.evaluate((n) => {
    for (const li of document.querySelectorAll('.sidebar [data-testid=\"vars-panel\"] li')) {
      if (li.querySelector("input.name")?.value !== n) continue;
      return {
        found: true,
        inactive: li.classList.contains("inactive"),
        error: li.querySelector(".row-error")?.textContent ?? null,
        preview: !!li.querySelector(".row-preview .katex"),
        previewText: li.querySelector(".row-preview")?.textContent ?? "",
        eqBadge: !!li.querySelector(".badge"),
        value: li.querySelector("input.value")?.value ?? "",
      };
    }
    return { found: false };
  }, name);
}

async function setRowField(name, field, text) {
  const sel = `.sidebar [data-testid=\"vars-panel\"] li`;
  await page.evaluate(
    (n, f, t) => {
      for (const li of document.querySelectorAll(
        '.sidebar [data-testid=\"vars-panel\"] li',
      )) {
        if (li.querySelector("input.name")?.value !== n) continue;
        const el = li.querySelector(`input.${f}`);
        Object.getOwnPropertyDescriptor(
          HTMLInputElement.prototype,
          "value",
        ).set.call(el, t);
        el.dispatchEvent(new Event("input", { bubbles: true }));
        return;
      }
      throw new Error(`row not found: ${n}`);
    },
    name,
    field,
    text,
  );
}

async function lastRowSet(field, text) {
  await page.$eval(
    `.sidebar [data-testid=\"vars-panel\"] li:last-of-type input.${field}`,
    (el, t) => {
      Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, "value").set.call(
        el,
        t,
      );
      el.dispatchEvent(new Event("input", { bubbles: true }));
    },
    text,
  );
}

const cardText = () =>
  page
    .$eval(".result-region .card", (el) => el.textContent)
    .catch(() => "");

async function computeAndWait(marker) {
  const m = marker.replace(/\s+/g, "");
  await page.click(SEL.compute);
  try {
    await wait(
      (mm) => {
        const card = document.querySelector(".result-region .card");
        return card && card.textContent.replace(/\s+/g, "").includes(mm);
      },
      m,
    );
  } catch {
    const t = await cardText();
    throw new Error(
      `computeAndWait('${marker}') timed out; card: ${t.slice(0, 400)}`,
    );
  }
}

const tab = (id) => page.click(`#tab-${id}`);

// ===========================================================================
console.log("--- Part A: spec §12.2 worked web session ---");

// (1) restore `a := 2` from localStorage (seed once, then a clean load)
await page.goto(url, { waitUntil: "domcontentloaded" });
await page.evaluate(() => {
  localStorage.setItem(
    "mathsolver.vars",
    JSON.stringify({ v: 1, vars: [{ name: "a", value: "2", ts: 1752600000000 }] }),
  );
});
await page.goto(url, { waitUntil: "networkidle0" });
await wait(() => document.querySelector(".version-chip")?.textContent?.startsWith("v"));
await wait(
  () =>
    !!document.querySelector(
      '.sidebar [data-testid=\"vars-panel\"] li .row-preview .katex',
    ),
);
let st = await rowState("a");
check("A1: panel restores a := 2 with KaTeX preview", st.found && st.preview && !st.inactive, JSON.stringify(st));

// (2) Simplify `a x^2 + a`: chip, result, computed-from, history
await setField(SEL.ta, "a x^2 + a");
await waitChip("a := 2", "applied");
check("A2: chip 'a := 2' under the input", true);
await computeAndWait("2*x^2 + 2");
let plain = await page.$$eval(".card .copy-field .text", (els) => els.map((e) => e.textContent));
check("A2: simplify result is 2*x^2 + 2", plain[0] === "2*x^2 + 2", plain.join("|"));
check(
  "A2: 'computed from' resolved-input line shown",
  await page.$('[data-testid="computed-from"]').then(Boolean),
);
let hist = await page.$$eval(".sidebar .history .entry .input", (els) => els.map((e) => e.textContent));
check("A2: run landed in History", hist.includes("a x^2 + a"), hist.join("|"));

// (3) add row: name `speed` errors, rename v_max value a^3 previews
await page.click(`${SEL.panel} .add`);
await lastRowSet("name", "speed");
await wait(() => {
  const li = [...document.querySelectorAll('.sidebar [data-testid=\"vars-panel\"] li')].pop();
  return li?.querySelector(".row-error")?.textContent?.includes("unknown name 'speed'");
});
st = await page.$eval(`${SEL.panel} li:last-of-type .row-error`, (el) => el.textContent);
check(
  "A3: 'speed' shows word-guard error + assignment hint",
  st.includes("variables are single letters") &&
    st.includes("assignment targets follow the same rule — try a subscripted name like s_max := 5"),
  st,
);
await lastRowSet("name", "v_max");
await lastRowSet("value", "a^3");
await wait(() => {
  const li = [...document.querySelectorAll('.sidebar [data-testid=\"vars-panel\"] li')].pop();
  return (
    !li.classList.contains("inactive") && !!li.querySelector(".row-preview .katex")
  );
});
check("A3: row previews v_max := a^3 (KaTeX, active)", true);

// (4) self-cycle: value v_{max} + 1 flags and grays out; fix restores
await setRowField("v_max", "value", "v_{max} + 1");
await wait(() => {
  const li = [...document.querySelectorAll('.sidebar [data-testid=\"vars-panel\"] li')].filter(
    (l) => l.querySelector("input.name")?.value === "v_max",
  )[0];
  return li?.classList.contains("inactive") && !!li.querySelector(".row-error");
});
st = await rowState("v_max");
check(
  "A4: self-definition flags \"'v_max' cannot be defined in terms of itself\" and row is inactive",
  st.inactive && st.error === "'v_max' cannot be defined in terms of itself",
  JSON.stringify(st),
);
await setRowField("v_max", "value", "a^3");
await wait(() => {
  const li = [...document.querySelectorAll('.sidebar [data-testid=\"vars-panel\"] li')].filter(
    (l) => l.querySelector("input.name")?.value === "v_max",
  )[0];
  return li && !li.classList.contains("inactive");
});
check("A4: fixed back to a^3 — row active again", true);

// (5) Solve x^2 = v_{max} + 1 -> ±3 via v_max -> a^3 -> 8; then x := 1 amber chip
await tab("solve");
await setField(SEL.ta, "x^2 = v_{max} + 1");
await waitChip("v_max := a^3", "applied");
await waitChip("a := 2", "applied");
check("A5: chips show v_max := a^3 and a := 2", true);
await wait(() => {
  const sel = document.querySelector(".ctl-row select");
  return sel && sel.value === "x";
});
check("A5: solve variable defaults to x (environment disambiguates)", true);
await computeAndWait("x = 3");
let card = await cardText();
check("A5: roots x = -3 and x = 3", card.includes("-3") && /x=3|x = 3/.test(card.replace(/−/g, "-")), card.slice(0, 200));

await page.click(`${SEL.panel} .add`);
await lastRowSet("name", "x");
await lastRowSet("value", "1");
await waitChip("x := 1 ignored (solving for x)", "ignored");
check("A5: amber chip 'x := 1 ignored (solving for x)'", true);
await computeAndWait("x = 3");
card = await cardText();
check("A5: same roots with x assigned", card.includes("-3"), card.slice(0, 200));

// (6) E_1 := x + y = 3 (eq badge); system E_1; x - y = 1 -> x = 2, y = 1;
//     derivative tab with E_1 shows the muted chip
await page.click(`${SEL.panel} .add`);
await lastRowSet("name", "E_1");
await lastRowSet("value", "x + y = 3");
await wait(() => {
  const li = [...document.querySelectorAll('.sidebar [data-testid=\"vars-panel\"] li')].pop();
  return !!li.querySelector(".badge");
});
check("A6: E_1 row badged 'eq'", true);
await setField(SEL.ta, "E_1; x - y = 1");
await waitChip("E_1 := x + y = 3", "applied");
check("A6: chip E_1 := x + y = 3 (whole-segment equation applies on Solve)", true);
// "with variables x, y": toggle the solve-for chips like the user does.
await wait(() => {
  const all = [...document.querySelectorAll(".var-chip")].map((b) => b.textContent.trim());
  return all.includes("x") && all.includes("y");
});
await page.evaluate(() => {
  for (const b of document.querySelectorAll(".var-chip")) {
    const name = b.textContent.trim();
    const pressed = b.getAttribute("aria-pressed") === "true";
    if ((name === "x" || name === "y") && !pressed) b.click();
    if (name !== "x" && name !== "y" && pressed) b.click();
  }
});
await wait(() => {
  const pressed = [...document.querySelectorAll(".var-chip[aria-pressed='true']")].map(
    (b) => b.textContent.trim(),
  );
  return pressed.includes("x") && pressed.includes("y") && pressed.length === 2;
});
await computeAndWait("x = 2");
card = await cardText();
check(
  "A6: system solution x = 2, y = 1",
  card.replace(/\s/g, "").includes("x=2") && card.replace(/\s/g, "").includes("y=1"),
  card.slice(0, 200),
);
await tab("derivative");
await setField(SEL.ta, "E_1");
await waitChip("E_1 is an equation — not applied here", "muted");
check("A6: derivative tab shows muted 'E_1 is an equation — not applied here'", true);

// (7) reload: rows return; a devtools-corrupted row comes back inactive with
//     its parse error instead of vanishing
await page.evaluate(() => {
  const store = JSON.parse(localStorage.getItem("mathsolver.vars"));
  store.vars.push({ name: "q", value: "2^", ts: Date.now() });
  localStorage.setItem("mathsolver.vars", JSON.stringify(store));
});
await page.reload({ waitUntil: "networkidle0" });
await wait(() => document.querySelector(".version-chip")?.textContent?.startsWith("v"));
await wait(() => {
  const rows = [...document.querySelectorAll('.sidebar [data-testid=\"vars-panel\"] li')];
  const q = rows.find((li) => li.querySelector("input.name")?.value === "q");
  return q && q.classList.contains("inactive") && q.querySelector(".row-error");
});
st = await rowState("q");
check(
  "A7: corrupted row 'q := 2^' restored inactive with its parse error",
  st.inactive && st.error && st.error.includes("unexpected end of input"),
  JSON.stringify(st),
);
const names = await page.$$eval(`${SEL.panel} li input.name`, (els) =>
  els.map((e) => e.value),
);
check(
  "A7: all rows survive reload (a, v_{max}, x, E_1, q)",
  ["a", "v_{max}", "x", "E_1", "q"].every((n) => names.includes(n)),
  names.join(","),
);

// ===========================================================================
console.log("--- Part B: main-input ':=' flow, per-verb semantics, error pins ---");
await page.evaluate(() => localStorage.clear());
await page.reload({ waitUntil: "networkidle0" });
await wait(() => document.querySelector(".version-chip")?.textContent?.startsWith("v"));
check(
  "B0: cleared storage -> empty panel",
  (await page.$$eval(`${SEL.panel} li`, (e) => e.length)) === 0,
);

// B1: assignments typed in the MAIN input
await tab("simplify");
for (const line of ["m := 2", "f := g + 1", "g := x^2"]) {
  await setField(SEL.ta, line);
  await wait(() =>
    document
      .querySelector('[data-testid="assign-preview"]')
      ?.textContent?.includes("assignment:"),
  );
  await computeAndWait("saved to the Variables panel");
}
check("B1: ':=' lines recognized, previewed, and saved via Compute", true);
check(
  "B1: panel now has m, f, g rows",
  (await page.$$eval(`${SEL.panel} li input.name`, (els) => els.map((e) => e.value)))
    .join(",") === "m,f,g",
);

// B2: lazy chain f -> g + 1 -> x^2 + 1 on Simplify, with both chips
await setField(SEL.ta, "f");
await waitChip("f := g + 1", "applied");
await waitChip("g := x^2", "applied");
await computeAndWait("x^2 + 1");
check("B2: simplify f = x^2 + 1 (transitive resolution)", true);
const cf = await page.$eval('[data-testid="computed-from"]', (el) => el.textContent);
check("B2: computed-from line present for f", cf.includes("computed from:"), cf);

// B3: derivative of f picks x (residual) and differentiates the resolved form
await tab("derivative");
await setField(SEL.ta, "f");
await wait(() => {
  const sel = document.querySelector(".ctl-row select");
  return sel && sel.value === "x";
});
await computeAndWait("2*x");
check("B3: diff f, x = 2*x", true);

// B4: cycle rejected from the main input (REPL semantics: reject, don't store)
await setField(SEL.ta, "g := f^2");
await wait(() =>
  document
    .querySelector('[data-testid="assign-preview"]')
    ?.textContent?.includes("assignment would create a cycle"),
);
const cyc = await page.$eval('[data-testid="assign-preview"]', (el) => el.textContent);
check(
  "B4: 'g := f^2' pre-flagged 'assignment would create a cycle: g -> f -> g'",
  cyc.includes("assignment would create a cycle: g -> f -> g"),
  cyc,
);
await page.click(SEL.compute);
await wait(() =>
  document
    .querySelector(".result-region .card.error-card")
    ?.textContent?.includes("assignment would create a cycle: g -> f -> g"),
);
st = await rowState("g");
check("B4: g still x^2 (cycle rejected, binding unchanged)", st.value === "x^2", JSON.stringify(st));

// B5: bare-equation disambiguation — m := 2 makes m*x = 6 solve for x
await tab("solve");
await setField(SEL.ta, "m*x = 6");
await waitChip("m := 2", "applied");
await wait(() => document.querySelector(".ctl-row select")?.value === "x");
await computeAndWait("x = 3");
card = await cardText();
check("B5: solve m*x = 6 -> x = 3", card.replace(/\s/g, "").includes("x=3"), card.slice(0, 200));

// B6: definite integral bounds resolve (integrate x^2, x, 0, m -> 8/3)
await tab("integral");
await setField(SEL.ta, "x^2");
await page.click(".ctl.checkbox input");
await wait(() => !!document.querySelector("input.expr"));
const exprInputs = await page.$$("input.expr");
await exprInputs[0].evaluate((el) => {
  Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, "value").set.call(el, "0");
  el.dispatchEvent(new Event("input", { bubbles: true }));
});
await exprInputs[1].evaluate((el) => {
  Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, "value").set.call(el, "m");
  el.dispatchEvent(new Event("input", { bubbles: true }));
});
await computeAndWait("8/3");
check("B6: definite integral with bound m resolves to 8/3", true);

// B7: evaluate binds only the residual symbol (x), env supplies m
await tab("evaluate");
await setField(SEL.ta, "m x^2 + m");
await waitChip("m := 2", "applied");
await wait(() => {
  const labels = [...document.querySelectorAll(".ctl-row .ctl span")].map((s) =>
    s.textContent.trim(),
  );
  return labels.includes("x =") && !labels.includes("m =");
});
check("B7: evaluate offers a binding for x only (m resolved by the environment)", true);
await page.$eval('.ctl-row input[type="number"]', (el) => {
  Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, "value").set.call(el, "2");
  el.dispatchEvent(new Event("input", { bubbles: true }));
});
await computeAndWait("10");
check("B7: evaluate m x^2 + m at x=2 -> 10", true);

// B8: plot samples the resolved input (m*x -> 2*x), no sample error
await tab("plot");
await setField(SEL.ta, "m*x");
await waitChip("m := 2", "applied");
await wait(() => {
  const legend = document.querySelector(".plot .legend");
  return legend && legend.textContent.includes("f") && !document.querySelector(".overlay.error");
});
check("B8: plot renders resolved m*x with no sample error", true);

// B9: target error pins (§2.3), straight from the main input preview
await tab("simplify");
const pins = [
  ["pi := 3", "cannot assign to the constant 'pi'"],
  ["sin := x", "cannot assign to the function name 'sin'"],
  ["E1 := 5", "— 'E1' reads as E*1; did you mean E_1?"],
  ["x :=", "assignment needs a value (e.g. x := 2)"],
  ["a := a + 1", "'a' cannot be defined in terms of itself"],
];
for (const [line, msg] of pins) {
  await setField(SEL.ta, line);
  await wait(
    (m) =>
      document
        .querySelector('[data-testid="assign-preview"]')
        ?.textContent?.includes(m),
    msg,
  ).catch(() => {});
  const got = await page
    .$eval('[data-testid="assign-preview"]', (el) => el.textContent)
    .catch(() => "(no preview)");
  check(`B9: "${line}" -> ${msg}`, got.includes(msg), got);
}

// B10: equation name inside an expression
await setField(SEL.ta, "E_1 := x + y = 3");
await wait(() =>
  document
    .querySelector('[data-testid="assign-preview"]')
    ?.textContent?.includes("assignment:"),
);
await computeAndWait("saved to the Variables panel");
await setField(SEL.ta, "E_1 + 1");
await waitChip("E_1 is an equation — not applied here", "muted");
check("B10: simplify tab shows the muted equation chip for E_1 + 1", true);
await tab("solve");
await setField(SEL.ta, "E_1 + 1");
await wait(() => document.querySelector('#workbench-panel textarea')?.value === "E_1 + 1");
await page.click(SEL.compute);
await wait(() =>
  document
    .querySelector(".result-region .card.error-card")
    ?.textContent?.includes("'E_1' names an equation and cannot be used inside an expression"),
);
check("B10: solve tab errors — equation name used inside an expression", true);

// B11: persistence round-trip + clear-all with confirm
await page.reload({ waitUntil: "networkidle0" });
await wait(() => document.querySelector(".version-chip")?.textContent?.startsWith("v"));
const names2 = await page.$$eval(`${SEL.panel} li input.name`, (els) =>
  els.map((e) => e.value),
);
check(
  "B11: rows persist across reload (m, f, g, E_1)",
  ["m", "f", "g", "E_1"].every((n) => names2.includes(n)),
  names2.join(","),
);
await page.click(`${SEL.panel} .clear`);
check(
  "B11: clear-all asks for confirmation",
  (await page.$eval(`${SEL.panel} .clear`, (el) => el.textContent.trim())) ===
    "Really clear?",
);
await page.click(`${SEL.panel} .clear`);
await wait(() => document.querySelectorAll('.sidebar [data-testid=\"vars-panel\"] li').length === 0);
const stored = await page.evaluate(() => localStorage.getItem("mathsolver.vars"));
check("B11: cleared panel persists an empty store", stored === '{"v":1,"vars":[]}', stored);

// B12: with the environment cleared, f is an ordinary free symbol again
await tab("simplify");
await setField(SEL.ta, "f");
await wait(() => document.querySelector(".preview.ok .katex"));
await computeAndWait("f");
check(
  "B12: simplify f with empty env prints f, no chips, no computed-from",
  (await page.$$(".chips .chip")).length === 0 &&
    !(await page.$('[data-testid="computed-from"]')),
);

// B13: "computed from" shows the resolved input UN-simplified (§8
// simplifyResult=false): w := 7 then x + x + w computes 2*x + 7 from
// "x + x + 7", not from "2x + 7".
await setField(SEL.ta, "w := 7");
await wait(() =>
  document
    .querySelector('[data-testid="assign-preview"]')
    ?.textContent?.includes("assignment:"),
);
await computeAndWait("saved to the Variables panel");
await setField(SEL.ta, "x + x + w");
await waitChip("w := 7", "applied");
await computeAndWait("2*x + 7");
const cf13 = await page.$eval('[data-testid="computed-from"]', (el) =>
  el.textContent.replace(/\s+/g, ""),
);
check(
  "B13: computed-from is the un-simplified resolved input x + x + 7",
  cf13.includes("x+x+7") && !cf13.includes("2x+7"),
  cf13,
);

// B14: variable auto-pick when EVERY input symbol is assigned — no choice is
// mathematically forced, so the picker must land on the conventional x, not
// the alphabetical accident g (regression: d/dg of x^2 + g returned 1).
for (const line of ["g := x^2", "x := 3"]) {
  await setField(SEL.ta, line);
  await wait(() =>
    document
      .querySelector('[data-testid="assign-preview"]')
      ?.textContent?.includes("assignment:"),
  );
  await computeAndWait("saved to the Variables panel");
}
await tab("derivative");
await setField(SEL.ta, "t^2"); // make the current variable stale (t)
await wait(() => document.querySelector(".ctl-row select")?.value === "t");
await setField(SEL.ta, "x^2 + g");
await wait(() => document.querySelector(".ctl-row select")?.value === "x");
check("B14: picker lands on x (not g) with every symbol assigned", true);
await waitChip("x := 3 ignored (differentiating with respect to x)", "ignored");
await computeAndWait("4*x");
check("B14: d/dx of x^2 + g with g := x^2 is 4*x", true);

// B15: §9.2 cap enforced at write time — a 33rd binding is refused with a
// visible error instead of silently outliving what persistence keeps.
await page.evaluate(() => {
  const vars = [];
  for (let i = 1; i <= 32; i++)
    vars.push({ name: `a_${i}`, value: String(i), ts: Date.now() });
  localStorage.setItem("mathsolver.vars", JSON.stringify({ v: 1, vars }));
});
await page.reload({ waitUntil: "networkidle0" });
await wait(() => document.querySelector(".version-chip")?.textContent?.startsWith("v"));
await wait(
  () => document.querySelectorAll('.sidebar [data-testid=\"vars-panel\"] li').length === 32,
);
check(
  "B15: '+ add' disabled at the 32-row cap",
  await page.$eval(`${SEL.panel} .add`, (el) => el.disabled),
);
await tab("simplify");
await setField(SEL.ta, "z_9 := 1");
await wait(() =>
  document
    .querySelector('[data-testid="assign-preview"]')
    ?.textContent?.includes("assignment:"),
);
await page.click(SEL.compute);
await wait(() =>
  document
    .querySelector(".result-region .card.error-card")
    ?.textContent?.includes("variable limit reached (32) — delete a binding to add another"),
);
check("B15: 33rd binding refused with the cap error", true);
const memAndStored = await page.evaluate(() => ({
  mem: document.querySelectorAll('.sidebar [data-testid=\"vars-panel\"] li').length,
  stored: JSON.parse(localStorage.getItem("mathsolver.vars")).vars.length,
}));
check(
  "B15: memory and storage agree at 32 rows",
  memAndStored.mem === 32 && memAndStored.stored === 32,
  JSON.stringify(memAndStored),
);
// Redefinition of an existing name is still allowed at the cap.
await setField(SEL.ta, "a_1 := 100");
await wait(() =>
  document
    .querySelector('[data-testid="assign-preview"]')
    ?.textContent?.includes("assignment:"),
);
await computeAndWait("saved to the Variables panel");
const a1 = await page.evaluate(
  () =>
    JSON.parse(localStorage.getItem("mathsolver.vars")).vars.find(
      (v) => v.name === "a_1",
    )?.value,
);
check("B15: redefining a_1 at the cap still works", a1 === "100", String(a1));

// ===========================================================================
check("zero pageerrors", pageErrors.length === 0, pageErrors.join(" | "));
check("zero console errors", consoleErrors.length === 0, consoleErrors.join(" | "));

console.log(`\n${passCount} passed, ${failCount} failed`);
await browser.close();
server.kill();
process.exit(failCount === 0 ? 0 : 1);
