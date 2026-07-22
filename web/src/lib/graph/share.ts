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

const CAP = 60; // never encode a runaway number of rows/vars

export function encodeState(state: SharedState): string {
  return toB64Url(JSON.stringify(state));
}

/** Parse an encoded snapshot, returning null on any malformed/invalid input. */
export function decodeState(str: string): SharedState | null {
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
  if (
    !view ||
    !Number.isFinite(view.cx) ||
    !Number.isFinite(view.cy) ||
    !Number.isFinite(view.scale) ||
    (view.scale as number) <= 0
  ) {
    return null;
  }
  if (!Array.isArray(o.rows) || !Array.isArray(o.vars)) return null;
  const rows: SharedRow[] = o.rows
    .filter((r): r is Record<string, unknown> => !!r && typeof (r as Record<string, unknown>).text === "string")
    .slice(0, CAP)
    .map((r) => ({
      text: r.text as string,
      color: typeof r.color === "string" ? (r.color as string) : "#2563eb",
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
    .map((x) => ({ name: x.name as string, value: x.value as string }));
  return {
    v: 1,
    rows,
    view: { cx: view.cx as number, cy: view.cy as number, scale: view.scale as number },
    vars,
  };
}
