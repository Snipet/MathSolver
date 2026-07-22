// Unit tests for the 2-D wave FDTD engine (web/src/lib/wave/sim.ts).
//
// No DOM, no build: Node 24 strips the TS types and imports the engine
// directly, matching the repo's tools/*.mjs convention (wasm_smoke.mjs).
//
//   node tools/wave_sim_test.mjs
import {
  WaveSim,
  MAX_COURANT,
  MAX_MASS,
  MAX_COUPLING_SINE,
  besselJ,
  BESSEL_ZEROS,
  drumGeom,
} from "../web/src/lib/wave/sim.ts";
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

// --- 21. Klein–Gordon: exact discrete eigenmode with the mass term ---------
// Adding mass m shifts the discrete dispersion to cos ω = cos ω₀ − m/2, so a
// massive standing mode still evolves as mode·cos(ωt) — the strongest check on
// the reaction term. It also proves DISPERSION: the massive ω exceeds ω₀.
{
  const sim = new WaveSim(97, 97, { speed: 0.6, damping: 0, boundary: "fixed" });
  sim.model = "klein-gordon";
  const m = 0.25;
  sim.mass = m;
  const w0 = discreteOmega(sim, 2, 3); // massless discrete frequency
  const w = Math.acos(Math.max(-1, Math.min(1, Math.cos(w0) - m / 2)));
  const mode = new Float32Array(sim.nx * sim.ny);
  for (let j = 0; j < sim.ny; j++)
    for (let i = 0; i < sim.nx; i++)
      mode[sim.idx(i, j)] =
        Math.sin((2 * Math.PI * i) / (sim.nx - 1)) *
        Math.sin((3 * Math.PI * j) / (sim.ny - 1));
  for (let k = 0; k < mode.length; k++) {
    sim.cur[k] = mode[k];
    sim.prev[k] = mode[k] * Math.cos(w);
  }
  const N = 120;
  sim.step(N);
  const expected = new Float32Array(mode.length);
  const c = Math.cos(w * N);
  for (let k = 0; k < mode.length; k++) expected[k] = mode[k] * c;
  check(
    "Klein–Gordon mass shifts the eigenmode frequency exactly (dispersion)",
    l2(sim.cur, expected) < 5e-3 && w > w0 + 1e-3,
    `L2=${l2(sim.cur, expected).toExponential(2)} ω=${w.toFixed(3)} ω₀=${w0.toFixed(3)}`,
  );
}

// --- 22. Klein–Gordon rest frequency: the k=0 mode oscillates at √m ---------
// A spatially uniform field has ∇²u = 0, so u_tt = −m·u — it rings at the rest
// frequency ω = acos(1 − m/2) with no spatial propagation at all.
{
  const m = 0.4;
  const sim = new WaveSim(24, 24, { speed: 1, damping: 0, boundary: "free" });
  sim.model = "klein-gordon";
  sim.mass = m;
  const w = Math.acos(1 - m / 2);
  sim.cur.fill(1);
  sim.prev.fill(Math.cos(w));
  const N = 60;
  sim.step(N);
  const got = sim.cur[sim.idx(12, 12)];
  check(
    "Klein–Gordon rest mode rings at ω = acos(1 − m/2)",
    Math.abs(got - Math.cos(w * N)) < 1e-3,
    `got=${got.toFixed(5)} want=${Math.cos(w * N).toFixed(5)}`,
  );
}

// --- 23. sine-Gordon: nonlinear (≈ KG for small u, differs for large u) -----
{
  const run = (model, amp) => {
    const s = new WaveSim(60, 60, { speed: 0.7, damping: 0, boundary: "fixed" });
    s.model = model;
    s.mass = 0.3;
    for (let j = 0; j < s.ny; j++)
      for (let i = 0; i < s.nx; i++)
        s.cur[s.idx(i, j)] = s.prev[s.idx(i, j)] =
          amp * Math.sin((Math.PI * i) / (s.nx - 1)) * Math.sin((Math.PI * j) / (s.ny - 1));
    s.step(80);
    return s.cur;
  };
  const small = l2(run("sine-gordon", 0.02), run("klein-gordon", 0.02));
  const large = l2(run("sine-gordon", 1.6), run("klein-gordon", 1.6));
  check(
    "sine-Gordon ≈ Klein–Gordon for small u but nonlinear for large u",
    small < 1e-3 && large > 30 * (small + 1e-6),
    `small=${small.toExponential(2)} large=${large.toExponential(2)}`,
  );
}

