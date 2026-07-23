// Unit tests for breakDiscontinuities (web/src/lib/graph/viewport.ts): the
// pole-break pass that stops a curve from drawing a vertical connector through
// an asymptote (tan, 1/x, sec, …).
//   node tools/graph_pole_break_test.mjs
import { breakDiscontinuities } from "../web/src/lib/graph/viewport.ts";

let passed = 0;
let failed = 0;
function check(name, cond) {
  if (cond) {
    passed++;
    console.log(`PASS  ${name}`);
  } else {
    failed++;
    console.log(`FAIL  ${name}`);
  }
}
const nullsIn = (arr) => arr.reduce((n, v) => n + (v === null ? 1 : 0), 0);

// A 1/x-style pole: two finite samples straddling zero with a huge jump, in a
// view whose y-span is ~10. The boundary between them must be broken.
{
  const ys = [-2, -1, -50, 60, 1, 0.5]; // pole between index 2 and 3
  const out = breakDiscontinuities(ys, 10);
  check("pole: opposite-sign huge jump is broken", nullsIn(out) === 1);
  check("pole: the larger-magnitude sample (60) is nulled", out[3] === null && out[2] === -50);
  check("pole: input is not mutated", ys[3] === 60);
  check("pole: continuous samples survive", out[0] === -2 && out[5] === 0.5);
}

// A steep BUT continuous segment: sign change but a modest jump (< 8× span).
// Must NOT be broken (that would tear a normal curve).
{
  const ys = [-3, -1, 4, 6]; // jump 5 over a span-10 view → not a pole
  const out = breakDiscontinuities(ys, 10);
  check("steep-continuous: not broken", nullsIn(out) === 0);
}

// Same-sign spike (1/x²): both sides large positive, no sign change → left as-is.
{
  const ys = [1, 100, 120, 1];
  const out = breakDiscontinuities(ys, 10);
  check("same-sign spike: left intact", nullsIn(out) === 0);
}

// Existing nulls (out-of-domain / non-finite) are ignored, not crashed on.
{
  const ys = [1, null, -80, 90, 2];
  const out = breakDiscontinuities(ys, 10);
  check("existing nulls: preserved and pole still broken", out[1] === null && nullsIn(out) === 2);
}

// Multiple poles (tan over several periods) are each broken.
{
  const ys = [1, 90, -90, 1, 88, -88, 1];
  const out = breakDiscontinuities(ys, 10);
  check("multiple poles: each connector broken", nullsIn(out) === 2);
}

// A degenerate span never throws and never breaks anything.
{
  const out = breakDiscontinuities([-50, 60], 0);
  check("zero span: no-op (no break)", nullsIn(out) === 0);
}

console.log(`\n${failed === 0 ? "ALL PASS" : "FAILED"}: ${passed} passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
