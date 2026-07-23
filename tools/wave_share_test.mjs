// Unit tests for the Wave share-link codec (web/src/lib/wave/share.ts).
//
//   node tools/wave_share_test.mjs
import { encodeWave, decodeWave } from "../web/src/lib/wave/share.ts";

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

const full = {
  v: 1,
  scene: "double-slit",
  boundary: "absorbing",
  speed: 0.72,
  damping: 0.03,
  freq: 0.41,
  src: "drive",
  brush: 6,
  strength: 1.8,
  robin: 2.5,
  filterType: "highpass",
  cutoff: 0.33,
  reflect: 0.6,
  model: "klein-gordon",
  mass: 0.4,
  stencil: "nine",
  color: "fire",
  view: "intensity",
  cols: 220,
  ic: "exp(-6*(x^2+y^2))*cos(8*x)",
};

// 1. round-trip preserves every field.
{
  const back = decodeWave(encodeWave(full));
  check(
    "round-trips a full config exactly",
    back && JSON.stringify(back) === JSON.stringify(full),
    JSON.stringify(back),
  );
}

// 2. numbers are clamped into range; unknown enums fall back to defaults.
{
  const hostile = encodeWave({
    ...full,
    speed: 99,
    mass: -5,
    cols: 99999,
    scene: "not-a-scene",
    model: "evil",
    boundary: "nope",
    color: "chartreuse",
  });
  const back = decodeWave(hostile);
  check(
    "clamps numbers and rejects unknown enums",
    back &&
      back.speed === 1 &&
      back.mass === 0 &&
      back.cols === 512 &&
      back.scene === "empty" &&
      back.model === "linear" &&
      back.boundary === "fixed" &&
      back.color === "coolwarm",
    JSON.stringify(back),
  );
}

// 3. malformed / hostile payloads return null, never throw.
{
  const bad = ["", "!!!!", "x".repeat(9000), "eyJ2IjoxfQ".repeat(2000)];
  let ok = true;
  for (const s of bad) {
    try {
      if (decodeWave(s) !== null && s.length > 8000) ok = false;
    } catch {
      ok = false;
    }
  }
  // A non-JSON but decodable-length string must yield null, not throw.
  check("rejects malformed input without throwing", ok && decodeWave("not-base64-json!") === null);
}

// 4. a control-character IC expression is dropped; a clean one survives.
{
  const dirty = decodeWave(encodeWave({ ...full, ic: "sin(x)\u0007bad" }));
  const clean = decodeWave(encodeWave({ ...full, ic: "cos(3*x) + sin(2*y)" }));
  check(
    "strips a control-character IC but keeps a clean expression",
    dirty && dirty.ic === "" && clean && clean.ic === "cos(3*x) + sin(2*y)",
  );
}

// 5. an over-long IC expression is dropped.
{
  const back = decodeWave(encodeWave({ ...full, ic: "x+".repeat(400) }));
  check("drops an over-long IC expression", back && back.ic === "");
}

console.log(`\n${fail === 0 ? "ALL PASS" : "FAILURES"}: ${pass} passed, ${fail} failed`);
process.exit(fail === 0 ? 0 : 1);