// --- 23b. sine-Gordon kink soliton: a 2π twist that translates, stays bound -
{
  const sim = new WaveSim(140, 44, { speed: 1, damping: 0, boundary: "absorbing" });
  sim.seedKink(0.25); // rightward
  const mid = 22;
  const twistOK = (s) => {
    const left = s.cur[s.idx(2, mid)];
    const right = s.cur[s.idx(s.nx - 3, mid)];
    let mono = true;
    for (let i = 2; i < s.nx - 2; i++)
      if (s.cur[s.idx(i, mid)] < s.cur[s.idx(i - 1, mid)] - 1e-3) mono = false;
    return { left, right, mono };
  };
  const centroid = (s) => {
    let sum = 0;
    let w = 0;
    for (let i = 1; i < s.nx - 1; i++) {
      const g = Math.abs(s.cur[s.idx(i, mid)] - s.cur[s.idx(i - 1, mid)]);
      sum += g * i;
      w += g;
    }
    return sum / w;
  };
  const c0 = centroid(sim);
  let peak = 0;
  for (let k = 0; k < 500; k++) {
    sim.step();
    peak = Math.max(peak, sim.maxAbs());
  }
  const t = twistOK(sim);
  const c1 = centroid(sim);
  check(
    "sine-Gordon kink is a stable 2π twist that glides rightward",
    t.mono &&
      Math.abs(t.left) < 0.2 &&
      Math.abs(t.right - 2 * Math.PI) < 0.2 &&
      c1 - c0 > 4 &&
      peak < 8,
    `ends ${t.left.toFixed(2)}..${t.right.toFixed(2)} mono=${t.mono} moved=${(c1 - c0).toFixed(1)} peak=${peak.toFixed(2)}`,
  );
}

// --- 24. 9-point Laplacian is more isotropic than the 5-point (at matched κ) -
{
  const anisotropy = (stencil) => {
    const N = 161;
    const c = (N - 1) / 2;
    const s = new WaveSim(N, N, { speed: 1, damping: 0, boundary: "absorbing" });
    s.stencil = stencil;
    s.speed = 0.5 / s.maxCourant(); // match the wave speed (κ = 0.5) across stencils
    s.poke(c, c, 3, 1, 9);
    for (let k = 0; k < 90; k++) s.step();
    let R = 10;
    let best = 0;
    for (let r = 8; r < 70; r++) {
      const v = Math.abs(s.cur[s.idx(c + r, c)]);
      if (v > best) {
        best = v;
        R = r;
      }
    }
    const M = 72;
    const vals = [];
    for (let a = 0; a < M; a++) {
      const th = (2 * Math.PI * a) / M;
      vals.push(Math.abs(s.cur[s.idx(Math.round(c + R * Math.cos(th)), Math.round(c + R * Math.sin(th)))]));
    }
    const mean = vals.reduce((x, v) => x + v, 0) / M;
    return Math.sqrt(vals.reduce((x, v) => x + (v - mean) ** 2, 0) / M) / mean;
  };
  const cv5 = anisotropy("five");
  const cv9 = anisotropy("nine");
  check(
    "9-point stencil halves the wavefront anisotropy vs the 5-point",
    cv9 < 0.7 * cv5,
    `CV5=${cv5.toFixed(4)} CV9=${cv9.toFixed(4)}`,
  );
}

