// Unit tests for the per-row plot style mapping (web/src/lib/graph/style.ts).
import { LINE_STYLES, LINE_WEIGHTS, strokeWidth, dashArrFor, pointRadius } from "../web/src/lib/graph/style.ts";

let pass = 0, fail = 0;
const check = (n, c) => { if (c) { pass++; console.log("PASS  " + n); } else { fail++; console.log("FAIL  " + n); } };
const eq = (a, b) => JSON.stringify(a) === JSON.stringify(b);

check("style/weight option lists", eq(LINE_STYLES, ["solid", "dashed", "dotted"]) && eq(LINE_WEIGHTS, ["thin", "normal", "thick"]));

// strokeWidth: thin < normal < thick, normal = the legacy 2.2
check("normal weight keeps the legacy 2.2", strokeWidth("normal") === 2.2);
check("thin is thinner, thick is thicker", strokeWidth("thin") < 2.2 && strokeWidth("thick") > 2.2);

// dashArrFor: solid empty, dashed/dotted non-empty with dotted finer than dashed
check("solid → no dash", eq(dashArrFor("solid"), []));
check("dashed → a dash pattern", dashArrFor("dashed").length === 2 && dashArrFor("dashed")[0] > 2);
check("dotted → a fine pattern (small on-length)", dashArrFor("dotted")[0] <= 2);

// pointRadius scales with weight off the base 4 at normal.
check("normal point radius is the base 4", pointRadius("normal") === 4);
check("thick points are larger, thin smaller", pointRadius("thick") > 4 && pointRadius("thin") < 4);

console.log(`\n${fail === 0 ? "ALL PASS" : "FAILURES"}: ${pass} passed, ${fail} failed`);
process.exit(fail === 0 ? 0 : 1);
