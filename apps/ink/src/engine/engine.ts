// Typed async facade over the WASM engine.
//
// Every method returns a Promise even though the underlying embind call is
// synchronous: the call is deferred to the next event-loop turn so the Ink UI
// can paint its "computing…" state first, and the async surface means a
// worker-thread implementation (for hard timeouts on pathological inputs)
// could later drop in without touching any caller.

import { loadEngineModule } from "./load.js";
import type { MathSolverModule } from "./module.js";
import type {
  AnalyzeResult,
  DefiniteResult,
  DsolveResult,
  EvaluateResult,
  FitResult,
  IntegrateResult,
  LimitResult,
  RsolveResult,
  SeqResult,
  SolveResult,
  StatsResult,
  SumResult,
  SystemResult,
  TransformResult,
  VersionResult,
} from "./types.js";

/**
 * The engine surface the app depends on. Declaring it as an interface (rather
 * than only the concrete class) lets tests inject a stub without a WASM build.
 */
export interface Engine {
  version(): Promise<VersionResult>;
  analyze(input: string): Promise<AnalyzeResult>;
  simplify(input: string): Promise<TransformResult>;
  expand(input: string): Promise<TransformResult>;
  factor(input: string): Promise<TransformResult>;
  cancel(input: string): Promise<TransformResult>;
  together(input: string): Promise<TransformResult>;
  latex(input: string): Promise<TransformResult>;
  collect(input: string, variable: string): Promise<TransformResult>;
  apart(input: string, variable: string): Promise<TransformResult>;
  derivative(input: string, variable: string): Promise<TransformResult>;
  subs(input: string, assignments: string, simplifyResult: boolean): Promise<TransformResult>;
  laplace(input: string, timeVar: string): Promise<TransformResult>;
  ilaplace(input: string, freqVar: string): Promise<TransformResult>;
  vectorOp(op: string, fieldSemi: string, varsCsv: string): Promise<TransformResult>;
  stirling(variable: string, terms: number): Promise<TransformResult>;
  gcd(list: string): Promise<TransformResult>;
  lcm(list: string): Promise<TransformResult>;
  isprime(n: string): Promise<TransformResult>;
  nextprime(n: string): Promise<TransformResult>;
  divisors(n: string): Promise<TransformResult>;
  totient(n: string): Promise<TransformResult>;
  cfrac(value: string): Promise<TransformResult>;
  mod(args: string): Promise<TransformResult>;
  powmod(args: string): Promise<TransformResult>;
  modinv(args: string): Promise<TransformResult>;
  crt(system: string): Promise<TransformResult>;
  solveIneq(lhs: string, rhs: string, op: string, variable: string): Promise<TransformResult>;
  series(input: string, variable: string, center: string, order: number): Promise<TransformResult>;
  fit(data: string, model: string, degree: string): Promise<FitResult>;
  stats(data: string): Promise<StatsResult>;
  seq(termsCsv: string): Promise<SeqResult>;
  limit(input: string, variable: string, point: string, direction: string): Promise<LimitResult>;
  mlimit(input: string, xVar: string, a: string, yVar: string, b: string): Promise<LimitResult>;
  sum(term: string, variable: string, lo: string, hi: string): Promise<SumResult>;
  product(term: string, variable: string, lo: string, hi: string): Promise<SumResult>;
  rsolve(recurrence: string, conditionsCsv: string): Promise<RsolveResult>;
  dsolve(ode: string, conditionsCsv: string): Promise<DsolveResult>;
  integrate(input: string, variable: string): Promise<IntegrateResult>;
  integrateDefinite(
    input: string,
    variable: string,
    lo: string,
    hi: string,
  ): Promise<DefiniteResult>;
  solve(
    input: string,
    variable: string,
    lo: number,
    hi: number,
    useRange: boolean,
  ): Promise<SolveResult>;
  solveSystem(input: string, varsCsv: string): Promise<SystemResult>;
  evaluate(input: string, bindings: string): Promise<EvaluateResult>;
}

/** Describe a thrown value from the WASM boundary (aborts throw non-Errors). */
function describe(e: unknown): string {
  if (e instanceof Error) return e.message;
  if (typeof e === "string") return e;
  return `engine internal error (${String(e)})`;
}

class WasmEngine implements Engine {
  constructor(private readonly wasm: MathSolverModule) {}

  /** Defer to the next tick, then run the bound function and parse its JSON. */
  private call<T>(fn: keyof MathSolverModule, ...args: unknown[]): Promise<T> {
    return new Promise<T>((resolve, reject) => {
      setImmediate(() => {
        try {
          const f = this.wasm[fn] as (...a: unknown[]) => string;
          resolve(JSON.parse(f.apply(this.wasm, args)) as T);
        } catch (e) {
          reject(new Error(describe(e)));
        }
      });
    });
  }

