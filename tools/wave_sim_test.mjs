// Unit tests for the 2-D wave FDTD engine (web/src/lib/wave/sim.ts).
//
// No DOM, no build: Node 24 strips the TS types and imports the engine
// directly, matching the repo's tools/*.mjs convention (wasm_smoke.mjs).
//
//   node tools/wave_sim_test.mjs
import { WaveSim, MAX_COURANT } from "../web/src/lib/wave/sim.ts";
import { magnitudeSpectrum, fftRadix2 } from "../web/src/lib/wave/spectrum.ts";

let pass = 0;
let fail = 0;
function check(name, cond, extra = "") {
  if (cond) {
    pass++;
    console.log(`PASS  ${name}`);
  } else {
    fail++;
    console.log(`FAIL  ${name}${extra ? `  [${extra}]` : ""}`);
  }
}

// --- helpers ---------------------------------------------------------------

/** Discrete leapfrog angular frequency for mode (m, n) on an nx×ny grid. */
function discreteOmega(sim, m, n) {
  const kx = (m * Math.PI) / (sim.nx - 1);
  const ky = (n * Math.PI) / (sim.ny - 1);
  const s =
    sim.courant *
    Math.sqrt(Math.sin(kx / 2) ** 2 + Math.sin(ky / 2) ** 2);
  return 2 * Math.asin(Math.min(1, s));
}

/** Seed the exact discrete standing-wave eigenmode u = mode·cos(ω·t). */
function seedMode(sim, m, n) {
  const w = discreteOmega(sim, m, n);
  const cw = Math.cos(w); // one step in the past
  for (let j = 0; j < sim.ny; j++) {
    for (let i = 0; i < sim.nx; i++) {
      const s =
        Math.sin((m * Math.PI * i) / (sim.nx - 1)) *
        Math.sin((n * Math.PI * j) / (sim.ny - 1));
      const k = sim.idx(i, j);
      sim.cur[k] = s;
      sim.prev[k] = s * cw;
    }
  }
  return w;
}

function l2(a, b) {
  let s = 0;
  for (let k = 0; k < a.length; k++) {
    const d = a[k] - b[k];
    s += d * d;
  }
  return Math.sqrt(s / a.length);
}

/** Energy-weighted centroid x of the current field. */
function energyCentroidX(sim) {
  let sum = 0;
  let cx = 0;
  for (let j = 0; j < sim.ny; j++)
    for (let i = 0; i < sim.nx; i++) {
      const e = sim.cur[sim.idx(i, j)] ** 2;
      sum += e;
      cx += e * i;
    }
  return sum > 0 ? cx / sum : (sim.nx - 1) / 2;
}

/** Ratio of gradient energy to displacement energy — a spatial-frequency proxy. */
function gradRatio(sim) {
  let grad = 0;
  let disp = 0;
  for (let j = 0; j < sim.ny; j++)
    for (let i = 0; i < sim.nx; i++) {
      const k = sim.idx(i, j);
      disp += sim.cur[k] ** 2;
      if (i < sim.nx - 1) grad += (sim.cur[k + 1] - sim.cur[k]) ** 2;
      if (j < sim.ny - 1) grad += (sim.cur[k + sim.nx] - sim.cur[k]) ** 2;
    }
  return disp > 1e-12 ? grad / disp : 0;
}

// --- 1. stability: no blow-up over many steps ------------------------------
{
  const sim = new WaveSim(80, 60, { speed: 1, damping: 0, boundary: "fixed" });
  sim.poke(40, 30, 5, 1);
  sim.poke(20, 15, 3, -0.7);
  for (let k = 0; k < 4000; k++) sim.step();
  const m = sim.maxAbs();
  check(
    "stable over 4000 steps at max speed (no blow-up / NaN)",
    Number.isFinite(m) && m < 10,
    `maxAbs=${m}`,
  );
}

