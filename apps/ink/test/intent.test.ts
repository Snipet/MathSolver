import assert from "node:assert/strict";
import { test } from "node:test";
import { parseLine, type Intent, type Job } from "../src/core/intent.js";

/** Assert parseLine yields a job and return it. */
function jobOf(line: string): Job {
  const intent = parseLine(line);
  assert.equal(intent.kind, "job", `expected a job for ${JSON.stringify(line)}, got ${intent.kind}`);
  return (intent as Extract<Intent, { kind: "job" }>).job;
}

test("meta and session lines", () => {
  assert.equal(parseLine("").kind, "empty");
  assert.equal(parseLine("help").kind, "help");
  assert.equal(parseLine("quit").kind, "quit");
  assert.equal(parseLine("exit").kind, "quit");
  assert.equal(parseLine("clear").kind, "clear");
  assert.equal(parseLine("vars").kind, "note");
  assert.equal(parseLine("unset x").kind, "note");
  assert.equal(parseLine("a := 2").kind, "note");
  assert.equal(parseLine("debug x").kind, "note");
});

test("bare input routes to simplify / equation / system / inequality", () => {
  assert.deepEqual(jobOf("2x + 3x"), { t: "simplify", input: "2x + 3x" });
  assert.deepEqual(jobOf("x^2 = 4"), { t: "bareEquation", input: "x^2 = 4" });
  assert.deepEqual(jobOf("x + y = 3; x - y = 1"), {
    t: "solveSystem",
    input: "x + y = 3; x - y = 1",
    varsCsv: "",
  });
  assert.deepEqual(jobOf("x^2 < 4"), { t: "solveIneq", lhs: "x^2", rhs: "4", op: "<", variable: "" });
});

test("solve command: single, explicit var, system, inequality", () => {
  assert.deepEqual(jobOf("solve x^2 = 4"), { t: "solve", input: "x^2 = 4", variable: null });
  assert.deepEqual(jobOf("solve x^2 = 4, x"), { t: "solve", input: "x^2 = 4", variable: "x" });
  assert.deepEqual(jobOf("solve x + y = 3; x - y = 1, x, y"), {
    t: "solveSystem",
    input: "x + y = 3; x - y = 1",
    varsCsv: "x,y",
  });
  assert.deepEqual(jobOf("solve x^2 < 4, x"), {
    t: "solveIneq",
    lhs: "x^2",
    rhs: "4",
    op: "<",
    variable: "x",
  });
  assert.equal(parseLine("solve x = 1, y, z").kind, "usage");
});

test("calculus verbs and inference", () => {
  assert.deepEqual(jobOf("diff sin(x^2)"), { t: "derivative", input: "sin(x^2)", variable: null });
  assert.deepEqual(jobOf("diff sin(x*y), x"), { t: "derivative", input: "sin(x*y)", variable: "x" });
  assert.deepEqual(jobOf("integrate x*sin(x)"), {
    t: "integrate",
    input: "x*sin(x)",
    variable: null,
    bounds: null,
  });
  assert.deepEqual(jobOf("integrate sin(x), x, 0, pi"), {
    t: "integrate",
    input: "sin(x)",
    variable: "x",
    bounds: ["0", "pi"],
  });
  assert.equal(parseLine("integrate x, x, 0").kind, "usage"); // 3 args invalid
  assert.deepEqual(jobOf("limit sin(x)/x, x, 0"), {
    t: "limit",
    input: "sin(x)/x",
    variable: "x",
    point: "0",
    direction: "",
  });
  assert.deepEqual(jobOf("limit 1/x, x, 0, right"), {
    t: "limit",
    input: "1/x",
    variable: "x",
    point: "0",
    direction: "right",
  });
  assert.deepEqual(jobOf("mlimit x*y/(x^2+y^2), x, 0, y, 0"), {
    t: "mlimit",
    input: "x*y/(x^2+y^2)",
    xVar: "x",
    a: "0",
    yVar: "y",
    b: "0",
  });
});

