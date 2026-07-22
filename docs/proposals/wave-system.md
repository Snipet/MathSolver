# Proposal: Overhauling the Wave System

Status: **in progress** — Phases 1–2 shipped. This document is the roadmap for
turning the interactive wave field from a "ripple tank" into a "wave
laboratory". Grounded in `web/src/lib/wave/sim.ts`, `WaveField.svelte`, the
console `wave` command, and the analytical `pde.wave` (plugins/pde).

---

## 1. Current state

The Wave system is two pieces:

**A. The interactive field** (`web/src/lib/wave/sim.ts` + `WaveField.svelte`):
a 2-D **FDTD** solver of the damped scalar wave equation
`∂²u/∂t² = c²∇²u − γ ∂u/∂t`, explicit leapfrog + 5-point Laplacian, Courant
number clamped below the CFL limit so it is **unconditionally stable** for any
slider. Five boundary conditions (fixed / free / Robin / filtered / absorbing),
three source gestures (Gaussian pluck, directional d'Alembert launch, continuous
driven source) with a frequency knob, three colormaps, a live conserved-energy
readout. The sim is DOM-free and deterministic (unit-tested via
`tools/wave_sim_test.mjs`).

**B. The analytical solver** (`pde.wave`): separation of variables for
`u_tt = c²u_xx` on `[0, L]`, **Dirichlet only, 1-D only**.

**The limits.** The medium is homogeneous and empty (constant c, no obstacles or
interfaces); there are no measurement tools; the physics is the plain linear
scalar wave; and the numeric field, the analytic solver, and the CAS don't talk
to each other.

## 2. Vision

Structure the medium, instrument it, enrich the physics, let people author &
share experiments, and deepen the numerics — keeping the "any slider is stable,
honest about blow-up" doctrine.

## 3. Catalog (tiered)

**T1 — Structure the medium (flagship).** Per-cell wave speed `c(x,y)`
(refraction, lenses, gradient-index, waveguides); interior obstacles / masked
geometry (walls, cavities, **single/double-slit diffraction**); two-media
interfaces (Snell, TIR); a materials brush to draw them.

**T2 — Instrument it.** Probes/receivers recording `u(t)`; **FFT spectra**
(reuse `dsp`); intensity / time-averaged energy fields for quantitative
interference & diffraction; wavelength/speed/period overlays; k-space FFT and
dispersion-relation plots; modal analysis (reuse the **`fem` eigen-solver**).

**T3 — Enrich the physics.** Klein–Gordon dispersion (one term); sine-Gordon
**solitons**; elastic/vector (P&S) waves; advected media (Doppler); a **PML**
absorbing layer; a 9-point Laplacian for reduced numerical anisotropy.

**T4 — Sources.** Plane-wave/line sources; **phased arrays / beamforming**;
arbitrary waveforms / chirps; **time-reversal**; expression-driven initial
conditions `u(x,y,0)=f(x,y)` (couples the CAS, reuse `sampleGrid`).

**T5 — Visualization.** 3-D height-field surface (WebGL); amplitude/phase/
intensity view modes, log scale, contours/wavefronts; energy-flux arrows;
slow-motion / step / scrub / record.

**T6 — Author & share.** A presets/experiments gallery; save/share a scene as a
URL (reuse the graph `share.ts`); a numeric inspector.

**T7 — Extend `pde.wave`.** More BCs (Neumann/Robin/periodic); 2-D membranes —
rectangular & **circular (Bessel) drumhead modes**; d'Alembert traveling-wave;
damped/forced (telegraph) wave; a verification bridge comparing FDTD to the
analytic modes.

## 4. Phased roadmap

- **Phase 1 — Structured media (flagship).** Per-cell `c(x,y)` + obstacle mask +
  a preset gallery (double-slit, single-slit, lens, waveguide, refraction).
  **✓ shipped.**
- **Phase 2 — Instrumentation.** Probes → time-series → FFT spectrum + intensity
  field. **✓ shipped:** click-to-place receivers record `u(t)` into a ring
  buffer; a dependency-free radix-2 FFT (`web/src/lib/wave/spectrum.ts`, linear
  detrend + Hann window) renders a live per-probe magnitude spectrum with a
  peak-frequency readout (cycles/step + period); a Wave⇄Intensity view toggle
  paints the running-mean intensity field `⟨u²⟩` that freezes diffraction /
  interference fringes into a quantitative heatmap.
- **Phase 3 — Physics packs.** Klein–Gordon, sine-Gordon solitons, PML, 9-point
  Laplacian.
- **Phase 4 — Authoring & analytics.** Scene share-links, CAS-driven ICs, and
  `pde.wave` extensions (d'Alembert + 2-D Bessel drumhead) with the verification
  bridge.

Each phase is independently shippable; later phases reuse the `dsp` FFT/filters,
the `fem` eigen-solver, `sampleGrid`, and the graph `share.ts`. New physics
terms keep the sim DOM-free/deterministic and each gets a conservation/stability
test.

## 5. Design notes

- **Stability under variable media:** represent the medium as a *slowness*
  `cScale ∈ (0, 1]` (1 = reference/fastest, < 1 = slower/denser). The local
  Courant `κ·cScale ≤ κ ≤ CFL`, so every scene stays unconditionally stable
  without per-region CFL bookkeeping.
- **Obstacles** are masked Dirichlet cells (held at 0) — hard reflecting walls,
  exactly what slit/cavity demos need.
- Keep the sim DOM-free and deterministic; every scene and physics term is
  covered by `tools/wave_sim_test.mjs`.