// --- 2. CFL clamp: speed=1 stays below the 1/√2 limit ----------------------
{
  const sim = new WaveSim(40, 40, { speed: 1 });
  check(
    "Courant clamped below the 2-D CFL limit 1/√2",
    sim.courant <= MAX_COURANT + 1e-9 && MAX_COURANT < Math.SQRT1_2,
    `courant=${sim.courant}, max=${MAX_COURANT}`,
  );
  // Even an out-of-range requested speed is clamped, not honored.
  sim.speed = 5;
  check("speed setter clamps to [0,1]", sim.speed === 1);
}

// --- 3. discrete eigenmode: exact phase evolution --------------------------
// The FDTD scheme's standing-wave eigenmodes evolve as cos(ω·t) with the
// DISCRETE ω. Seeding one and stepping N times must reproduce mode·cos(ωN)
// to near float precision — the strongest possible check on the stencil.
{
  const sim = new WaveSim(97, 97, { speed: 0.6, damping: 0, boundary: "fixed" });
  const w = seedMode(sim, 2, 3);
  const N = 120;
  const mode = new Float32Array(sim.nx * sim.ny);
  for (let j = 0; j < sim.ny; j++)
    for (let i = 0; i < sim.nx; i++)
      mode[sim.idx(i, j)] =
        Math.sin((2 * Math.PI * i) / (sim.nx - 1)) *
        Math.sin((3 * Math.PI * j) / (sim.ny - 1));
  sim.step(N);
  const expected = new Float32Array(mode.length);
  const c = Math.cos(w * N);
  for (let k = 0; k < mode.length; k++) expected[k] = mode[k] * c;
  const err = l2(sim.cur, expected);
  check(
    "discrete eigenmode matches analytic cos(ωt) evolution",
    err < 5e-3,
    `L2=${err.toExponential(2)}`,
  );
}

// --- 4. energy conservation (lossless) vs. dissipation (damped) ------------
{
  const a = new WaveSim(90, 90, { speed: 0.6, damping: 0, boundary: "fixed" });
  a.poke(45, 45, 6, 1);
  const e0 = a.energy();
  let emin = e0;
  let emax = e0;
  for (let k = 0; k < 600; k++) {
    a.step();
    const e = a.energy();
    emin = Math.min(emin, e);
    emax = Math.max(emax, e);
  }
  check(
    "lossless energy bounded (drift < 3%)",
    (emax - emin) / e0 < 0.03,
    `drift=${(((emax - emin) / e0) * 100).toFixed(2)}%`,
  );

  const b = new WaveSim(90, 90, { speed: 0.6, damping: 0.2, boundary: "fixed" });
  b.poke(45, 45, 6, 1);
  const be0 = b.energy();
  for (let k = 0; k < 600; k++) b.step();
  check("damping strictly dissipates energy", b.energy() < 0.2 * be0, `E/E0=${(b.energy() / be0).toFixed(3)}`);
}

// --- 5. absorbing boundary removes energy from the window ------------------
{
  const abs = new WaveSim(120, 120, { speed: 0.6, damping: 0, boundary: "absorbing" });
  abs.poke(60, 60, 5, 1);
  const e0 = abs.energy();
  for (let k = 0; k < 500; k++) abs.step();
  check(
    "absorbing boundary radiates energy away",
    abs.energy() < 0.1 * e0,
    `E/E0=${(abs.energy() / e0).toFixed(3)}`,
  );
}

// --- 6. fixed boundary holds the walls at zero -----------------------------
{
  const sim = new WaveSim(60, 50, { speed: 0.6, boundary: "fixed" });
  sim.poke(5, 5, 4, 1); // poke near a corner
  for (let k = 0; k < 200; k++) sim.step();
  let wall = 0;
  for (let i = 0; i < sim.nx; i++) {
    wall = Math.max(wall, Math.abs(sim.cur[sim.idx(i, 0)]));
    wall = Math.max(wall, Math.abs(sim.cur[sim.idx(i, sim.ny - 1)]));
  }
  check("fixed boundary keeps wall displacement at 0", wall < 1e-6, `wall=${wall}`);
}

