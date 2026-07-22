// 2D wave-equation solver (finite-difference time-domain).
//
// Integrates the damped scalar wave equation on a rectangular grid
//
//     ∂²u/∂t² = c² ∇²u − γ ∂u/∂t
//
// with an explicit leapfrog scheme and a 5-point Laplacian. Grid spacing and
// time step are fixed to 1, so the wave speed enters purely as the Courant
// number `κ = c·dt/h`, which we clamp below the 2-D stability limit
// 1/√2 ≈ 0.7071 — this makes the integrator unconditionally stable for every
// exposed `speed`, so a user slider can never blow the field up.
//
// The class is deliberately DOM-free and deterministic: it is the unit under
// test (see tools/wave_sim_test.mjs) and is driven per animation frame by
// WaveField.svelte, which reads `cur` directly to paint the canvas.

export type Boundary = "fixed" | "free" | "absorbing" | "robin" | "filtered";

/** 1st-order filter shaping a frequency-dependent boundary reflection. */
export type FilterType = "lowpass" | "highpass";

/**
 * Which wave PDE the field obeys (Phase 3 physics packs):
 * - `linear`        — the plain scalar wave `u_tt = c²∇²u − γu_t`.
 * - `klein-gordon`  — adds a mass term `− m²u`, giving a dispersive medium
 *                     (a rest frequency `√m` and phase speed that varies with
 *                     wavelength: short waves outrun long ones, so pulses spread).
 * - `sine-gordon`   — adds the nonlinear `− m²·sin(u)`, whose 2π twists are
 *                     topological **kink solitons** that keep their shape.
 */
export type FieldModel = "linear" | "klein-gordon" | "sine-gordon";

/** Spatial Laplacian stencil: the 5-point star, or a reduced-anisotropy
 *  9-point isotropic stencil (orthogonal + diagonal neighbors). */
export type Stencil = "five" | "nine";

export interface WaveSimOptions {
  /** 0..1, mapped to the Courant number κ ∈ (0, MAX_COURANT]. */
  speed?: number;
  /** 0..1, velocity damping γ (0 = lossless). */
  damping?: number;
  boundary?: Boundary;
  /** Robin edge stiffness k ≥ 0 (∂u/∂n + k·u = 0): 0 = free, ∞ = fixed. */
  robin?: number;
}

/** Below the 2-D CFL limit 1/√2 with headroom, so every speed is stable. This
 *  is the reference (5-point, massless) cap; `maxCourant()` scales it for the
 *  9-point stencil and for the mass term (see the CFL note there). */
export const MAX_COURANT = 0.7;

/** Slowest medium exposed: cScale below this freezes the field uninterestingly. */
export const MIN_SCALE = 0.2;

/** Reaction-term ceiling (m² per step²), by model. Klein–Gordon is a linear
 *  restoring term and stays unconditionally stable up to the CFL cap. The
 *  sine-Gordon coupling is held lower: its `−m²sin(u)` is *anti*-restoring near
 *  u = π (a physical hilltop), so — like any explicit nonlinear integrator — a
 *  large enough excitation can drive it unstable no matter how small the step.
 *  This ceiling keeps ordinary interaction (seeded kinks, single pokes) stable
 *  while staying honest that extreme forcing can still blow the nonlinear field
 *  up (see docs/proposals/wave-system.md, design notes). */
export const MAX_MASS = 2;
export const MAX_COUPLING_SINE = 0.5;

/**
 * Bessel function J_m(x) via its power series — accurate for the arguments a
 * drumhead eigenmode needs (|x| ≲ 12). Used to seed circular-membrane modes.
 */
export function besselJ(m: number, x: number): number {
  let term = Math.pow(x / 2, m);
  for (let k = 1; k <= m; k++) term /= k; // (x/2)^m / m!
  let sum = 0;
  for (let k = 0; k < 40; k++) {
    sum += term;
    term *= -((x * x) / 4) / ((k + 1) * (k + 1 + m));
  }
  return sum;
}

/** First positive zeros α_{m,n} of J_m (m = 0..2, n = 1..3) — a drumhead's
 *  eigenfrequencies are ω_{m,n} = c·α_{m,n}/R. */
export const BESSEL_ZEROS: Record<number, number[]> = {
  0: [2.404825558, 5.520078110, 8.653727913],
  1: [3.831705970, 7.015586670, 10.17346814],
  2: [5.135622302, 8.417244140, 11.61984117],
};

/** Inscribed circular-cavity geometry for a drumhead on an nx×ny grid. */
export function drumGeom(nx: number, ny: number): { cx: number; cy: number; R: number } {
  return {
    cx: (nx - 1) / 2,
    cy: (ny - 1) / 2,
    R: Math.max(6, Math.floor(Math.min(nx, ny) / 2) - 3),
  };
}

