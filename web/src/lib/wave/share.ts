// Encode/decode a shareable Wave-scene snapshot to a compact URL-safe string.
// A share link carries the full experiment — the scene preset, boundary, the
// simulation/source knobs, the physics model, the appearance, and any
// CAS-driven initial-condition expression — so opening the link reproduces the
// same setup on any machine. Pure and unit-tested (tools/wave_share_test.mjs).

import type { Boundary, FilterType, FieldModel, Stencil } from "./sim";

export interface WaveConfig {
  v: 1;
  scene: string;
  boundary: Boundary;
  speed: number;
  damping: number;
  freq: number;
  src: "ripple" | "drive";
  brush: number;
  strength: number;
  robin: number;
  filterType: FilterType;
  cutoff: number;
  reflect: number;
  model: FieldModel;
  mass: number;
  stencil: Stencil;
  color: "coolwarm" | "fire" | "violet";
  view: "wave" | "intensity";
  cols: number;
  /** CAS initial-condition expression u(x,y,0)=f(x,y); "" = none. */
  ic: string;
}

// Allowed enum values — anything outside these is rejected on decode.
const SCENES = ["empty", "double-slit", "single-slit", "lens", "waveguide", "refraction", "drumhead"];
const BOUNDARIES = ["fixed", "free", "absorbing", "robin", "filtered"];
const MODELS = ["linear", "klein-gordon", "sine-gordon"];
const STENCILS = ["five", "nine"];
const COLORS = ["coolwarm", "fire", "violet"];
const VIEWS = ["wave", "intensity"];
const SRCS = ["ripple", "drive"];
const FILTERS = ["lowpass", "highpass"];

const MAX_PAYLOAD = 8000; // reject an oversized hostile hash before decoding
const MAX_IC = 512; // CAS expression length cap

function toB64Url(s: string): string {
  const bytes = new TextEncoder().encode(s);
  let bin = "";
  for (const b of bytes) bin += String.fromCharCode(b);
  return btoa(bin).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}
function fromB64Url(s: string): string {
  const b64 = s.replace(/-/g, "+").replace(/_/g, "/");
  const bin = atob(b64);
  const bytes = Uint8Array.from(bin, (c) => c.charCodeAt(0));
  return new TextDecoder().decode(bytes);
}

export function encodeWave(cfg: WaveConfig): string {
  return toB64Url(JSON.stringify(cfg));
}

function num(v: unknown, lo: number, hi: number, fallback: number): number {
  return typeof v === "number" && Number.isFinite(v) ? Math.max(lo, Math.min(hi, v)) : fallback;
}
function pick<T extends string>(v: unknown, allowed: readonly string[], fallback: T): T {
  return typeof v === "string" && allowed.includes(v) ? (v as T) : fallback;
}

/**
 * Parse an encoded snapshot, returning null on any malformed/invalid/hostile
 * input. Never throws; bounds the payload, clamps every number, and rejects
 * any enum value or IC expression outside the allowed set.
 */
export function decodeWave(str: string): WaveConfig | null {
  if (typeof str !== "string" || str.length === 0 || str.length > MAX_PAYLOAD) return null;
  let obj: Record<string, unknown>;
  try {
    const parsed: unknown = JSON.parse(fromB64Url(str));
    if (!parsed || typeof parsed !== "object") return null;
    obj = parsed as Record<string, unknown>;
  } catch {
    return null;
  }
  let ic = typeof obj.ic === "string" ? obj.ic : "";
  // Reject control characters; cap length (the CAS validates the rest at use).
  if (ic.length > MAX_IC || /[\u0000-\u001f\u007f]/.test(ic)) ic = "";
  return {
    v: 1,
    scene: pick(obj.scene, SCENES, "empty"),
    boundary: pick(obj.boundary, BOUNDARIES, "fixed"),
    speed: num(obj.speed, 0, 1, 0.5),
    damping: num(obj.damping, 0, 1, 0.06),
    freq: num(obj.freq, 0, 1, 0.35),
    src: pick(obj.src, SRCS, "ripple"),
    brush: Math.round(num(obj.brush, 1, 12, 4)),
    strength: num(obj.strength, 0.2, 2.5, 1),
    robin: num(obj.robin, 0, 8, 0.5),
    filterType: pick(obj.filterType, FILTERS, "lowpass"),
    cutoff: num(obj.cutoff, 0.02, 1, 0.5),
    reflect: num(obj.reflect, 0, 1, 0.85),
    model: pick(obj.model, MODELS, "linear"),
    mass: num(obj.mass, 0, 2, 0.1),
    stencil: pick(obj.stencil, STENCILS, "five"),
    color: pick(obj.color, COLORS, "coolwarm"),
    view: pick(obj.view, VIEWS, "wave"),
    cols: Math.round(num(obj.cols, 48, 512, 180)),
    ic,
  };
}