test("discrete / series / transforms", () => {
  assert.deepEqual(jobOf("sum k^2, k, 1, n"), {
    t: "sum",
    noun: "sum",
    term: "k^2",
    variable: "k",
    lo: "1",
    hi: "n",
  });
  assert.deepEqual(jobOf("product k, k, 1, 5"), {
    t: "sum",
    noun: "product",
    term: "k",
    variable: "k",
    lo: "1",
    hi: "5",
  });
  assert.deepEqual(jobOf("rsolve a(n+2) = a(n+1) + a(n), a(0)=0, a(1)=1"), {
    t: "rsolve",
    recurrence: "a(n+2) = a(n+1) + a(n)",
    conditionsCsv: "a(0)=0,a(1)=1",
  });
  assert.deepEqual(jobOf("dsolve y'' + y = sin(t), y(0)=0, y'(0)=0"), {
    t: "dsolve",
    ode: "y'' + y = sin(t)",
    conditionsCsv: "y(0)=0,y'(0)=0",
  });
  assert.deepEqual(jobOf("series sin(x), x, 0, 5"), {
    t: "series",
    input: "sin(x)",
    variable: "x",
    center: "0",
    order: 5,
  });
  assert.deepEqual(jobOf("series e^x"), {
    t: "series",
    input: "e^x",
    variable: "",
    center: "",
    order: 6,
  });
  assert.equal(parseLine("series x, x, 0, half").kind, "usage"); // bad order
  assert.deepEqual(jobOf("laplace sin(t)"), { t: "laplace", input: "sin(t)", variable: "" });
  assert.deepEqual(jobOf("stirling x, 3"), { t: "stirling", variable: "x", terms: 3 });
  assert.deepEqual(jobOf("stirling"), { t: "stirling", variable: "", terms: 3 });
});

test("vector calculus splits field from variables", () => {
  assert.deepEqual(jobOf("grad x^2 + y^2, x, y"), {
    t: "vector",
    op: "grad",
    fieldSemi: "x^2 + y^2",
    varsCsv: "x,y",
  });
  assert.deepEqual(jobOf("curl -y; x; 0, x, y, z"), {
    t: "vector",
    op: "curl",
    fieldSemi: "-y; x; 0",
    varsCsv: "x,y,z",
  });
  assert.equal(parseLine("grad x^2").kind, "usage"); // no variables
});

test("substitution / evaluation / algebra verbs", () => {
  assert.deepEqual(jobOf("eval x^2 + y, x=3, y=0.5"), {
    t: "evaluate",
    input: "x^2 + y",
    bindings: "x=3,y=0.5",
  });
  assert.deepEqual(jobOf("subs a*x + 3, a=2"), {
    t: "subs",
    input: "a*x + 3",
    assignments: "a=2",
  });
  assert.equal(parseLine("subs x").kind, "usage"); // no assignment
  assert.deepEqual(jobOf("collect a*x + b*x, x"), { t: "collect", input: "a*x + b*x", variable: "x" });
  assert.deepEqual(jobOf("apart 1/(x^2-1)"), { t: "apart", input: "1/(x^2-1)", variable: "" });
  assert.deepEqual(jobOf("cancel (x^2-1)/(x-1)"), {
    t: "cancel",
    input: "(x^2-1)/(x-1)",
    variable: null,
  });
  assert.deepEqual(jobOf("cancel (x^2-1)/(x-1), x"), {
    t: "cancel",
    input: "(x^2-1)/(x-1)",
    variable: "x",
  });
  assert.equal(parseLine("simplify").kind, "usage"); // empty input
});

test("fit / stats / seq", () => {
  const fit = jobOf("fit 0,0; 1,1; 2,4 | quadratic");
  assert.equal(fit.t, "fit");
  assert.equal((fit as Extract<Job, { t: "fit" }>).model, "quadratic");
  assert.equal((fit as Extract<Job, { t: "fit" }>).data.trim(), "0,0; 1,1; 2,4");

  const poly = jobOf("fit 0,0; 1,1 | poly 3");
  assert.equal((poly as Extract<Job, { t: "fit" }>).model, "poly");
  assert.equal((poly as Extract<Job, { t: "fit" }>).degree, "3");

  assert.deepEqual(jobOf("stats 1, 2, 3, 4, 5"), { t: "stats", data: "1, 2, 3, 4, 5" });
  assert.deepEqual(jobOf("seq 0, 1, 1, 2, 3"), { t: "seq", termsCsv: "0, 1, 1, 2, 3" });
  assert.equal(parseLine("seq 1, 2").kind, "usage"); // fewer than 4 terms
});

test("number theory verbs", () => {
  assert.deepEqual(jobOf("gcd 48, 36"), { t: "nt", name: "gcd", arg: "48, 36" });
  assert.deepEqual(jobOf("cfrac 355/113"), { t: "nt", name: "cfrac", arg: "355/113" });
  assert.deepEqual(jobOf("powmod 7, 100, 13"), { t: "nt", name: "powmod", arg: "7, 100, 13" });
  assert.deepEqual(jobOf("crt 2,3,2; 3,5,7"), { t: "nt", name: "crt", arg: "2,3,2; 3,5,7" });
  assert.equal(parseLine("gcd").kind, "usage");
});
