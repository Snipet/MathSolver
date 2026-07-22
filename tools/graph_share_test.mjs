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
check("wrong version → null", decodeState(encodeState({ ...state, v: 2 })) === null);
check("bad view → null", decodeState(encodeState({ ...state, view: { cx: 0, cy: 0, scale: 0 } })) === null);
check("non-array rows → null", (() => { const bad = btoaJson({ v: 1, rows: "x", vars: [], view: { cx: 0, cy: 0, scale: 1 } }); return decodeState(bad) === null; })());

function btoaJson(o) {
  const s = JSON.stringify(o);
  const bytes = new TextEncoder().encode(s);
  let bin = ""; for (const b of bytes) bin += String.fromCharCode(b);
  return btoa(bin).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}

console.log(`\n${fail === 0 ? "ALL PASS" : "FAILURES"}: ${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