// --- 25. stability & CFL: every model × stencil stays bounded, κ ≤ limit ----
{
  let bounded = true;
  let detail = "";
  for (const model of ["linear", "klein-gordon", "sine-gordon"]) {
    for (const stencil of ["five", "nine"]) {
      const s = new WaveSim(70, 70, { speed: 1, damping: 0.02, boundary: "fixed" });
      s.model = model;
      s.stencil = stencil;
      s.mass = MAX_MASS; // clamped per model
      const cfl = stencil === "nine" ? Math.sqrt(4 / (16 / 3)) : Math.SQRT1_2;
      if (s.maxCourant() > cfl + 1e-9) {
        bounded = false;
        detail = `${model}/${stencil} κ=${s.maxCourant().toFixed(3)} > CFL ${cfl.toFixed(3)}`;
      }
      s.poke(35, 35, 6, 1.2);
      let mx = 0;
      for (let k = 0; k < 2500; k++) {
        s.step();
        mx = Math.max(mx, s.maxAbs());
      }
      if (!(Number.isFinite(mx) && mx < 10)) {
        bounded = false;
        detail = `${model}/${stencil} maxAbs=${mx}`;
      }
    }
  }
  check("all models × stencils stay bounded and below their CFL", bounded, detail);
}

// --- 26. reaction coupling is clamped into each model's stable range --------
{
  const s = new WaveSim(10, 10);
  s.model = "klein-gordon";
  s.mass = 99;
  check("Klein–Gordon mass clamps to MAX_MASS", s.mass === MAX_MASS);
  s.model = "sine-gordon";
  check("switching to sine-Gordon re-clamps the coupling", s.mass === MAX_COUPLING_SINE);
  s.mass = 99;
  check("sine-Gordon coupling clamps to MAX_COUPLING_SINE", s.mass === MAX_COUPLING_SINE);
  check("the caps are ordered (nonlinear coupling is held lower)", MAX_COUPLING_SINE < MAX_MASS);
}

// --- 27. Bessel J_m: known values and zeros --------------------------------
{
  const j00 = besselJ(0, 0);
  const j10 = besselJ(1, 0);
  const atZero0 = besselJ(0, BESSEL_ZEROS[0][0]); // J0 at its first zero
  const atZero1 = besselJ(1, BESSEL_ZEROS[1][0]); // J1 at its first zero
  check(
    "besselJ matches known values (J0(0)=1, J1(0)=0) and vanishes at its zeros",
    Math.abs(j00 - 1) < 1e-9 &&
      Math.abs(j10) < 1e-9 &&
      Math.abs(atZero0) < 1e-6 &&
      Math.abs(atZero1) < 1e-6,
    `J0(0)=${j00.toFixed(4)} J1(0)=${j10.toFixed(4)} J0(α01)=${atZero0.toExponential(1)}`,
  );
}

// --- 28. verification bridge: rectangular membrane FDTD vs continuous ω -----
// The FDTD's discrete eigenfrequency must match the CONTINUOUS analytic
// membrane frequency ω = c·π·√((p/Lx)² + (q/Ly)²) to O(h²) — the numeric ⇄
// analytic bridge for a clamped rectangular drum.
{
  const sim = new WaveSim(129, 129, { speed: 0.6, boundary: "fixed" });
  let worst = 0;
  for (const [p, q] of [[1, 1], [2, 3], [3, 2]]) {
    const wd = discreteOmega(sim, p, q);
    const wc = sim.courant * Math.PI * Math.hypot(p / (sim.nx - 1), q / (sim.ny - 1));
    worst = Math.max(worst, Math.abs(wd - wc) / wc);
  }
  check(
    "rectangular membrane: FDTD frequency matches the continuous analytic ω",
    worst < 0.01,
    `worst rel. error = ${(worst * 100).toFixed(3)}%`,
  );
}

