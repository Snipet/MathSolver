// Unit tests for the slider stepping/animation math (web/src/lib/graph/animate.ts).
import { snapToStep, advance } from "../web/src/lib/graph/animate.ts";

let pass = 0, fail = 0;
const check = (n, c) => { if (c) { pass++; console.log("PASS  " + n); } else { fail++; console.log("FAIL  " + n); } };
const near = (a, b) => Math.abs(a - b) < 1e-9;

// --- snapToStep ------------------------------------------------------------
check("snap none → clamp only", snapToStep(3.7, 0, 10, null) === 3.7);
check("snap clamps above hi", snapToStep(99, 0, 10, null) === 10);
check("snap clamps below lo", snapToStep(-5, 0, 10, null) === 0);
check("snap to integer grid (down)", snapToStep(2.4, 0, 10, 1) === 2);
check("snap to integer grid (up)", snapToStep(2.6, 0, 10, 1) === 3);
check("snap to half grid off a nonzero lo", near(snapToStep(1.24, 1, 3, 0.5), 1));
check("snap respects lo offset", near(snapToStep(1.8, 1, 3, 0.5), 2));
check("snap never exceeds hi", snapToStep(10.9, 0, 10, 3) === 10);

// --- advance: constant-rate sweep ------------------------------------------
{
  // Over a 4s sweep, a 1s tick moves a quarter of the [0,4] span (= 1).
  const s = advance({ value: 0, dir: 1 }, 0, 4, 1000);
  check("quarter-sweep per second", near(s.value, 1) && s.dir === 1);
}
{
  // Overshooting the top clamps to hi and reverses.
  const s = advance({ value: 3.9, dir: 1 }, 0, 4, 1000);
  check("bounces at hi", s.value === 4 && s.dir === -1);
}
{
  const s = advance({ value: 0.1, dir: -1 }, 0, 4, 1000);
  check("bounces at lo", s.value === 0 && s.dir === 1);
}
{
  // A degenerate range never moves.
  const s = advance({ value: 2, dir: 1 }, 5, 5, 16);
  check("degenerate range holds", s.value === 5);
}

// --- advance + snap: an integer step dwells on each grid point --------------
{
  // A [0,5] sweep in 4s at ~30fps: the SNAPPED value should count 0,1,2,…,5
  // and then reverse, spending several ticks on each integer (not race through).
  let st = { value: 0, dir: 1 };
  const emitted = [];
  let guard = 0;
  while (guard++ < 2000) {
    const e = snapToStep(st.value, 0, 5, 1);
    if (emitted[emitted.length - 1] !== e) emitted.push(e);
    if (emitted.length >= 6 && emitted[emitted.length - 1] === 5) break;
    st = advance(st, 0, 5, 32);
  }
  check("stepped sweep reveals every integer in order",
    JSON.stringify(emitted) === JSON.stringify([0, 1, 2, 3, 4, 5]));
  // At 32ms/tick over ~4s the 0→5 sweep is ~125 ticks, so each of the 6 grid
  // points is held for many ticks — confirm it isn't one-tick-per-step.
  check("stepped sweep is watchable (many ticks per step)", guard > 60);
}

console.log(`\n${fail === 0 ? "ALL PASS" : "FAILURES"}: ${pass} passed, ${fail} failed`);
process.exit(fail === 0 ? 0 : 1);
