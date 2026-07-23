// Pure slider stepping/animation math for the grapher's play button.
// Kept side-effect-free (no timers, no DOM) so the ping-pong / snap behavior is
// unit-tested (tools/graph_animate_test.mjs); GraphCalculator owns the interval
// loop and the override write-through.

/** Snap a value to the nearest `lo + k·step` grid point, clamped to [lo, hi]. */
export function snapToStep(value: number, lo: number, hi: number, step: number | null): number {
  if (!step || !Number.isFinite(step) || step <= 0) return clamp(value, lo, hi);
  const k = Math.round((value - lo) / step);
  return clamp(lo + k * step, lo, hi);
}

function clamp(v: number, lo: number, hi: number): number {
  return v < lo ? lo : v > hi ? hi : v;
}

export interface AnimState {
  /** Raw (unsnapped) sweep position — accumulates across ticks. */
  value: number;
  dir: 1 | -1;
}

/**
 * Advance an animating slider one tick, ping-ponging within [lo, hi] at a
 * constant rate (the whole range takes `secondsPerSweep`). The returned `value`
 * is the raw sweep position; the caller snaps it to any step grid at emit time
 * (`snapToStep`) so a stepped slider dwells on each grid point for a visible
 * moment rather than racing through it. At a bound the value clamps exactly and
 * the direction reverses, so the motion oscillates rather than jumping.
 */
export function advance(
  s: AnimState,
  lo: number,
  hi: number,
  dtMs: number,
  secondsPerSweep = 4,
): AnimState {
  if (!(hi > lo)) return { value: clamp(s.value, lo, hi), dir: s.dir };
  const span = hi - lo;
  const delta = (span * dtMs) / (secondsPerSweep * 1000);
  let next = s.value + s.dir * delta;
  let dir = s.dir;
  if (next >= hi) {
    next = hi;
    dir = -1;
  } else if (next <= lo) {
    next = lo;
    dir = 1;
  }
  return { value: next, dir };
}
