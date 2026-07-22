// Wave scenes: structured media + obstacle presets for the interactive field.
// A scene supplies the geometry (a per-cell slowness `medium` and/or a solid
// obstacle `mask`), a sensible edge boundary, and a `start` hook that seeds a
// self-demonstrating driven source. The classic wave demos — double-slit
// diffraction, a converging lens, a waveguide, refraction at an interface —
// each become one click.
import { drumGeom, type Boundary, type WaveSim } from "./sim";

export interface SceneMeta {
  id: string;
  label: string;
  hint: string;
}

export interface Scene {
  boundary?: Boundary;
  /** Per-cell slowness cScale ∈ [MIN_SCALE, 1] (1 = fastest); omit for uniform. */
  medium?: (i: number, j: number) => number;
  /** Solid obstacle cells (reflecting walls); omit for none. */
  solid?: (i: number, j: number) => boolean;
  /** Seed a self-running demo (a driven source) after the geometry is applied. */
  start?: (sim: WaveSim) => void;
}

/** Menu metadata (order = display order). */
export const WAVE_SCENES: SceneMeta[] = [
  { id: "empty", label: "Open water", hint: "Uniform medium, no obstacles" },
  { id: "double-slit", label: "Double slit", hint: "Two slits → interference fringes" },
  { id: "single-slit", label: "Single slit", hint: "One slit → diffraction spread" },
  { id: "lens", label: "Lens", hint: "A slow region focuses waves to a point" },
  { id: "waveguide", label: "Waveguide", hint: "Two walls channel the wave" },
  { id: "refraction", label: "Refraction", hint: "Slower medium: waves bend, wavelength shortens" },
  { id: "drumhead", label: "Drumhead", hint: "Circular membrane — a Bessel eigenmode rings" },
];

/** A left-edge driven point source that keeps the scene alive. A modest
 *  amplitude keeps the color auto-scale from being dominated by the source, so
 *  the downstream diffraction/interference pattern stays visible. */
function driveLeft(sim: WaveSim, fx: number, fy: number, freq = 0.04): void {
  sim.setSource(
    Math.round(sim.nx * fx),
    Math.round(sim.ny * fy),
    freq,
    0.28,
    2.5,
  );
}

/** A vertical barrier band [bx, bx+thick) with the given gap test punched out. */
function barrier(
  nx: number,
  thick: number,
  frac: number,
  isGap: (j: number) => boolean,
): (i: number, j: number) => boolean {
  const bx = Math.round(nx * frac);
  return (i, j) => i >= bx && i < bx + thick && !isGap(j);
}

export function buildScene(id: string, nx: number, ny: number): Scene {
  const cy = ny / 2;

  switch (id) {
    case "double-slit": {
      const sep = Math.max(6, Math.round(ny * 0.22));
      const halfW = Math.max(2, Math.round(ny * 0.028));
      const a = cy - sep / 2;
      const b = cy + sep / 2;
      return {
        boundary: "absorbing",
        solid: barrier(nx, 2, 0.34, (j) => Math.abs(j - a) <= halfW || Math.abs(j - b) <= halfW),
        start: (s) => driveLeft(s, 0.14, 0.5, 0.035),
      };
    }

    case "single-slit": {
      const halfW = Math.max(2, Math.round(ny * 0.035));
      return {
        boundary: "absorbing",
        solid: barrier(nx, 2, 0.34, (j) => Math.abs(j - cy) <= halfW),
        start: (s) => driveLeft(s, 0.14, 0.5, 0.035),
      };
    }

    case "lens": {
      // A tall, thin biconvex region of slow medium centered on the grid —
      // waves from the left converge to a focus on the right.
      const cx = nx * 0.5;
      const rx = nx * 0.07;
      const ry = ny * 0.4;
      return {
        boundary: "absorbing",
        medium: (i, j) => {
          const dx = (i - cx) / rx;
          const dy = (j - cy) / ry;
          return dx * dx + dy * dy < 1 ? 0.45 : 1;
        },
        start: (s) => driveLeft(s, 0.1, 0.5, 0.04),
      };
    }

    case "waveguide": {
      // Two horizontal walls form a channel with an open mouth on the left.
      const half = Math.max(3, Math.round(ny * 0.13));
      const startX = Math.round(nx * 0.24);
      return {
        boundary: "absorbing",
        solid: (i, j) =>
          i >= startX && (Math.abs(j - (cy - half)) < 1.5 || Math.abs(j - (cy + half)) < 1.5),
        start: (s) => driveLeft(s, 0.14, 0.5, 0.04),
      };
    }

    case "refraction": {
      // A slower slab on the right half: circular wavefronts crossing the
      // vertical interface bend and their wavelength shortens.
      const xi = nx * 0.52;
      return {
        boundary: "absorbing",
        medium: (i) => (i > xi ? 0.55 : 1),
        start: (s) => driveLeft(s, 0.2, 0.5, 0.05),
      };
    }

    case "drumhead": {
      // A clamped circular membrane: the geometry is the inscribed disk (a
      // fixed cavity); the mode is seeded by seedDrumheadMode so it rings at
      // the analytic Bessel eigenfrequency.
      const { cx, cy, R } = drumGeom(nx, ny);
      return {
        boundary: "fixed",
        solid: (i, j) => Math.hypot(i - cx, j - cy) > R,
        start: (s) => s.seedDrumheadMode(1, 1),
      };
    }

    default:
      // "empty" / unknown: a clean uniform pond.
      return { boundary: "fixed" };
  }
}