// --- 7. symmetry: a centered pluck stays left–right symmetric --------------
{
  const sim = new WaveSim(81, 81, { speed: 0.6, damping: 0, boundary: "fixed" });
  sim.poke(40, 40, 6, 1); // exact center of an odd grid
  for (let k = 0; k < 150; k++) sim.step();
  let asym = 0;
  for (let j = 0; j < sim.ny; j++)
    for (let i = 0; i < sim.nx; i++) {
      const m = sim.cur[sim.idx(i, j)] - sim.cur[sim.idx(sim.nx - 1 - i, j)];
      asym = Math.max(asym, Math.abs(m));
    }
  check("centered pluck stays mirror-symmetric", asym < 1e-4, `asym=${asym.toExponential(2)}`);
}

// --- 8. directional launch: energy TRANSLATES along the drag ---------------
// The d'Alembert launch must move the energy centroid, not just make an
// antisymmetric splash (an odd velocity dipole is P-invariant under the
// symmetric stencil and stays centered — so this asserts real translation
// AND that a reversed drag goes the other way).
{
  const R = new WaveSim(200, 120, { speed: 0.6, damping: 0, boundary: "absorbing" });
  R.launch(100, 60, 6, 1, 1, 0); // rightward
  for (let k = 0; k < 80; k++) R.step();
  const cR = energyCentroidX(R);

  const L = new WaveSim(200, 120, { speed: 0.6, damping: 0, boundary: "absorbing" });
  L.launch(100, 60, 6, 1, -1, 0); // leftward
  for (let k = 0; k < 80; k++) L.step();
  const cL = energyCentroidX(L);

  check("rightward drag moves energy right", cR > 108, `cx=${cR.toFixed(1)} (start 100)`);
  check("leftward drag moves energy left", cL < 92, `cx=${cL.toFixed(1)} (start 100)`);
  check("launch is genuinely directional (right ≠ left)", cR - cL > 25, `cR-cL=${(cR - cL).toFixed(1)}`);
}

// --- 8b. mouse frequency: shorter wavelength = higher spatial frequency -----
{
  const long = new WaveSim(140, 140, { speed: 0.6, boundary: "fixed" });
  long.poke(70, 70, 8, 1, 40);
  const short = new WaveSim(140, 140, { speed: 0.6, boundary: "fixed" });
  short.poke(70, 70, 8, 1, 6);
  check(
    "shorter-wavelength poke carries higher spatial frequency",
    gradRatio(short) > 2 * gradRatio(long),
    `short=${gradRatio(short).toFixed(3)} long=${gradRatio(long).toFixed(4)}`,
  );
}

// --- 8c. driven source pumps energy into the field -------------------------
{
  const sim = new WaveSim(120, 120, { speed: 0.6, damping: 0, boundary: "absorbing" });
  sim.setSource(60, 60, 0.03, 0.3, 3);
  for (let k = 0; k < 200; k++) sim.step();
  const e = sim.energy();
  sim.clearSource();
  check("driven source injects energy over time", e > 0.05 && sim.maxAbs() > 0, `E=${e.toFixed(3)}`);
}

// --- 8d. filtered boundary: reflect=0 absorbs; lowpass absorbs highs more ---
{
  const open = new WaveSim(120, 120, { speed: 0.6, boundary: "filtered" });
  open.setBoundaryFilter("lowpass", 0.5, 0);
  open.poke(60, 60, 5, 1);
  const e0 = open.energy();
  for (let k = 0; k < 400; k++) open.step();
  check("filtered reflect=0 absorbs (like an open edge)", open.energy() < 0.1 * e0, `E/E0=${(open.energy() / e0).toFixed(3)}`);

  const retained = (wavelength) => {
    const s = new WaveSim(100, 100, { speed: 0.6, boundary: "filtered" });
    s.setBoundaryFilter("lowpass", 0.3, 0.9);
    s.poke(50, 50, 6, 1, wavelength);
    const start = s.energy();
    for (let k = 0; k < 700; k++) s.step();
    return s.energy() / start;
  };
  const lowKept = retained(28); // long wavelength = low frequency
  const highKept = retained(5); // short wavelength = high frequency
  check(
    "lowpass boundary absorbs highs more than lows",
    lowKept > highKept + 0.05,
    `low=${lowKept.toFixed(2)} high=${highKept.toFixed(2)}`,
  );

  const stab = new WaveSim(100, 100, { speed: 1, boundary: "filtered" });
  stab.setBoundaryFilter("highpass", 0.6, 1);
  stab.poke(50, 50, 5, 1);
  for (let k = 0; k < 3000; k++) stab.step();
  check("filtered boundary stable at max speed", Number.isFinite(stab.maxAbs()) && stab.maxAbs() < 10, `maxAbs=${stab.maxAbs()}`);
}

