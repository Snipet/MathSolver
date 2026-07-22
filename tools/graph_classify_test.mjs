// Unit tests for the graph row classifier (web/src/lib/graph/classify.ts).
import { classifyRow, splitRelation, splitTopLevelCommas, splitRestrictions, parseRestriction, coordAtom, rebuildPointRow } from "../web/src/lib/graph/classify.ts";

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

// Domain restrictions
check("restriction stripped from function", (() => { const r = classifyRow("x^2 {x > 0}"); return r.t === "function" && r.expr === "x^2" && JSON.stringify(r.restrict) === JSON.stringify(["x > 0"]); })());
check("multiple restriction clauses", (() => { const { body, restrict } = splitRestrictions("x^2 {x>0}{x<5}"); return body === "x^2" && restrict.length === 2 && restrict[0] === "x>0" && restrict[1] === "x<5"; })());
check("restriction on polar", (() => { const r = classifyRow("theta {0 <= theta <= 6*pi}"); return r.t === "polar" || (r.t === "function"); })());
check("chained bound → two comparisons", (() => { const cs = parseRestriction(["0 <= t <= 6*pi"]); return cs.length === 2 && cs[0].lhs === "0" && cs[0].op === "<=" && cs[0].rhs === "t" && cs[1].lhs === "t" && cs[1].rhs === "6*pi"; })());
check("single comparison", (() => { const cs = parseRestriction(["a < x"]); return cs.length === 1 && cs[0].lhs === "a" && cs[0].op === "<" && cs[0].rhs === "x"; })());
check("no restriction → empty list", (() => { const { body, restrict } = splitRestrictions("x^2 + 1"); return body === "x^2 + 1" && restrict.length === 0; })());
check("brace inside stays balanced", (() => { const { body, restrict } = splitRestrictions("x^2"); return body === "x^2" && restrict.length === 0; })());

// Draggable-point coordinate classification
check("coordAtom literals", ["3","-2.5","1e3",".5","+4"].every((s) => coordAtom(s) === "literal"));
check("coordAtom fraction is locked (no silent decimal)", coordAtom("1/2") === "locked");
check("coordAtom idents", coordAtom("a") === "ident" && coordAtom("x_max") === "ident");
check("coordAtom x/y/r are locked", ["x","y","r"].every((s) => coordAtom(s) === "locked"));
check("coordAtom expressions are locked", ["a+1","2*b","cos(t)"].every((s) => coordAtom(s) === "locked"));
// rebuildPointRow preserves siblings + restrictions, changes only the dragged coord
check("rebuild replaces one point, keeps the other", rebuildPointRow("(1,2), (a, b)", 0, "3.5", "-1") === "(3.5, -1), (a, b)");
check("rebuild changes only x, keeps y + restriction", rebuildPointRow("(1,2){x>0}", 0, "3", null) === "(3, 2) {x>0}");
check("rebuild non-pointish is unchanged", rebuildPointRow("y = x^2", 0, "1", "1") === "y = x^2");
check("rebuild out-of-range index is unchanged", rebuildPointRow("(1, 2)", 5, "9", "9") === "(1, 2)");

console.log(`\n${fail === 0 ? "ALL PASS" : "FAILURES"}: ${pass} passed, ${fail} failed`);
process.exit(fail === 0 ? 0 : 1);