  version() {
    return this.call<VersionResult>("version");
  }
  analyze(input: string) {
    return this.call<AnalyzeResult>("analyze", input);
  }
  simplify(input: string) {
    return this.call<TransformResult>("simplify", input);
  }
  expand(input: string) {
    return this.call<TransformResult>("expand", input);
  }
  factor(input: string) {
    return this.call<TransformResult>("factor", input);
  }
  cancel(input: string) {
    return this.call<TransformResult>("cancel", input);
  }
  together(input: string) {
    return this.call<TransformResult>("together", input);
  }
  latex(input: string) {
    return this.call<TransformResult>("latex", input);
  }
  collect(input: string, variable: string) {
    return this.call<TransformResult>("collect", input, variable);
  }
  apart(input: string, variable: string) {
    return this.call<TransformResult>("apart", input, variable);
  }
  derivative(input: string, variable: string) {
    return this.call<TransformResult>("derivative", input, variable);
  }
  subs(input: string, assignments: string, simplifyResult: boolean) {
    return this.call<TransformResult>("subs", input, assignments, simplifyResult);
  }
  laplace(input: string, timeVar: string) {
    return this.call<TransformResult>("laplace", input, timeVar);
  }
  ilaplace(input: string, freqVar: string) {
    return this.call<TransformResult>("ilaplace", input, freqVar);
  }
  vectorOp(op: string, fieldSemi: string, varsCsv: string) {
    return this.call<TransformResult>("vectorOp", op, fieldSemi, varsCsv);
  }
  stirling(variable: string, terms: number) {
    return this.call<TransformResult>("stirling", variable, terms);
  }
  gcd(list: string) {
    return this.call<TransformResult>("gcd", list);
  }
  lcm(list: string) {
    return this.call<TransformResult>("lcm", list);
  }
  isprime(n: string) {
    return this.call<TransformResult>("isprime", n);
  }
  nextprime(n: string) {
    return this.call<TransformResult>("nextprime", n);
  }
  divisors(n: string) {
    return this.call<TransformResult>("divisors", n);
  }
  totient(n: string) {
    return this.call<TransformResult>("totient", n);
  }
  cfrac(value: string) {
    return this.call<TransformResult>("cfrac", value);
  }
  mod(args: string) {
    return this.call<TransformResult>("mod", args);
  }
  powmod(args: string) {
    return this.call<TransformResult>("powmod", args);
  }
  modinv(args: string) {
    return this.call<TransformResult>("modinv", args);
  }
  crt(system: string) {
    return this.call<TransformResult>("crt", system);
  }
  solveIneq(lhs: string, rhs: string, op: string, variable: string) {
    return this.call<TransformResult>("solveIneq", lhs, rhs, op, variable);
  }
  series(input: string, variable: string, center: string, order: number) {
    return this.call<TransformResult>("series", input, variable, center, order);
  }
  fit(data: string, model: string, degree: string) {
    return this.call<FitResult>("fit", data, model, degree);
  }
  stats(data: string) {
    return this.call<StatsResult>("stats", data);
  }
  seq(termsCsv: string) {
    return this.call<SeqResult>("seq", termsCsv);
  }
  limit(input: string, variable: string, point: string, direction: string) {
    return this.call<LimitResult>("limit", input, variable, point, direction);
  }
  mlimit(input: string, xVar: string, a: string, yVar: string, b: string) {
    return this.call<LimitResult>("mlimit", input, xVar, a, yVar, b);
  }
  sum(term: string, variable: string, lo: string, hi: string) {
    return this.call<SumResult>("sum", term, variable, lo, hi);
  }
  product(term: string, variable: string, lo: string, hi: string) {
    return this.call<SumResult>("product", term, variable, lo, hi);
  }
  rsolve(recurrence: string, conditionsCsv: string) {
    return this.call<RsolveResult>("rsolve", recurrence, conditionsCsv);
  }
  dsolve(ode: string, conditionsCsv: string) {
    return this.call<DsolveResult>("dsolve", ode, conditionsCsv);
  }
  integrate(input: string, variable: string) {
    return this.call<IntegrateResult>("integrate", input, variable);
  }
  integrateDefinite(input: string, variable: string, lo: string, hi: string) {
    return this.call<DefiniteResult>("integrateDefinite", input, variable, lo, hi);
  }
  solve(input: string, variable: string, lo: number, hi: number, useRange: boolean) {
    return this.call<SolveResult>("solve", input, variable, lo, hi, useRange);
  }
  solveSystem(input: string, varsCsv: string) {
    return this.call<SystemResult>("solveSystem", input, varsCsv);
  }
  evaluate(input: string, bindings: string) {
    return this.call<EvaluateResult>("evaluate", input, bindings);
  }
}

/** Load the WASM engine and wrap it. Rejects with EngineLoadError if unbuilt. */
export async function createEngine(): Promise<Engine> {
  const mod = await loadEngineModule();
  return new WasmEngine(mod);
}
