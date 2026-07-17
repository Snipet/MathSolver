// Module worker hosting the WASM engine. Each request calls one bound
// function (which returns a JSON string) and posts back the parsed object.
import createMathSolverModule from "../wasm/mathsolver.js";
import wasmUrl from "../wasm/mathsolver.wasm?url";
import type { WorkerRequest, WorkerResponse } from "./types";

const modulePromise = createMathSolverModule({
  locateFile: (path: string) => (path.endsWith(".wasm") ? wasmUrl : path),
});

function post(msg: WorkerResponse) {
  (self as unknown as Worker).postMessage(msg);
}

function describe(e: unknown): string {
  // Emscripten aborts often throw numbers/strings rather than Errors.
  if (e instanceof Error) return e.message;
  if (typeof e === "string") return e;
  return `engine internal error (${String(e)})`;
}

modulePromise.then(
  () => post({ id: -1, ready: true }),
  (e: unknown) => post({ id: -1, ready: false, error: describe(e) }),
);

self.onmessage = async (ev: MessageEvent<WorkerRequest>) => {
  const { id, fn, args } = ev.data;
  try {
    const mod = await modulePromise;
    const f = (mod as unknown as Record<string, (...a: unknown[]) => string>)[fn];
    if (typeof f !== "function") {
      post({ id, ok: false, error: `unknown engine function '${fn}'` });
      return;
    }
    post({ id, ok: true, result: JSON.parse(f(...args)) });
  } catch (e) {
    post({ id, ok: false, error: describe(e) });
  }
};
