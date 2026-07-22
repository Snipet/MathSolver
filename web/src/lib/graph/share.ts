// Encode/decode a shareable graph snapshot to a compact URL-safe string.
// A share link carries the graph rows, the viewport, and the session variables
// they reference, so opening it reproduces the same picture on any machine.
// Pure and unit-tested (tools/graph_share_test.mjs).

export interface SharedRow {
  text: string;
  color: string;
  visible: boolean;
}
export interface SharedVar {
  name: string;
  value: string;
}
export interface SharedState {
  v: 1;
  rows: SharedRow[];
  view: { cx: number; cy: number; scale: number };
  vars: SharedVar[];
}

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

const CAP = 60; // never encode/decode a runaway number of rows/vars
const MAX_PAYLOAD = 64_000; // reject an oversized hostile hash before decoding
const MAX_TEXT = 2048;
const MAX_NAME = 64;
const MAX_VALUE = 1024;
const VIEW_MAX = 1e6; // reject a degenerate/extreme viewport
const COLOR_RE = /^#[0-9a-fA-F]{3,8}$/; // only hex colors reach style:background

export function encodeState(state: SharedState): string {
  const trimmed: SharedState = {
    v: 1,
    rows: state.rows.slice(0, CAP),
    view: state.view,
    vars: state.vars.slice(0, CAP),
  };
  return toB64Url(JSON.stringify(trimmed));
}

/** Parse an encoded snapshot, returning null on any malformed/invalid/hostile
 *  input. Never throws; bounds payload size, field lengths, colors, and view. */
export function decodeState(str: string): SharedState | null {
  if (typeof str !== "string" || str.length === 0 || str.length > MAX_PAYLOAD) return null;
  let obj: unknown;
  try {
    obj = JSON.parse(fromB64Url(str));
  } catch {
    return null;
  }
  if (!obj || typeof obj !== "object") return null;
  const o = obj as Record<string, unknown>;
  if (o.v !== 1) return null;
  const view = o.view as Record<string, unknown> | undefined;
  if (!view || !Number.isFinite(view.cx) || !Number.isFinite(view.cy) || !Number.isFinite(view.scale)) {
    return null;
  }
  const cx = view.cx as number;
  const cy = view.cy as number;
  const scale = view.scale as number;
  if (Math.abs(cx) > VIEW_MAX || Math.abs(cy) > VIEW_MAX || scale < 1e-6 || scale > VIEW_MAX) return null;
  if (!Array.isArray(o.rows) || !Array.isArray(o.vars)) return null;
  const rows: SharedRow[] = o.rows
    .filter((r): r is Record<string, unknown> => !!r && typeof (r as Record<string, unknown>).text === "string")
    .slice(0, CAP)
    .map((r) => ({
      text: (r.text as string).slice(0, MAX_TEXT),
      color:
        typeof r.color === "string" && COLOR_RE.test(r.color as string) ? (r.color as string) : "#2563eb",
      visible: r.visible !== false,
    }));
  const vars: SharedVar[] = o.vars
    .filter(
      (x): x is Record<string, unknown> =>
        !!x &&
        typeof (x as Record<string, unknown>).name === "string" &&
        typeof (x as Record<string, unknown>).value === "string",
    )
    .slice(0, CAP)
    .map((x) => ({ name: (x.name as string).slice(0, MAX_NAME), value: (x.value as string).slice(0, MAX_VALUE) }));
  return { v: 1, rows, view: { cx, cy, scale }, vars };
}