// --- 9. Robin boundary interpolates between free and fixed -----------------
// k → 0 must behave like a free edge (wall not clamped, energy retained);
// large k must behave like a fixed edge (wall ≈ 0).
{
  const soft = new WaveSim(70, 70, { speed: 0.6, boundary: "robin", robin: 0 });
  soft.poke(35, 35, 6, 1);
  const e0 = soft.energy();
  for (let k = 0; k < 300; k++) soft.step();
  let wallSoft = 0;
  for (let i = 0; i < soft.nx; i++)
    wallSoft = Math.max(wallSoft, Math.abs(soft.cur[soft.idx(i, 0)]));
  check("Robin k=0 behaves like a free edge (wall moves, energy kept)",
    wallSoft > 1e-3 && soft.energy() > 0.5 * e0,
    `wall=${wallSoft.toExponential(2)}, E/E0=${(soft.energy() / e0).toFixed(2)}`);

  const stiff = new WaveSim(70, 70, { speed: 0.6, boundary: "robin", robin: 1e6 });
  stiff.poke(35, 35, 6, 1);
  for (let k = 0; k < 300; k++) stiff.step();
  let wallStiff = 0;
  for (let i = 0; i < stiff.nx; i++)
    wallStiff = Math.max(wallStiff, Math.abs(stiff.cur[stiff.idx(i, 0)]));
  check("Robin k→∞ behaves like a fixed edge (wall ≈ 0)", wallStiff < 1e-4, `wall=${wallStiff.toExponential(2)}`);

  // A reactive (real-k) Robin edge is lossless: energy stays bounded.
  const mid = new WaveSim(70, 70, { speed: 0.6, boundary: "robin", robin: 1 });
  mid.poke(35, 35, 6, 1);
  const me0 = mid.energy();
  let mmax = me0;
  for (let k = 0; k < 400; k++) { mid.step(); mmax = Math.max(mmax, mid.energy()); }
  check("Robin edge is lossless (energy bounded)", mmax < 1.05 * me0 && mid.energy() > 0.4 * me0,
    `Emax/E0=${(mmax / me0).toFixed(2)}, E/E0=${(mid.energy() / me0).toFixed(2)}`);
}

// --- 10. reset clears the field --------------------------------------------
{
  const sim = new WaveSim(40, 40);
  sim.poke(20, 20, 5, 1);
  sim.step(10);
  sim.reset();
  check("reset zeroes the field and energy", sim.maxAbs() === 0 && sim.energy() === 0);
}

// --- 11. obstacles: a solid wall holds zero and blocks the far side --------
{
  const sim = new WaveSim(80, 60, { speed: 0.6, boundary: "fixed" });
  const wx = 40;
  sim.setSolid((i) => i === wx || i === wx + 1); // full-height 2-cell barrier
  sim.poke(20, 30, 5, 1); // energy on the left only
  for (let k = 0; k < 150; k++) sim.step();
  let wallMax = 0;
  let right = 0;
  let left = 0;
  for (let j = 0; j < sim.ny; j++) {
    for (let i = 0; i < sim.nx; i++) {
      const a = Math.abs(sim.cur[sim.idx(i, j)]);
      if (i === wx || i === wx + 1) wallMax = Math.max(wallMax, a);
      else if (i > wx + 1) right = Math.max(right, a);
      else left = Math.max(left, a);
    }
  }
  check(
    "solid wall holds zero and decouples the two halves",
    wallMax === 0 && right === 0 && left > 1e-2,
    `wall=${wallMax}, right=${right}, left=${left.toExponential(2)}`,
  );
}

