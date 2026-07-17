// Typed async client for the WASM engine worker.
//
// - Single worker instance, lazily started; requests are matched by id.
// - A request exceeding its timeout terminates and respawns the worker
//   (protecting the UI from pathological inputs), rejecting all pending
//   requests.
import type { EngineApi, EngineFn, WorkerRequest, WorkerResponse } from "./types";

const DEFAULT_TIMEOUT_MS = 20_000;

interface Pending {
  resolve: (v: unknown) => void;
  reject: (e: Error) => void;
  timer: ReturnType<typeof setTimeout>;
}

interface ReadyWaiter {
  resolve: () => void;
  reject: (e: Error) => void;
}

let worker: Worker | null = null;
let nextId = 1;
const pending = new Map<number, Pending>();
let readyWaiters: ReadyWaiter[] = [];
let isReady = false;
let readyError: Error | null = null;

function failReady(err: Error) {
  readyError = err;
  readyWaiters.forEach((r) => r.reject(err));
  readyWaiters = [];
}

function spawn(): Worker {
  const w = new Worker(new URL("./worker.ts", import.meta.url), { type: "module" });
  w.onmessage = (ev: MessageEvent<WorkerResponse>) => {
    const msg = ev.data;
    if ("ready" in msg) {
      if (msg.ready) {
        isReady = true;
        readyWaiters.forEach((r) => r.resolve());
        readyWaiters = [];
      } else {
        failReady(new Error(`engine failed to load: ${msg.error}`));
        failAll(readyError!);
      }
      return;
    }
    const p = pending.get(msg.id);
    if (!p) return;
    pending.delete(msg.id);
    clearTimeout(p.timer);
    if (msg.ok) p.resolve(msg.result);
    else p.reject(new Error(msg.error));
  };
  w.onerror = (e) => {
    const err = new Error(`engine worker error: ${e.message || "failed to start"}`);
    if (!isReady) failReady(err);
    failAll(err);
  };
  return w;
}

function failAll(err: Error) {
  for (const [, p] of pending) {
    clearTimeout(p.timer);
    p.reject(err);
  }
  pending.clear();
}

function ensureWorker(): Worker {
  if (!worker) {
    isReady = false;
    readyError = null;
    worker = spawn();
  }
  return worker;
}

function restart() {
  worker?.terminate();
  worker = null;
  isReady = false;
  failAll(new Error("computation timed out; the engine was restarted"));
}

/**
 * Resolves once the WASM module has finished loading; rejects if the module
 * (or the worker itself) failed to load.
 */
export function engineReady(): Promise<void> {
  ensureWorker();
  if (isReady) return Promise.resolve();
  if (readyError) return Promise.reject(readyError);
  return new Promise((resolve, reject) => readyWaiters.push({ resolve, reject }));
}

export function call<F extends EngineFn>(
  fn: F,
  args: EngineApi[F][0],
  timeoutMs: number = DEFAULT_TIMEOUT_MS,
): Promise<EngineApi[F][1]> {
  const w = ensureWorker();
  if (readyError) return Promise.reject(readyError);
  const id = nextId++;
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      pending.delete(id);
      restart();
      reject(new Error(`computation timed out after ${Math.round(timeoutMs / 1000)}s`));
    }, timeoutMs);
    pending.set(id, { resolve: resolve as (v: unknown) => void, reject, timer });
    const req: WorkerRequest = { id, fn, args: args as unknown[] };
    w.postMessage(req);
  });
}
