// End-to-end smoke test against the real WASM engine. Requires a build:
//   bash tools/build_wasm.sh   (stages apps/ink/wasm/mathsolver.js + .wasm)
//   npm run build              (compiles this app to dist/)
//   npm run e2e
//
// Drives the same processLine() pipeline the CLI uses and checks real results.

import { createEngine } from "../dist/engine/engine.js";
import { processLine } from "../dist/core/run.js";

const engine = await createEngine();

/** [input line, expected /regex/ over the joined output]. */
const cases = [
  ["simplify 2x + 3x", /^5\*x$/m],
  ["expand (x+1)^2", /x\^2 \+ 2\*x \+ 1/],
  ["factor x^2 - 5x + 6", /\(x - 3\)/],
  ["cancel (x^2-1)/(x-1)", /^x \+ 1$/m],
  ["together 1/x + 1/y", /\(x \+ y\)\/\(x\*y\)/],
  ["solve x^2 = 4", /x = -?2/],
  ["x^2 = 9", /x = -?3/], // bare equation -> solve
  ["diff sin(x^2)", /2\*x\*cos\(x\^2\)/],
  ["integrate x*sin(x)", /\+ C/],
  ["integrate sin(x), x, 0, pi", /value = 2/],
  ["limit sin(x)/x, x, 0", /limit = 1/],
  ["sum k^2, k, 1, n", /sum = /],
  ["dsolve y' + y = 0, y(0)=1", /y\(t\) = e\^\(-t\)/],
  ["series sin(x), x, 0, 5", /x\^5\/120/],
  ["gcd 48, 36", /^12$/m],
  ["isprime 97", /prime/],
  ["cfrac 355/113", /3/],
  ["x^2 < 4", /-2 < x < 2/],
  ["stats 1, 2, 3, 4, 5", /mean = 3/],
  ["seq 1, 4, 9, 16, 25", /pattern:/],
  ["eval x^2 + y, x=3, y=0.5", /^9\.5$/m],
  ["latex sqrt(x)/2", /\\frac|\\sqrt/],
];

let failures = 0;
for (const [line, re] of cases) {
  const res = await processLine(engine, line);
  const text = res.lines.map((l) => l.text).join("\n");
  const ok = re.test(text);
  if (!ok) failures++;
  console.log(`${ok ? "ok  " : "FAIL"} ${line}  ->  ${text.split("\n")[0] ?? ""}`);
  if (!ok) console.error(`     expected ${re}`);
}

console.log(failures === 0 ? "\nALL PASS" : `\n${failures} FAILURES`);
process.exit(failures === 0 ? 0 : 1);
