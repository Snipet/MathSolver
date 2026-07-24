// User-function application expansion (beta-reduction) against the engine.
// Shared by the grapher's row expander (resolveRow) and the console line
// expander so the capture-avoiding substitution lives in exactly one place.
import { call } from "../engine";
import { splitTopLevelCommas } from "./classify";
import { type AnyCall, freshPlaceholders, findInnermostAppl } from "./functions";
import type { FnBinding } from "./registry";

/** How a call site looks up its function (grapher vs. console registry). */
export type GetFn = (name: string) => FnBinding | undefined;

/**
 * Beta-reduce: substitute `args` for `params` in `body`, capture-avoiding.
 * Two phases (params → fresh placeholders → args) because the engine `subs`
 * applies its CSV sequentially, so a direct `a=b,b=a` would corrupt `f(a,b)=a/b`
 * called `f(b,a)` into `a/a`. Placeholders are fresh single letters that appear
 * in neither the body nor any argument.
 */
export async function betaReduce(
  params: string[],
  body: string,
  args: string[],
): Promise<string> {
  const ph = freshPlaceholders(params.length, `${body} ${args.join(" ")}`);
  const r1 = await call("subs", [body, params.map((p, k) => `${p}=${ph[k]}`).join(","), false]);
  if (!r1.ok) throw new Error(r1.error);
  const r2 = await call("subs", [r1.plain, ph.map((z, k) => `${z}=(${args[k]})`).join(","), false]);
  if (!r2.ok) throw new Error(r2.error);
  return r2.plain;
}

/**
 * Reduce one user-function call `c` in `s` and splice the (parenthesized)
 * result back. A trailing prime run differentiates the body first. Args stay
 * raw (not env-resolved) so session vars / the axis resolve later. An
 * unregistered name is left untouched (stays implicit multiplication).
 */
export async function spliceApplication(c: AnyCall, s: string, getFn: GetFn): Promise<string> {
  const fn = getFn(c.name);
  if (!fn) return s;
  const args = splitTopLevelCommas(c.inner).map((a) => a.trim());
  if (args.length !== fn.params.length) {
    throw new Error(`${c.name} expects ${fn.params.length} argument(s), got ${args.length}`);
  }
  let body = fn.body;
  if (c.primes > 0) {
    if (fn.params.length !== 1) {
      throw new Error("prime notation needs a single-argument function");
    }
    for (let k = 0; k < c.primes; k++) {
      const d = await call("derivative", [body, fn.params[0]]);
      if (!d.ok) throw new Error(d.error);
      body = d.plain;
    }
  }
  const reduced = await betaReduce(fn.params, body, args);
  return s.slice(0, c.start) + "(" + reduced + ")" + s.slice(c.end);
}

/**
 * Expand every user-function application in `text`, innermost-first, leaving
 * calc operators (diff/integral/…) untouched — the console's application-only
 * expander. Throws on arity / prime / substitution errors.
 */
export async function expandApplications(
  text: string,
  getFn: GetFn,
  fnNames: string[],
): Promise<string> {
  let s = text;
  for (let guard = 0; guard < 64; guard++) {
    const c = findInnermostAppl(s, fnNames);
    if (!c) break;
    s = await spliceApplication(c, s, getFn);
  }
  return s;
}
