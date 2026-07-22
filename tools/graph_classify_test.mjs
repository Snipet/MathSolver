// Unit tests for the graph row classifier (web/src/lib/graph/classify.ts).
import { classifyRow, splitRelation, splitTopLevelCommas } from "../web/src/lib/graph/classify.ts";

let pass = 0, fail = 0;
const check = (n, c, e = "") => { if (c) { pass++; console.log("PASS  " + n); } else { fail++; console.log(`FAIL  ${n}${e ? "  [" + e + "]" : ""}`); } };
const kind = (s) => classifyRow(s).t;

check("bare expression → function", kind("a*sin(x)") === "function");
check("y = expr → function", kind("y = x^2 + 1") === "function" && classifyRow("y=x^2").expr === "x^2");
check("x = expr → functionY", kind("x = y^2") === "functionY" && classifyRow("x = y^2").expr === "y^2");
check("r = expr → polar", kind("r = 1 + cos(theta)") === "polar");
check("single point → pointish", (() => { const r = classifyRow("(1, 2)"); return r.t === "pointish" && r.coords.length === 1 && r.coords[0][0] === "1" && r.coords[0][1] === "2"; })());
check("point list → pointish", (() => { const r = classifyRow("(1,2), (3, 4)"); return r.t === "pointish" && r.coords.length === 2; })());
check("parametric-looking → pointish", (() => { const r = classifyRow("(cos(t), sin(t))"); return r.t === "pointish" && r.coords[0][0] === "cos(t)"; })());
check("implicit relation → relation =", (() => { const r = classifyRow("x^2 + y^2 = 4"); return r.t === "relation" && r.op === "=" && r.lhs === "x^2 + y^2" && r.rhs === "4"; })());
check("inequality < → relation", (() => { const r = classifyRow("y < x^2"); return r.t === "relation" && r.op === "<"; })());
check("inequality >= → relation", (() => { const r = classifyRow("x^2 + y^2 >= 1"); return r.t === "relation" && r.op === ">="; })());
check("empty → empty", kind("   ") === "empty");
check("comma inside a function is not a point", kind("max(x, 2)") === "function");
check("relation not split inside parens", splitRelation("f(x, y)") === null);
check("<= detected before <", splitRelation("y <= 2").op === "<=");
check("splitTopLevelCommas respects parens", splitTopLevelCommas("cos(t), sin(t)").length === 2 && splitTopLevelCommas("f(a,b)").length === 1);
check("chained 'y = x = 2' → relation, not folded function", kind("y = x = 2") === "relation");
check("define 'f = x^2' → define", (() => { const r = classifyRow("f = x^2"); return r.t === "define" && r.name === "f" && r.expr === "x^2"; })());

console.log(`\n${fail === 0 ? "ALL PASS" : "FAILURES"}: ${pass} passed, ${fail} failed`);
process.exit(fail === 0 ? 0 : 1);