/** −h²∇² symbol maxima: 8 (5-point), 16/3 (9-point isotropic). Set the CFL. */
const LAPLACE_MAX_5 = 8;
const LAPLACE_MAX_9 = 16 / 3;

function clamp(v: number, lo: number, hi: number): number {
  return v < lo ? lo : v > hi ? hi : v;
}

export class WaveSim {
  readonly nx: number;
  readonly ny: number;
  /** Displacement at the current time step (row-major, length nx·ny). */
  cur: Float32Array;
  /** Displacement one step in the past. */
  prev: Float32Array;
  /** Scratch buffer for the step being computed (rotated in). */
  private nextBuf: Float32Array;

  private _speed = 0.5;
  private _damping = 0;
  private _robin = 0.5;
  boundary: Boundary;

  // Physics pack (Phase 3): the field model adds a reaction term to the wave
  // equation and the stencil selects the Laplacian. Both shift the CFL, which
  // `maxCourant()` folds back into the speed→Courant mapping so any slider (and
  // any mass) stays unconditionally stable.
  private _model: FieldModel = "linear";
  private _mass = 0; // reaction coefficient m² (per step²); 0 ⇒ plain wave
  private _stencil: Stencil = "five";

  // Frequency-dependent ("filtered") boundary: the reflected wave is the
  // incident wave passed through a 1st-order filter (see #applyBoundary).
  private _filterType: FilterType = "lowpass";
  private _filterCutoff = 0.5; // lowpass coefficient α ∈ (0,1]
  private _filterReflect = 0.85; // overall reflectivity g ∈ [0,1]
  private fOut: Float32Array; // per-boundary-cell filter memory

  // Heterogeneous medium: per-cell (cScale)² where cScale ∈ [MIN_SCALE, 1] is a
  // slowness (1 = reference/fastest, < 1 = slower/denser → higher index). Since
  // the local Courant κ·cScale never exceeds the uniform κ ≤ CFL, every scene
  // stays unconditionally stable. `solid` marks reflecting obstacle cells (held
  // at 0, i.e. interior Dirichlet). Both default to "off" with a fast path.
  private scale2: Float32Array;
  private solid: Uint8Array;
  private hasMedium = false;
  private hasSolid = false;

  // Continuous driven point source (the "drive"/oscillator gesture).
  private src = { active: false, x: 0, y: 0, r: 3, amp: 0, freq: 0.03, phase: 0 };

  // Instrumentation (Phase 2). Probes record u(t) at a cell into a ring buffer
  // (newest samples kept), for spectral analysis. The intensity field is a
  // per-cell running mean of u² — the time-averaged energy that makes standing
  // interference / diffraction patterns quantitative and static. Both are
  // gathered only while `measuring` is on, so the uniform fast path pays nothing
  // when instrumentation is idle.
  private probes: { i: number; j: number; buf: Float32Array; head: number; count: number }[] = [];
  private intensity: Float32Array;
  private intensityN = 0;
  private measuring = false;

  /** Ring-buffer length per probe (samples retained for the spectrum). */
  static readonly PROBE_CAP = 1024;

  constructor(nx: number, ny: number, opts: WaveSimOptions = {}) {
    this.nx = Math.max(3, Math.floor(nx));
    this.ny = Math.max(3, Math.floor(ny));
    const n = this.nx * this.ny;
    this.cur = new Float32Array(n);
    this.prev = new Float32Array(n);
    this.nextBuf = new Float32Array(n);
    this.fOut = new Float32Array(n);
    this.scale2 = new Float32Array(n).fill(1);
    this.solid = new Uint8Array(n);
    this.intensity = new Float32Array(n);
    this._speed = clamp(opts.speed ?? 0.5, 0, 1);
    this._damping = clamp(opts.damping ?? 0, 0, 1);
    this._robin = Math.max(0, opts.robin ?? 0.5);
    this.boundary = opts.boundary ?? "fixed";
  }

  /** Courant number κ = c·dt/h actually used by the integrator. */
  get courant(): number {
    return this.maxCourant() * this._speed;
  }

