// Type surface of the Emscripten ES module produced by tools/build_wasm.sh
// (see wasm/bindings.cpp). Every method takes plain strings/numbers and
// returns a JSON string envelope. This mirrors the full EMSCRIPTEN_BINDINGS
// list — a superset of web/src/lib/wasm/mathsolver.d.ts, which only declares
// the subset the Svelte app happens to use.

export interface MathSolverModule {
  version(): string;
  analyze(input: string): string;
  simplify(input: string): string;
  expand(input: string): string;
  factor(input: string): string;
  cancel(input: string): string;
  together(input: string): string;
  latex(input: string): string;
  subs(input: string, assignments: string, simplifyResult: boolean): string;
  collect(input: string, variable: string): string;
  derivative(input: string, variable: string): string;
  apart(input: string, variable: string): string;
  fit(data: string, model: string, degree: string): string;
  stats(data: string): string;
  dsolve(ode: string, conditionsCsv: string): string;
  series(input: string, variable: string, center: string, order: number): string;
  vectorOp(op: string, fieldSemi: string, varsCsv: string): string;
  limit(input: string, variable: string, point: string, direction: string): string;
  mlimit(input: string, xVar: string, a: string, yVar: string, b: string): string;
  stirling(variable: string, terms: number): string;
  seq(termsCsv: string): string;
  gcd(list: string): string;
  lcm(list: string): string;
  isprime(n: string): string;
  nextprime(n: string): string;
  divisors(n: string): string;
  totient(n: string): string;
  cfrac(value: string): string;
  solveIneq(lhs: string, rhs: string, op: string, variable: string): string;
  mod(args: string): string;
  powmod(args: string): string;
  modinv(args: string): string;
  crt(system: string): string;
  sum(term: string, variable: string, lo: string, hi: string): string;
  product(term: string, variable: string, lo: string, hi: string): string;
  rsolve(recurrence: string, conditionsCsv: string): string;
  laplace(input: string, timeVar: string): string;
  ilaplace(input: string, freqVar: string): string;
  integrate(input: string, variable: string): string;
  integrateDefinite(input: string, variable: string, lo: string, hi: string): string;
  solve(input: string, variable: string, lo: number, hi: number, useRange: boolean): string;
  solveSystem(input: string, varsCsv: string): string;
  evaluate(input: string, bindings: string): string;
  plugins(): string;
  pluginCall(plugin: string, command: string, argsCsv: string): string;
}

export default function createMathSolverModule(options?: {
  locateFile?: (path: string) => string;
}): Promise<MathSolverModule>;
