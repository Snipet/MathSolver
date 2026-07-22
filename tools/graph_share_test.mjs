// Unit tests for graph/share.ts encode/decode round-trip + validation.
import { encodeState, decodeState } from "../web/src/lib/graph/share.ts";

let pass = 0, fail = 0;
const check = (n, c, e = "") => { if (c) { pass++; console.log("PASS  " + n); } else { fail++; console.log(`FAIL  ${n}${e ? "  [" + e + "]" : ""}`); } };

const state = {
  v: 1,
  rows: [
    { text: "a*sin(x)", color: "#2563eb", visible: true },
    { text: "(a, b)", color: "#dc2626", visible: false },
  ],
  view: { cx: 1.5, cy: -2, scale: 40 },
  vars: [{ name: "a", value: "3" }, { name: "b", value: "1/2" }],
};

const enc = encodeState(state);
check("encoded is a URL-safe string", typeof enc === "string" && /^[A-Za-z0-9_-]+$/.test(enc), enc.slice(0, 40));
const dec = decodeState(enc);
check("round-trips rows", JSON.stringify(dec.rows) === JSON.stringify(state.rows));
check("round-trips view", JSON.stringify(dec.view) === JSON.stringify(state.view));
check("round-trips vars", JSON.stringify(dec.vars) === JSON.stringify(state.vars));
check("round-trips visibility=false", dec.rows[1].visible === false);

// Unicode survives (theta, etc.)
const uni = { v: 1, rows: [{ text: "r = 1 + cos(θ)", color: "#16a34a", visible: true }], view: { cx: 0, cy: 0, scale: 40 }, vars: [] };
check("round-trips unicode", decodeState(encodeState(uni)).rows[0].text === "r = 1 + cos(θ)");

// Malformed / hostile inputs → null (never throw)
check("garbage → null", decodeState("!!!not base64!!!") === null);
check("empty → null", decodeState("") === null);
check("wrong version → null", decodeState(btoaJson({ v: 2, rows: [], vars: [], view: { cx: 0, cy: 0, scale: 40 } })) === null);
check("bad view → null", decodeState(encodeState({ ...state, view: { cx: 0, cy: 0, scale: 0 } })) === null);
check("non-array rows → null", (() => { const bad = btoaJson({ v: 1, rows: "x", vars: [], view: { cx: 0, cy: 0, scale: 1 } }); return decodeState(bad) === null; })());

// Hardening
check("oversized payload → null", decodeState("A".repeat(70000)) === null);
check("extreme view → null", decodeState(encodeState({ ...state, view: { cx: 1e9, cy: 0, scale: 40 } })) === null);
check("tiny scale → null", decodeState(encodeState({ ...state, view: { cx: 0, cy: 0, scale: 1e-9 } })) === null);
check("non-hex color falls back to default", (() => {
  const bad = btoaJson({ v: 1, rows: [{ text: "x", color: "url(javascript:alert(1))", visible: true }], vars: [], view: { cx: 0, cy: 0, scale: 40 } });
  return decodeState(bad).rows[0].color === "#2563eb";
})());
check("valid hex color preserved", decodeState(encodeState({ ...state, rows: [{ text: "x", color: "#abc123", visible: true }] })).rows[0].color === "#abc123");
check("encode caps rows at 60", (() => {
  const many = { v: 1, rows: Array.from({ length: 80 }, (_, i) => ({ text: `x+${i}`, color: "#2563eb", visible: true })), vars: [], view: { cx: 0, cy: 0, scale: 40 } };
  return decodeState(encodeState(many)).rows.length === 60;
})());
check("oversized text field is truncated", (() => {
  const big = btoaJson({ v: 1, rows: [{ text: "x".repeat(5000), color: "#2563eb", visible: true }], vars: [], view: { cx: 0, cy: 0, scale: 40 } });
  return decodeState(big).rows[0].text.length === 2048;
})());

function btoaJson(o) {
  const s = JSON.stringify(o);
  const bytes = new TextEncoder().encode(s);
  let bin = ""; for (const b of bytes) bin += String.fromCharCode(b);
  return btoa(bin).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}

console.log(`\n${fail === 0 ? "ALL PASS" : "FAILURES"}: ${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