// --- 12. variable media: stable, bounded, and reads back -------------------
{
  const sim = new WaveSim(80, 60, { speed: 0.65, boundary: "fixed" });
  sim.setMedium((i) => (i > 40 ? 0.5 : 1)); // slow right half
  sim.poke(20, 30, 5, 1);
  let peak = 0;
  for (let k = 0; k < 600; k++) {
    sim.step();
    peak = Math.max(peak, sim.maxAbs());
  }
  check(
    "variable media stays stable and bounded",
    Number.isFinite(peak) && peak < 3 && sim.maxAbs() > 5e-3,
    `peak=${peak.toFixed(3)}`,
  );
  sim.setMedium(() => 0.01); // below MIN_SCALE
  check(
    "medium reads back, clamped to [MIN_SCALE, 1]",
    Math.abs(sim.scaleAt(10, 30) - 0.2) < 1e-6,
    `scale=${sim.scaleAt(10, 30)}`,
  );
}

// --- 13. scene lifecycle: hasScene / clearScene ----------------------------
{
  const sim = new WaveSim(40, 40);
  sim.setSolid((i) => i === 20);
  sim.setMedium(() => 0.5);
  check("hasScene true once a medium/obstacle is set", sim.hasScene === true);
  sim.clearScene();
  check(
    "clearScene restores a uniform, obstacle-free medium",
    sim.hasScene === false && sim.scaleAt(20, 20) === 1 && !sim.isSolid(20, 20),
  );
}

// --- 14. probes: record u(t) and read it back chronologically -------------
{
  const sim = new WaveSim(60, 40, { speed: 0.6, boundary: "fixed" });
  sim.setMeasuring(true);
  const id = sim.addProbe(30, 20);
  check("addProbe returns an index and bumps the count", id === 0 && sim.probeCount === 1);
  // No samples until a step runs.
  check("probe series is empty before stepping", sim.probeSeries(0).length === 0);
  sim.setSource(30, 20, 0.05, 0.4, 2);
  for (let k = 0; k < 300; k++) sim.step();
  const series = sim.probeSeries(0);
  check(
    "probe records a non-trivial time-series at its cell",
    series.length > 100 && series.some((v) => Math.abs(v) > 1e-3),
    `len=${series.length}`,
  );
  // A probe far from the (fixed-wall-)decoupled source region still exists.
  sim.clearProbes();
  check("clearProbes empties the probe set", sim.probeCount === 0);
}

// --- 15. probe ring keeps only the most recent PROBE_CAP samples -----------
{
  const sim = new WaveSim(30, 30, { speed: 0.6, boundary: "free" });
  sim.setMeasuring(true);
  sim.addProbe(15, 15);
  sim.poke(15, 15, 4, 1);
  const cap = WaveSim.PROBE_CAP;
  for (let k = 0; k < cap + 500; k++) sim.step();
  const series = sim.probeSeries(0);
  check("probe ring caps at PROBE_CAP samples", series.length === cap, `len=${series.length}`);
  // Chronological order: last sample equals the current field value at the cell.
  const last = series[series.length - 1];
  check("probe series ends at the current sample", Math.abs(last - sim.cur[sim.idx(15, 15)]) < 1e-6);
}

// --- 16. intensity field: time-averaged ⟨u²⟩, resettable -------------------
{
  const sim = new WaveSim(80, 60, { speed: 0.6, damping: 0, boundary: "fixed" });
  sim.setMeasuring(true);
  sim.setSource(25, 30, 0.05, 0.5, 3);
  for (let k = 0; k < 500; k++) sim.step();
  const I = sim.intensityField();
  const near = I[sim.idx(25, 30)];
  const far = I[sim.idx(70, 30)];
  check(
    "intensity accumulates and is largest near the source",
    sim.intensitySamples === 500 && near > 0 && near > far,
    `near=${near.toExponential(2)} far=${far.toExponential(2)}`,
  );
  // The running mean must equal the true mean of u² for a probed cell.
  sim.resetIntensity();
  check("resetIntensity clears the average", sim.intensitySamples === 0 && sim.intensityField()[0] === 0);
  const probeCell = sim.idx(40, 30);
  let sum = 0;
  const M = 200;
  for (let k = 0; k < M; k++) {
    sim.step();
    sum += sim.cur[probeCell] ** 2;
  }
  const trueMean = sum / M;
  const got = sim.intensityField()[probeCell];
  check(
    "intensity running mean matches the true time-average of u²",
    Math.abs(got - trueMean) < 1e-6 * Math.max(1, trueMean) + 1e-9,
    `got=${got.toExponential(3)} true=${trueMean.toExponential(3)}`,
  );
}

