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

export type Boundary = "fixed" | "free" | "absorbing" | "robin";

export interface WaveSimOptions {
  /** 0..1, mapped to the Courant number κ ∈ (0, MAX_COURANT]. */
  speed?: number;
  /** 0..1, velocity damping γ (0 = lossless). */
  damping?: number;
  boundary?: Boundary;
  /** Robin edge stiffness k ≥ 0 (∂u/∂n + k·u = 0): 0 = free, ∞ = fixed. */
  robin?: number;
}

/** Below the 2-D CFL limit 1/√2 with headroom, so every speed is stable. */
export const MAX_COURANT = 0.7;

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

  constructor(nx: number, ny: number, opts: WaveSimOptions = {}) {
    this.nx = Math.max(3, Math.floor(nx));
    this.ny = Math.max(3, Math.floor(ny));
    const n = this.nx * this.ny;
    this.cur = new Float32Array(n);
    this.prev = new Float32Array(n);
    this.nextBuf = new Float32Array(n);
    this._speed = clamp(opts.speed ?? 0.5, 0, 1);
    this._damping = clamp(opts.damping ?? 0, 0, 1);
    this._robin = Math.max(0, opts.robin ?? 0.5);
    this.boundary = opts.boundary ?? "fixed";
  }

  /** Courant number κ = c·dt/h actually used by the integrator. */
  get courant(): number {
    return MAX_COURANT * this._speed;
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

    for (let j = 1; j < ny - 1; j++) {
      const row = j * nx;
      for (let i = 1; i < nx - 1; i++) {
        const k = row + i;
        const lap = cur[k - 1] + cur[k + 1] + cur[k - nx] + cur[k + nx] - 4 * cur[k];
        next[k] = (2 * cur[k] - (1 - a) * prev[k] + C * lap) * inv;
      }
    }

    this.#applyBoundary(next, cur, kappa);

    // Rotate buffers: prev ← cur, cur ← next, scratch ← old prev.
    this.prev = cur;
    this.cur = next;
    this.nextBuf = prev;
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

    // Absorbing: 1st-order Mur radiation condition, so wavefronts leave the
    // window with little reflection. κ = c·dt/h; the interior next-values are
    // already computed above.
    const mu = (kappa - 1) / (kappa + 1);
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
   * A rest "pluck": add a Gaussian displacement bump centered at grid
   * coordinate (gx, gy), with equal contribution to `cur` and `prev` so the
   * patch starts at rest and radiates a symmetric outgoing ring.
   */
  poke(gx: number, gy: number, radius: number, amp: number): void {
    this.#brush(gx, gy, radius, (k, w) => {
      const d = amp * w;
      this.cur[k] += d;
      this.prev[k] += d;
    });
  }

  /**
   * A directional impulse for drag gestures: a velocity dipole aligned with
   * (dirX, dirY). Cells ahead of the center along the drag get positive
   * velocity, cells behind negative — so the disturbance launches along the
   * drag rather than radiating symmetrically. Applied to `cur` only, so the
   * per-step velocity (cur − prev) carries the momentum.
   */
  impartVelocity(
    gx: number,
    gy: number,
    radius: number,
    amp: number,
    dirX: number,
    dirY: number,
  ): void {
    const len = Math.hypot(dirX, dirY);
    if (len < 1e-9) {
      // No direction: fall back to an isotropic velocity kick.
      this.#brush(gx, gy, radius, (k, w) => (this.cur[k] += amp * w));
      return;
    }
    const dnx = dirX / len;
    const dny = dirY / len;
    const nx = this.nx;
    this.#brush(gx, gy, radius, (k, w) => {
      const i = k % nx;
      const j = (k - i) / nx;
      const proj = (i - gx) * dnx + (j - gy) * dny; // signed along-drag offset
      this.cur[k] += amp * w * (proj / radius);
    });
  }

  /** Apply `fn(index, weight)` to every cell within `radius` of (gx, gy). */
  #brush(
    gx: number,
    gy: number,
    radius: number,
    fn: (k: number, w: number) => void,
  ): void {
    const { nx, ny } = this;
    const r = Math.max(1, radius);
    const sigma2 = (r / 2) * (r / 2);
    const i0 = Math.max(0, Math.floor(gx - r));
    const i1 = Math.min(nx - 1, Math.ceil(gx + r));
    const j0 = Math.max(0, Math.floor(gy - r));
    const j1 = Math.min(ny - 1, Math.ceil(gy + r));
    for (let j = j0; j <= j1; j++) {
      for (let i = i0; i <= i1; i++) {
        const dx = i - gx;
        const dy = j - gy;
        const d2 = dx * dx + dy * dy;
        if (d2 > r * r) continue;
        fn(j * nx + i, Math.exp(-d2 / (2 * sigma2)));
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
    return 0.5 * kinetic + 0.5 * C * potential;
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
  }
}