  /**
   * The largest stable Courant number for the current stencil and mass. The
   * lossless leapfrog is stable iff `κ²·λmax + R ≤ 4`, where λmax is the −∇²
   * stencil symbol maximum (8 for the 5-point star, 16/3 for the 9-point) and
   * `R` is the reaction bound (the mass term; |d/du sin(u)| ≤ 1 makes the
   * sine-Gordon coupling behave like a mass for stability). So
   * `κmax = √((4 − R)/λmax)`, times the same headroom factor that makes the
   * reference 5-point/massless cap MAX_COURANT rather than the bare 1/√2.
   */
  maxCourant(): number {
    const lambda = this._stencil === "nine" ? LAPLACE_MAX_9 : LAPLACE_MAX_5;
    const headroom = MAX_COURANT / Math.sqrt(4 / LAPLACE_MAX_5); // ≈ 0.99
    return headroom * Math.sqrt(Math.max(0, 4 - this.#reactionBound()) / lambda);
  }

  /** The reaction-term stability bound R (0 for the plain linear wave). */
  #reactionBound(): number {
    return this._model === "linear" ? 0 : this._mass;
  }

  get model(): FieldModel {
    return this._model;
  }
  set model(m: FieldModel) {
    this._model = m;
    // Re-clamp the coupling into the new model's stable range.
    this._mass = clamp(this._mass, 0, this.maxMass());
  }
  /** The stable ceiling for the reaction coefficient in the current model. */
  maxMass(): number {
    return this._model === "sine-gordon" ? MAX_COUPLING_SINE : MAX_MASS;
  }
  /** Reaction coefficient m² (Klein–Gordon mass² / sine-Gordon coupling). */
  get mass(): number {
    return this._mass;
  }
  set mass(v: number) {
    this._mass = clamp(v, 0, this.maxMass());
  }
  get stencil(): Stencil {
    return this._stencil;
  }
  set stencil(s: Stencil) {
    this._stencil = s;
  }
  get speed(): number {
    return this._speed;
  }
  set speed(v: number) {
    this._speed = clamp(v, 0, 1);
  }
  get damping(): number {
    return this._damping;
  }
  set damping(v: number) {
    this._damping = clamp(v, 0, 1);
  }
  get robin(): number {
    return this._robin;
  }
  set robin(v: number) {
    this._robin = Math.max(0, v);
  }
  setBoundary(b: Boundary): void {
    this.boundary = b;
  }

  /** Configure the frequency-dependent boundary: lowpass reflects lows and
   *  absorbs highs (a muffling wall), highpass does the reverse. `cutoff`
   *  ∈ (0,1] is the filter coefficient (higher = more reflective), `reflect`
   *  ∈ [0,1] the overall reflectivity (0 = fully absorbing). */
  setBoundaryFilter(type: FilterType, cutoff: number, reflect: number): void {
    this._filterType = type;
    this._filterCutoff = clamp(cutoff, 0.02, 1);
    this._filterReflect = clamp(reflect, 0, 1);
  }

  /** Position/enable the continuous driven source (a ripple-tank dipper).
   *  `freq` is cycles per step; energy is injected each step until cleared. */
  setSource(gx: number, gy: number, freq: number, amp: number, radius: number): void {
    const s = this.src;
    if (!s.active) s.phase = 0;
    s.active = true;
    s.x = gx;
    s.y = gy;
    s.freq = Math.max(0, freq);
    s.amp = amp;
    s.r = Math.max(1, radius);
  }
  clearSource(): void {
    this.src.active = false;
  }

  // --- heterogeneous medium & obstacles (scenes) -----------------------------

  /**
   * Set the medium: `cScale(i,j)` is a slowness in [MIN_SCALE, 1] (1 = the
   * reference speed, smaller = slower/denser → refraction toward the normal,
   * shorter wavelength). Pass a function, a Float32Array of length nx·ny, or
   * null to clear back to a uniform medium.
   */
  setMedium(field: ((i: number, j: number) => number) | Float32Array | null): void {
    const { nx, ny } = this;
    if (field === null) {
      this.scale2.fill(1);
      this.hasMedium = false;
      return;
    }
    let any = false;
    for (let j = 0; j < ny; j++) {
      for (let i = 0; i < nx; i++) {
        const k = j * nx + i;
        const raw = typeof field === "function" ? field(i, j) : field[k];
        const s = clamp(Number.isFinite(raw) ? raw : 1, MIN_SCALE, 1);
        this.scale2[k] = s * s;
        if (s !== 1) any = true;
      }
    }
    this.hasMedium = any;
  }

  /**
   * Mark reflecting obstacle cells (held at 0 — an interior Dirichlet wall).
   * Pass a predicate, a Uint8Array mask, or null to clear.
   */
  setSolid(mask: ((i: number, j: number) => boolean) | Uint8Array | null): void {
    const { nx, ny } = this;
    if (mask === null) {
      this.solid.fill(0);
      this.hasSolid = false;
      return;
    }
    let any = false;
    for (let j = 0; j < ny; j++) {
      for (let i = 0; i < nx; i++) {
        const k = j * nx + i;
        const on = typeof mask === "function" ? mask(i, j) : mask[k] !== 0;
        this.solid[k] = on ? 1 : 0;
        if (on) {
          any = true;
          this.cur[k] = 0;
          this.prev[k] = 0;
          this.nextBuf[k] = 0;
        }
      }
    }
    this.hasSolid = any;
  }

  /** Restore a uniform, obstacle-free medium. */
  clearScene(): void {
    this.scale2.fill(1);
    this.solid.fill(0);
    this.hasMedium = false;
    this.hasSolid = false;
  }

  get hasScene(): boolean {
    return this.hasMedium || this.hasSolid;
  }

  /** Obstacle mask (1 = wall), read-only — for painting. Do not mutate. */
  get obstacles(): Uint8Array {
    return this.solid;
  }
  /** Per-cell (cScale)², read-only — for shading the medium. Do not mutate. */
  get medium(): Float32Array {
    return this.scale2;
  }

  /** True if cell (i,j) is a solid obstacle — for painting the walls. */
  isSolid(i: number, j: number): boolean {
    return this.solid[j * this.nx + i] !== 0;
  }

  /** Slowness cScale ∈ [MIN_SCALE, 1] at (i,j) — for shading the medium. */
  scaleAt(i: number, j: number): number {
    return Math.sqrt(this.scale2[j * this.nx + i]);
  }

  // --- instrumentation: probes & intensity (Phase 2) -------------------------

  /** Enable/disable measurement gathering (probes + intensity). Off = no cost. */
  setMeasuring(on: boolean): void {
    this.measuring = on;
  }
  get isMeasuring(): boolean {
    return this.measuring;
  }

  /** Place a probe at the (clamped, interior) cell; returns its index. */
  addProbe(i: number, j: number): number {
    const pi = clamp(Math.round(i), 0, this.nx - 1);
    const pj = clamp(Math.round(j), 0, this.ny - 1);
    this.probes.push({
      i: pi,
      j: pj,
      buf: new Float32Array(WaveSim.PROBE_CAP),
      head: 0,
      count: 0,
    });
    return this.probes.length - 1;
  }

  removeProbe(id: number): void {
    if (id >= 0 && id < this.probes.length) this.probes.splice(id, 1);
  }

  clearProbes(): void {
    this.probes.length = 0;
  }

  get probeCount(): number {
    return this.probes.length;
  }

  /** Grid position of probe `id` (or null). */
  probeAt(id: number): { i: number; j: number } | null {
    const p = this.probes[id];
    return p ? { i: p.i, j: p.j } : null;
  }

  /**
   * The probe's recorded samples in chronological order (oldest → newest), up
   * to PROBE_CAP. Empty until it has collected samples. Returns a fresh copy.
   */
  probeSeries(id: number): Float32Array {
    const p = this.probes[id];
    if (!p || p.count === 0) return new Float32Array(0);
    const out = new Float32Array(p.count);
    const cap = p.buf.length;
    // Oldest sample sits just after head once the ring has wrapped.
    const start = p.count < cap ? 0 : p.head;
    for (let k = 0; k < p.count; k++) out[k] = p.buf[(start + k) % cap];
    return out;
  }

  /** Reset the accumulated intensity average (e.g. after changing the scene). */
  resetIntensity(): void {
    this.intensity.fill(0);
    this.intensityN = 0;
  }

  /** Number of steps folded into the current intensity average. */
  get intensitySamples(): number {
    return this.intensityN;
  }

  /** The per-cell time-averaged intensity ⟨u²⟩, read-only. Do not mutate. */
  intensityField(): Float32Array {
    return this.intensity;
  }

  idx(i: number, j: number): number {
    return j * this.nx + i;
  }

  /** Advance the field by `times` steps. */
  step(times = 1): void {
    for (let k = 0; k < times; k++) this.#stepOnce();
  }

  #stepOnce(): void {
    const { nx, ny } = this;
    const kappa = this.courant;
    const C = kappa * kappa;
    // Damping folded into the update: a = γ·dt/2 with dt = 1. The scheme
    //   u_next = [2u − (1−a)·u_prev + C·∇²u] / (1+a)
    // reduces to plain leapfrog when a = 0. γ is scaled down so the exposed
    // 0..1 damping spans "glassy" to "quickly dissipating" without over-
    // damping into mush.
    const a = this._damping * 0.5;
    const inv = 1 / (1 + a);
    const cur = this.cur;
    const prev = this.prev;
    const next = this.nextBuf;

    const linear = this._model === "linear";
    const five = this._stencil === "five";
    if (linear && five && !this.hasMedium && !this.hasSolid) {
      // Uniform linear 5-point fast path (the common case; unchanged).
      for (let j = 1; j < ny - 1; j++) {
        const row = j * nx;
        for (let i = 1; i < nx - 1; i++) {
          const k = row + i;
          const lap = cur[k - 1] + cur[k + 1] + cur[k - nx] + cur[k + nx] - 4 * cur[k];
          next[k] = (2 * cur[k] - (1 - a) * prev[k] + C * lap) * inv;
        }
      }
    } else {
      // General path: any 9-point stencil, Klein–Gordon / sine-Gordon reaction,
      // and/or a heterogeneous medium + obstacles. Per-cell C·scale²; solid
      // cells are held at 0, and solid neighbors read as 0 (interior Dirichlet),
      // so obstacles are hard reflecting walls with no special casing.
      const scene = this.hasMedium || this.hasSolid;
      const scale2 = this.scale2;
      const solid = this.solid;
      const nine = !five;
      const mass = this._mass;
      const react = this.#reactionBound(); // 0, or the mass coupling
      const sine = this._model === "sine-gordon";
      for (let j = 1; j < ny - 1; j++) {
        const row = j * nx;
        for (let i = 1; i < nx - 1; i++) {
          const k = row + i;
          if (scene && solid[k]) {
            next[k] = 0;
            continue;
          }
          // Laplacian: 5-point star, or the isotropic 9-point stencil
          //   ∇²u ≈ (1/6)[4·(orthogonal) + (diagonal) − 20·u].
          const lap = nine
            ? (4 * (cur[k - 1] + cur[k + 1] + cur[k - nx] + cur[k + nx]) +
                (cur[k - nx - 1] + cur[k - nx + 1] + cur[k + nx - 1] + cur[k + nx + 1]) -
                20 * cur[k]) / 6
            : cur[k - 1] + cur[k + 1] + cur[k - nx] + cur[k + nx] - 4 * cur[k];
          // Reaction term (Klein–Gordon: m²u; sine-Gordon: m²·sin u; else 0).
          const r = react === 0 ? 0 : sine ? mass * Math.sin(cur[k]) : mass * cur[k];
          const sc = scene ? scale2[k] : 1;
          next[k] = (2 * cur[k] - (1 - a) * prev[k] + C * sc * lap - r) * inv;
        }
      }
    }

    this.#applyBoundary(next, cur, kappa);

    // Walls win over the edge BC, so a barrier touching the frame stays a wall.
    if (this.hasSolid) {
      const solid = this.solid;
      for (let k = 0; k < solid.length; k++) if (solid[k]) next[k] = 0;
    }

    // Rotate buffers: prev ← cur, cur ← next, scratch ← old prev.
    this.prev = cur;
    this.cur = next;
    this.nextBuf = prev;

    // Driven source (soft): inject an oscillating forcing into the new field
    // each step, so holding the pointer in "drive" mode emits a steady tone.
    if (this.src.active) {
      const s = this.src;
      const val = s.amp * Math.sin(s.phase);
      this.#brush(s.x, s.y, s.r, (k, w) => {
        this.cur[k] += val * w;
      });
      s.phase += 2 * Math.PI * s.freq;
    }

    // Instrumentation: record probe samples and accumulate the intensity field
    // from the freshly-updated `cur`. Off by default (no cost).
    if (this.measuring) this.#measure();
  }

  /** Record u(t) into each probe's ring and fold u² into the intensity mean. */
  #measure(): void {
    const cur = this.cur;
    for (const p of this.probes) {
      p.buf[p.head] = cur[p.j * this.nx + p.i];
      p.head = (p.head + 1) % p.buf.length;
      if (p.count < p.buf.length) p.count++;
    }
    // Incremental per-cell running mean: I ← I + (u² − I)/n. Keeps the stored
    // value bounded (unlike a raw sum) and converges to the true time-average.
    const n = ++this.intensityN;
    const inv = 1 / n;
    const I = this.intensity;
    for (let k = 0; k < cur.length; k++) {
      const v = cur[k];
      I[k] += (v * v - I[k]) * inv;
    }
  }

  #applyBoundary(next: Float32Array, cur: Float32Array, kappa: number): void {
    const { nx, ny } = this;
    const iLast = nx - 1;
    const jLast = ny - 1;

    if (this.boundary === "fixed") {
      // Dirichlet u = 0: a clamped membrane (drum). Edges reflect with
      // inversion. Zero explicitly so edge pokes don't linger.
      for (let i = 0; i < nx; i++) {
        next[i] = 0;
        next[jLast * nx + i] = 0;
      }
      for (let j = 0; j < ny; j++) {
        next[j * nx] = 0;
        next[j * nx + iLast] = 0;
      }
      return;
    }

    if (this.boundary === "free") {
      // Neumann ∂u/∂n = 0: a free edge, reflects without inversion.
      for (let i = 0; i < nx; i++) {
        next[i] = next[nx + i]; // top ← row 1
        next[jLast * nx + i] = next[(jLast - 1) * nx + i]; // bottom ← row ny-2
      }
      for (let j = 0; j < ny; j++) {
        next[j * nx] = next[j * nx + 1]; // left ← col 1
        next[j * nx + iLast] = next[j * nx + iLast - 1]; // right ← col nx-2
      }
      return;
    }

    if (this.boundary === "robin") {
      // Robin / impedance edge ∂u/∂n + k·u = 0 (h = 1, one-sided): with the
      // interior neighbor u₁, the wall satisfies (u₁ − u₀) = k·u₀, i.e.
      // u₀ = u₁/(1 + k). k = 0 recovers free (Neumann); k → ∞ recovers fixed
      // (Dirichlet); intermediate k is a springy edge with a stiffness-
      // dependent reflection phase. Contraction (factor < 1) ⇒ stable.
      const f = 1 / (1 + this._robin);
      for (let i = 0; i < nx; i++) {
        next[i] = next[nx + i] * f;
        next[jLast * nx + i] = next[(jLast - 1) * nx + i] * f;
      }
      for (let j = 0; j < ny; j++) {
        next[j * nx] = next[j * nx + 1] * f;
        next[j * nx + iLast] = next[j * nx + iLast - 1] * f;
      }
      return;
    }

    const mu = (kappa - 1) / (kappa + 1);

    if (this.boundary === "filtered") {
      // Frequency-dependent edge. At each wall cell we form the reflecting
      // (Neumann F) and absorbing (Mur A) candidates; their difference F−A is
      // the reflected component. Passing it through a 1st-order lowpass and
      // recombining as A + g·H(F−A) gives a reflection that depends on
      // frequency: lowpass reflects lows / absorbs highs (a muffling wall),
      // highpass (its complement) reflects highs / absorbs lows. g scales the
      // overall reflectivity (0 = fully absorbing, like Mur).
      const g = this._filterReflect;
      const a = this._filterCutoff;
      const lp = this._filterType === "lowpass";
      const fOut = this.fOut;
      const cell = (k: number, kin: number) => {
        const A = cur[kin] + mu * (next[kin] - cur[k]);
        const F = next[kin];
        const refl = F - A;
        const low = fOut[k] + a * (refl - fOut[k]);
        fOut[k] = low;
        next[k] = A + g * (lp ? low : refl - low);
      };
      for (let j = 1; j < jLast; j++) {
        const l = j * nx;
        cell(l, l + 1);
        cell(l + iLast, l + iLast - 1);
      }
      for (let i = 1; i < iLast; i++) {
        cell(i, i + nx);
        cell(jLast * nx + i, jLast * nx + i - nx);
      }
      next[0] = 0.5 * (next[1] + next[nx]);
      next[iLast] = 0.5 * (next[iLast - 1] + next[iLast + nx]);
      next[jLast * nx] = 0.5 * (next[jLast * nx + 1] + next[(jLast - 1) * nx]);
      next[jLast * nx + iLast] =
        0.5 * (next[jLast * nx + iLast - 1] + next[(jLast - 1) * nx + iLast]);
      return;
    }

    // Absorbing: 1st-order Mur radiation condition, so wavefronts leave the
    // window with little reflection. κ = c·dt/h; the interior next-values are
    // already computed above.
    for (let j = 1; j < jLast; j++) {
      const l = j * nx;
      next[l] = cur[l + 1] + mu * (next[l + 1] - cur[l]);
      const r = l + iLast;
      next[r] = cur[r - 1] + mu * (next[r - 1] - cur[r]);
    }
    for (let i = 1; i < iLast; i++) {
      const t = i;
      next[t] = cur[t + nx] + mu * (next[t + nx] - cur[t]);
      const b = jLast * nx + i;
      next[b] = cur[b - nx] + mu * (next[b - nx] - cur[b]);
    }
    // Corners: average the two adjacent absorbed edges.
    next[0] = 0.5 * (next[1] + next[nx]);
    next[iLast] = 0.5 * (next[iLast - 1] + next[iLast + nx]);
    next[jLast * nx] = 0.5 * (next[jLast * nx + 1] + next[(jLast - 1) * nx]);
    next[jLast * nx + iLast] =
      0.5 * (next[jLast * nx + iLast - 1] + next[(jLast - 1) * nx + iLast]);
  }

  // --- energy injection ------------------------------------------------------

  /**
   * Seed a sine-Gordon **kink soliton**: a 2π twist `u = 4·arctan(e^{m·γ·ξ})`
   * (with `ξ` measured along x from the centre) — the exact static soliton of
   * `u_tt = c²u_xx − m²·sin u`, so it holds its shape as it evolves. Switches
   * the model to `sine-gordon` (and gives the coupling a sensible default if it
   * is zero, since a soliton needs `m > 0`). A non-zero `velocity` (cells/step)
   * launches a Lorentz-contracted moving kink that glides without spreading —
   * the headline demonstration that a soliton is not an ordinary wave packet.
   */
  seedKink(velocity = 0): void {
    this._model = "sine-gordon";
    if (this._mass <= 0) this._mass = 0.06;
    this.reset();
    const C = Math.max(1e-6, this.courant * this.courant); // wave speed² (= c²)
    const m = Math.sqrt(this._mass / C); // inverse kink width (cells⁻¹)
    const beta = clamp(velocity / Math.sqrt(C), -0.9, 0.9); // v / c
    const gamma = 1 / Math.sqrt(1 - beta * beta);
    const x0 = this.nx * 0.5;
    const v = velocity;
    const { nx, ny } = this;
    for (let i = 0; i < nx; i++) {
      // Current field at t = 0 and the past field at t = −1 (moving profile).
      const now = 4 * Math.atan(Math.exp(m * gamma * (i - x0)));
      const past = 4 * Math.atan(Math.exp(m * gamma * (i - x0 + v)));
      for (let j = 0; j < ny; j++) {
        const k = j * nx + i;
        this.cur[k] = now;
        this.prev[k] = past;
      }
    }
  }

  /**
   * Seed a circular-membrane **drumhead eigenmode**: mask an inscribed disk as
   * a fixed cavity and set `u = J_m(α_{m,n}·r/R)·cos(mθ)` at rest — the exact
   * analytic mode of a clamped circular membrane, which rings at the Bessel
   * eigenfrequency `ω = c·α_{m,n}/R`. The FDTD then reproduces that frequency
   * (the analytic ⇄ numeric verification bridge, tools/wave_sim_test.mjs).
   * Resets to the linear model (a membrane obeys the plain wave equation).
   */
  seedDrumheadMode(m = 1, n = 1): void {
    this._model = "linear";
    const zeros = BESSEL_ZEROS[m] ?? BESSEL_ZEROS[0];
    const alpha = zeros[Math.max(0, Math.min(zeros.length - 1, n - 1))];
    const { cx, cy, R } = drumGeom(this.nx, this.ny);
    this.reset();
    this.setSolid((i, j) => Math.hypot(i - cx, j - cy) > R);
    const { nx, ny } = this;
    for (let j = 0; j < ny; j++) {
      for (let i = 0; i < nx; i++) {
        const r = Math.hypot(i - cx, j - cy);
        if (r > R) continue; // outside the cavity: held at 0 by setSolid
        const th = Math.atan2(j - cy, i - cx);
        const v = besselJ(m, (alpha * r) / R) * Math.cos(m * th);
        const k = j * nx + i;
        this.cur[k] = v;
        this.prev[k] = v; // at rest → a standing mode
      }
    }
  }

  /** Analytic angular frequency (rad/step) of drumhead mode (m, n) on this
   *  grid: ω = κ·α_{m,n}/R. For the verification bridge and overlays. */
  drumheadOmega(m = 1, n = 1): number {
    const zeros = BESSEL_ZEROS[m] ?? BESSEL_ZEROS[0];
    const alpha = zeros[Math.max(0, Math.min(zeros.length - 1, n - 1))];
    return (this.courant * alpha) / drumGeom(this.nx, this.ny).R;
  }

  /**
   * A rest "pluck": a Gaussian bump added equally to `cur` and `prev` (starts
   * at rest → radiates a symmetric ring). With a finite `wavelength` the bump
   * carries a radial cosine, so it launches waves of that wavelength — the
   * frequency knob for the mouse. `wavelength = Infinity` is a plain bump.
   */
  poke(gx: number, gy: number, radius: number, amp: number, wavelength = Infinity): void {
    const kk = Number.isFinite(wavelength) ? (2 * Math.PI) / Math.max(2, wavelength) : 0;
    const eff = kk > 0 ? Math.min(60, Math.max(radius, 1.2 * wavelength)) : radius;
    this.#brush(gx, gy, eff, (k, w, r) => {
      const d = amp * w * (kk > 0 ? Math.cos(kk * r) : 1);
      this.cur[k] += d;
      this.prev[k] += d;
    });
  }

  /**
   * A directional launch for drag gestures. Uses the d'Alembert relation:
   * stamp the same bump into `cur` (centered at C) and into `prev` (centered
   * one wave-step κ *behind* along the drag), so the pulse travels in the
   * drag direction at speed κ instead of radiating symmetrically. `wavelength`
   * sets the launched frequency, as in poke().
   */
  launch(
    gx: number,
    gy: number,
    radius: number,
    amp: number,
    dirX: number,
    dirY: number,
    wavelength = Infinity,
  ): void {
    const len = Math.hypot(dirX, dirY);
    if (len < 1e-9) {
      this.poke(gx, gy, radius, amp, wavelength);
      return;
    }
    const dnx = dirX / len;
    const dny = dirY / len;
    const kappa = this.courant;
    const kk = Number.isFinite(wavelength) ? (2 * Math.PI) / Math.max(2, wavelength) : 0;
    const eff = kk > 0 ? Math.min(60, Math.max(radius, 1.2 * wavelength)) : radius;
    // cur bump at C; prev bump at C − κ·d̂ (behind) so the wave moves toward d̂.
    this.#brush(gx, gy, eff, (k, w, r) => {
      this.cur[k] += amp * w * (kk > 0 ? Math.cos(kk * r) : 1);
    });
    this.#brush(gx - kappa * dnx, gy - kappa * dny, eff, (k, w, r) => {
      this.prev[k] += amp * w * (kk > 0 ? Math.cos(kk * r) : 1);
    });
  }

  /** Apply `fn(index, weight, dist)` to every cell within `radius` of (gx, gy). */
  #brush(
    gx: number,
    gy: number,
    radius: number,
    fn: (k: number, w: number, r: number) => void,
  ): void {
    const { nx, ny } = this;
    const rad = Math.max(1, radius);
    const sigma2 = (rad / 2) * (rad / 2);
    const i0 = Math.max(0, Math.floor(gx - rad));
    const i1 = Math.min(nx - 1, Math.ceil(gx + rad));
    const j0 = Math.max(0, Math.floor(gy - rad));
    const j1 = Math.min(ny - 1, Math.ceil(gy + rad));
    for (let j = j0; j <= j1; j++) {
      for (let i = i0; i <= i1; i++) {
        const dx = i - gx;
        const dy = j - gy;
        const d2 = dx * dx + dy * dy;
        if (d2 > rad * rad) continue;
        fn(j * nx + i, Math.exp(-d2 / (2 * sigma2)), Math.sqrt(d2));
      }
    }
  }

  // --- diagnostics -----------------------------------------------------------

  /**
   * Discrete field energy that the leapfrog scheme actually conserves:
   * kinetic ½‖uⁿ − uⁿ⁻¹‖² plus potential ½c²⟨∇uⁿ, ∇uⁿ⁻¹⟩ — the potential is
   * the inner product of the spatial gradients at the two stored time levels,
   * NOT |∇uⁿ|² (that single-level form oscillates ~10% and isn't invariant).
   * With this staggered form the lossless value is constant to float
   * precision; it strictly decays with damping and with the absorbing edge.
   */
  energy(): number {
    const { nx, ny } = this;
    const cur = this.cur;
    const prev = this.prev;
    const C = this.courant * this.courant;
    let kinetic = 0;
    let potential = 0;
    for (let j = 0; j < ny; j++) {
      const row = j * nx;
      for (let i = 0; i < nx; i++) {
        const k = row + i;
        const v = cur[k] - prev[k];
        kinetic += v * v;
        if (i < nx - 1) {
          potential += (cur[k + 1] - cur[k]) * (prev[k + 1] - prev[k]);
        }
        if (j < ny - 1) {
          potential += (cur[k + nx] - cur[k]) * (prev[k + nx] - prev[k]);
        }
      }
    }
    let reaction = 0;
    if (this._model !== "linear" && this._mass > 0) {
      // Reaction potential: Klein–Gordon ½m²⟨uⁿ,uⁿ⁻¹⟩ (the staggered form the
      // scheme conserves); sine-Gordon m²(1 − cos u).
      if (this._model === "sine-gordon") {
        for (let k = 0; k < cur.length; k++) reaction += 1 - Math.cos(cur[k]);
        reaction *= this._mass;
      } else {
        for (let k = 0; k < cur.length; k++) reaction += cur[k] * prev[k];
        reaction *= 0.5 * this._mass;
      }
    }
    return 0.5 * kinetic + 0.5 * C * potential + reaction;
  }

  /** Largest absolute displacement — for color scaling and stability checks. */
  maxAbs(): number {
    const cur = this.cur;
    let m = 0;
    for (let k = 0; k < cur.length; k++) {
      const a = cur[k] < 0 ? -cur[k] : cur[k];
      if (a > m) m = a;
    }
    return m;
  }

  reset(): void {
    this.cur.fill(0);
    this.prev.fill(0);
    this.nextBuf.fill(0);
    this.fOut.fill(0);
    this.src.active = false;
    // Zeroing the field invalidates any accumulated intensity / probe history.
    this.resetIntensity();
    for (const p of this.probes) {
      p.buf.fill(0);
      p.head = 0;
      p.count = 0;
    }
  }
}