// --- 29. verification bridge: circular drumhead Bessel eigenfrequency -------
// Seed the exact J_m(α r/R)cos(mθ) mode in a masked circular cavity; the FDTD
// must ring at the Bessel eigenfrequency ω = c·α_{m,n}/R (measured by FFT).
{
  const probeFreq = (m, n, R) => {
    const N = 2 * R + 13;
    const { cx, cy } = drumGeom(N, N);
    const sim = new WaveSim(N, N, { speed: 0.5, damping: 0, boundary: "fixed" });
    sim.seedDrumheadMode(m, n);
    const pi = Math.round(cx + (m === 0 ? R * 0.25 : R * 0.45));
    const pj = Math.round(cy);
    const ser = new Float32Array(2048);
    for (let s = 0; s < 2048; s++) {
      sim.step();
      ser[s] = sim.cur[sim.idx(pi, pj)];
    }
    const meas = 2 * Math.PI * magnitudeSpectrum(ser).peakFreq;
    return { meas, ana: sim.drumheadOmega(m, n), peak: sim.maxAbs() };
  };
  // The higher modes are cleaner (the fundamental is most affected by the
  // staircased rim and coarse FFT); check a couple within a tight tolerance.
  const a = probeFreq(1, 1, 70);
  const b = probeFreq(2, 1, 70);
  check(
    "circular drumhead: FDTD rings at the analytic Bessel eigenfrequency",
    Math.abs(a.meas - a.ana) / a.ana < 0.04 &&
      Math.abs(b.meas - b.ana) / b.ana < 0.04 &&
      a.peak < 5 &&
      b.peak < 5,
    `J1,1 err=${((100 * Math.abs(a.meas - a.ana)) / a.ana).toFixed(1)}% J2,1 err=${((100 * Math.abs(b.meas - b.ana)) / b.ana).toFixed(1)}%`,
  );
}

// --- 30. d'Alembert: an exact right-travelling plane wave ------------------
// A discrete plane wave sin(k·i − ω·n) with ω = 2·asin(κ·sin(k/2)) is an exact
// solution of the leapfrog. Seeded with the correct one-step-past field and a
// free (uniform-in-y) edge, it must translate rigidly — d'Alembert.
{
  const nx = 160;
  const ny = 8;
  const k = (2 * Math.PI) / 22; // wavelength 22 cells
  const sim = new WaveSim(nx, ny, { speed: 0.6, damping: 0, boundary: "free" });
  const w = 2 * Math.asin(sim.courant * Math.sin(k / 2));
  for (let j = 0; j < ny; j++)
    for (let i = 0; i < nx; i++) {
      const kk = sim.idx(i, j);
      sim.cur[kk] = Math.sin(k * i);
      sim.prev[kk] = Math.sin(k * i + w); // one step in the past (n = −1)
    }
  const N = 30;
  sim.step(N);
  let maxErr = 0;
  const j = ny >> 1;
  for (let i = 45; i <= 105; i++) {
    // Interior band, away from the x-edges the reflections haven't reached.
    maxErr = Math.max(maxErr, Math.abs(sim.cur[sim.idx(i, j)] - Math.sin(k * i - w * N)));
  }
  check(
    "d'Alembert plane wave translates rigidly (exact discrete solution)",
    maxErr < 1e-4,
    `interior maxErr=${maxErr.toExponential(2)}, phase speed ω/k=${(w / k).toFixed(4)}`,
  );
}

// --- 31. drumhead scene lifecycle ------------------------------------------
{
  const sim = new WaveSim(90, 70, { speed: 0.5, boundary: "fixed" });
  sim.seedDrumheadMode(1, 1);
  const { cx, cy, R } = drumGeom(sim.nx, sim.ny);
  check(
    "seedDrumheadMode masks a circular cavity and seeds a non-trivial field",
    sim.hasScene &&
      sim.isSolid(Math.round(cx + R + 2), Math.round(cy)) &&
      !sim.isSolid(Math.round(cx), Math.round(cy)) &&
      sim.maxAbs() > 0.1 &&
      sim.model === "linear",
    `maxAbs=${sim.maxAbs().toFixed(2)} model=${sim.model}`,
  );
}

console.log(`\n${fail === 0 ? "ALL PASS" : "FAILURES"}: ${pass} passed, ${fail} failed`);
process.exit(fail === 0 ? 0 : 1);