// --- 17. measuring flag gates the cost (no probes recorded when off) -------
{
  const sim = new WaveSim(40, 40, { speed: 0.6 });
  sim.addProbe(20, 20); // measuring is OFF by default
  sim.poke(20, 20, 4, 1);
  for (let k = 0; k < 50; k++) sim.step();
  check("no recording while measuring is off", sim.probeSeries(0).length === 0 && sim.intensitySamples === 0);
}

// --- 18. FFT: inverse round-trips a known signal ---------------------------
{
  const N = 64;
  const re = new Float32Array(N);
  const im = new Float32Array(N);
  const orig = new Float32Array(N);
  for (let k = 0; k < N; k++) {
    orig[k] = Math.sin((2 * Math.PI * 5 * k) / N) + 0.4 * Math.cos((2 * Math.PI * 11 * k) / N);
    re[k] = orig[k];
  }
  fftRadix2(re, im, -1); // forward
  fftRadix2(re, im, +1); // inverse (unscaled)
  let err = 0;
  for (let k = 0; k < N; k++) err = Math.max(err, Math.abs(re[k] / N - orig[k]));
  check("FFT forward∘inverse round-trips (÷N)", err < 1e-5, `err=${err.toExponential(2)}`);
}

// --- 19. spectrum: a pure sinusoid peaks at its frequency ------------------
{
  const N = 512;
  const f = 0.08; // cycles/step
  const sig = new Float32Array(N);
  for (let k = 0; k < N; k++) sig[k] = Math.sin(2 * Math.PI * f * k);
  const spec = magnitudeSpectrum(sig);
  check(
    "spectrum peak lands on the sinusoid frequency",
    Math.abs(spec.peakFreq - f) < 1.5 / spec.n,
    `peak=${spec.peakFreq.toFixed(4)} want=${f}`,
  );
  // Mean-offset (DC) must not create the peak — detrending handles it.
  const sig2 = new Float32Array(N);
  for (let k = 0; k < N; k++) sig2[k] = 3 + Math.sin(2 * Math.PI * f * k);
  const spec2 = magnitudeSpectrum(sig2);
  check("spectrum ignores a DC offset", Math.abs(spec2.peakFreq - f) < 1.5 / spec2.n, `peak=${spec2.peakFreq.toFixed(4)}`);
  check("spectrum of a too-short signal is empty", magnitudeSpectrum(new Float32Array(4)).freq.length === 0);
}

// --- 20. end-to-end: a driven source's probe spectrum recovers its freq ----
{
  const sim = new WaveSim(120, 90, { speed: 0.6, damping: 0.02, boundary: "absorbing" });
  sim.setMeasuring(true);
  const driveFreq = 0.06; // cycles/step
  sim.setSource(60, 45, driveFreq, 0.5, 3);
  sim.addProbe(80, 45); // downstream of the source
  for (let k = 0; k < 1500; k++) sim.step();
  const spec = magnitudeSpectrum(sim.probeSeries(0));
  check(
    "driven-source probe spectrum recovers the drive frequency",
    Math.abs(spec.peakFreq - driveFreq) < 3 / spec.n,
    `peak=${spec.peakFreq.toFixed(4)} drive=${driveFreq}`,
  );
}

console.log(`\n${fail === 0 ? "ALL PASS" : "FAILURES"}: ${pass} passed, ${fail} failed`);
process.exit(fail === 0 ? 0 : 1);
