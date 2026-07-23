# Planning: Enterprise, Platform & Product — the Other Axis

Status: **planning** — strategic direction, not a per-feature contract. Each
project below graduates to its own `docs/proposals/<name>.md` (DESIGN.md
contract standard) before implementation. Grounded in a full read of the
current tree (v0.6.0, 2026-07) and cross-referenced with docs/ROADMAP.md and
docs/proposals/next-features.md.

Goal: chart the dimension the existing planning corpus does **not** cover.
Every prior doc — docs/ROADMAP.md, docs/proposals/next-features.md (T1–T10),
docs/proposals/complex-domain.md, inequalities.md, variable-assignment.md — is
about **math capability** or **web UX**. None of them proposes, scopes, or even
mentions a backend, an API, auth, accounts, cloud persistence, collaboration,
licensing, packaging, a plugin SDK, client SDKs, interop, observability, i18n,
or accessibility. This document owns that axis.

---

## 1. Two axes, and why this doc is about the second one

There are two independent directions in which MathSolver can become "more
commercial," and conflating them is the core planning error to avoid.

- **Axis A — commercial-CAS *capability*.** Feature-parity with
  Wolfram/Maple/SymPy on the math itself: bignum, complex domain, assumptions,
  matrices, inequalities, number theory, units, step-by-step. This axis is
  **already well-planned** as T1–T10 in docs/proposals/next-features.md, with
  sized effort and a strong fuzz/acceptance gate. **We do not re-plan it here.**
  Where an enterprise feature needs a capability from Axis A (e.g. the
  assessment core leans on `simplify` + `compare_expr`), we reference it and
  move on.

- **Axis B — the enterprise / platform / product dimension.** How the engine is
  *distributed, integrated, governed, sold, and trusted*: a stable API and
  SDKs, an actually-existing MCP server, accessibility conformance, notebook
  interop/export, a LICENSE, signed releases, an opt-in sync/collaboration
  layer, and a self-hostable control plane for teams. **No existing doc touches
  any of this.** It is greenfield, and it is where the "toy vs. adoptable
  product" line is drawn for buyers who are not evaluating our integrator.

next-features.md is literally subtitled "Bridging the Gap to Commercial CAS,"
but it defines "commercial" purely as *math* parity. That leaves Axis B
unclaimed. This proposal is the counterpart: the gap to a commercial
*product*, not a commercial *engine*.

A crucial early observation, developed in §5: the two axes touch **two
different founding invariants**, and they almost never collide. Axis A extends
**engine purity** (stateless, real-domain, 7-node AST — extended the way
next-features.md §3 already sanctions). Axis B extends the **privacy/deployment
stance** (nothing sent to a server, static, offline). Because they are
separable, roughly 80% of Axis B can ship with *zero* privacy-doctrine cost —
and should ship first.

---

## 2. Where we stand

MathSolver today is a single-player, offline, 100%-static product: a C++23
engine compiled to a WASM ES6 module (36-export "strings in, JSON out" embind
ABI, `wasm/bindings.cpp:1091-1127`) hosted in a Web Worker
(`web/src/lib/engine/worker.ts`), a Svelte SPA that persists to seven
`localStorage` keys, and a native CLI/REPL built from source. That architecture
is a genuine privacy asset (§5) — and simultaneously the ceiling on every
enterprise axis. Verified against the tree:

| Capability | Today | Evidence |
|---|---|---|
| Backend / compute service | ✗ none — engine reachable only as CLI or in-browser WASM | repo-wide grep for rest/grpc/http-server/express/flask → 0 hits; `README.md:471-472` |
| REST/gRPC API, OpenAPI | ✗ none | no service binary anywhere in tree |
| Auth / accounts / SSO / SCIM | ✗ none — no identity surface at all | all state is `localStorage` + share links |
| Cloud persistence / cross-device | ✗ none — per-browser only | `web/src/lib/{history,vars,theme}.svelte.ts`, `notebook/*.svelte.ts` |
| Collaboration / sharing of notebooks | ✗ only the *grapher* has a share link | `web/src/lib/graph/share.ts`; no share code in `notebook/*` |
| Notebook export / interop | ✗ LaTeX text only; `NotebookCell.svelte` has **no copy affordance** | only `GraphCalculator.savePNG` + result-card `CopyButton` exist |
| **LICENSE** | ✗ **none anywhere** — default all-rights-reserved | no `LICENSE`/`COPYING`/`NOTICE`; `web/package.json` `"private": true` |
| Third-party plugins / SDK | ✗ compiled-in C++, hand-enumerated; no dynamic loading | `plugins/register_builtin.cpp`; grep dlopen/dlsym → 0 |
| **MCP server** (claimed in docs) | ✗ **does not exist** | `next-features.md:22-23` is the only "mcp" in the tree |
| Client SDKs (npm / PyPI) | ✗ nothing published | `web/package.json` `"private": true`; no release job |
| Signed releases / SBOM / provenance | ✗ zero git tags, no Releases, Actions pinned to mutable tags | `git tag -l` empty; `ci.yml` `@v4`/`@v14`/`@v3` |
| Observability / telemetry | ✗ none | grep telemetry/metrics/sentry/otel → 0 |
| Accessibility (WCAG/VPAT/MathML) | ✗ no VPAT; only KaTeX-reconstructed presentation MathML | `web/src/lib/katex-action.ts`; no `prefers-reduced-motion`; tabs lack `aria-controls` |
| i18n / localization | ✗ hardcoded English, `lang="en"` fixed | `web/index.html:2`; English literals throughout |
| Security / compliance posture | ✗ no `SECURITY.md`, no CSP, no data-handling doc | `.github/` has only `workflows/`; no CSP in `index.html`/`vite.config` |

Note the two integrity gaps a due-diligence review flags immediately: **the
absent LICENSE** (blocks all legal review and every publish/redistribute path)
and **the phantom MCP server** (a documented capability that does not exist).
Both are cheap to fix and both gate other work.

---

## 3. The enterprise features, tiered by doctrine cost & leverage

The tiering here is deliberately **by doctrine cost**, not by glamour. Tier 0
is doctrine-pure or doctrine-*amplifying* and captures the developer,
education, and compliance channels with zero privacy risk. Tier 1 is a single,
conscious, self-hostable server extension. Tier 2 is demand-gated — build only
when a real customer funds it. The strategic mistake would be starting a
backend before the large Tier-0 surface is exhausted.

Effort scale **S/M/L/XL** and risk match next-features.md.

### Tier 0 — doctrine-pure foundations & wins (ship first)

These require no server, no accounts, and no change to the privacy stance.
Several *strengthen* the offline stance by making it a reviewable asset.

---

**E0. LICENSE + IP foundation** *(the absolute gate)*

- **Problem.** There is no `LICENSE`/`COPYING`/`NOTICE` and `web/package.json`
  is `"private": true`. Under default copyright this is all-rights-reserved: no
  OSPO/legal review can approve it, no customer may self-host or redistribute,
  no npm/PyPI publish is possible, and there is no legal vehicle to sell
  anything. **Every other item in this document is gated behind this file.**
- **Sketch.** Add a root `LICENSE` — recommended **Apache-2.0** for the entire
  current codebase (`src/`, `include/`, `apps/`, `wasm/`, `plugins/`, `web/`).
  Apache's explicit patent grant matters for a CAS with algorithmic content and
  is what enterprise legal teams prefer for embedding/SDK/plugin reuse;
  permissive licensing maximizes the adoption the SDK and (future) ecosystem
  ambitions depend on. Add `SPDX-License-Identifier` headers, a `NOTICE`, a
  `THIRD-PARTY-LICENSES.md` inventory (KaTeX MIT, Catch2 BSL-1.0, Emscripten
  toolchain, puppeteer-core dev-only), and set the `license` field in
  `web/package.json`. Collapse the `0.6.0` version drift between
  `CMakeLists.txt` and `web/package.json` by making the git tag the single
  source (ties to E3). **Defer** the `COMMERCIAL.md`/dual-license machinery
  until there is a control plane to sell (§6) — do not couple the license file
  to a revenue bet that may never ship.
- **Effort S. Risk low.** Doctrine-fit: purely additive legal artifact, zero
  conflict with any invariant. It is the enabling substrate for everything else.

---

**E1. SECURITY.md + coordinated vulnerability disclosure**

- **Problem.** No `SECURITY.md`, no disclosure channel, no security contact
  (`.github/` contains only `workflows/`). SIG/CAIQ procurement questionnaires
  explicitly require a documented vuln-reporting path, and a researcher who
  finds a parser DoS or a share-link decode flaw has nowhere to report it.
- **Sketch.** Add `/.github/SECURITY.md` declaring supported versions, scope
  (engine, `wasm/bindings.cpp`, web app, plugins), a private intake via GitHub
  Private Vulnerability Reporting (free, no infra — honors the no-backend
  stance), a coordinated-disclosure window (90 days), and a contact/PGP key.
  Serve `/.well-known/security.txt` (RFC 9116) from `web/public/`. Add a
  **one-page** `docs/security/THREAT-MODEL.md` enumerating the three real
  surfaces: untrusted LaTeX/ASCII into the ~1070-line parser, the already
  hardened share-link decoder (`graph/share.ts` — 64 KB cap, bounded counts,
  never-throw), and WASM DoS (the engine client `engine/index.ts` already
  terminates+respawns the worker on the 20 s timeout — `DEFAULT_TIMEOUT_MS =
  20_000`; `ALLOW_MEMORY_GROWTH` is the memory-exhaustion vector to note).
- **Effort S. Risk low.** Doctrine-fit: GitHub PVR + a static `security.txt`
  need no server. Feeds the security whitepaper in E4.

---

**E2. Static-app security hardening: CSP + security headers**

- **Problem.** No Content-Security-Policy or security-header configuration
  exists (no `_headers`, nothing in `index.html`/`vite.config`), yet the app
  renders user-supplied LaTeX through KaTeX and decodes untrusted share-link
  state client-side. A pen-tester or procurement scanner flags a missing CSP
  instantly.
- **Sketch.** Add a Cloudflare Pages `_headers` file (and equivalent nginx
  config for the E5 container) setting CSP with `script-src 'self'
  'wasm-unsafe-eval'` (WASM instantiation needs it) and a KaTeX-compatible
  `style-src`, no external hosts (matching zero-egress, so CSP doubles as an
  exfiltration guard), `frame-ancestors`/`X-Frame-Options`,
  `X-Content-Type-Options`, `Referrer-Policy`, `Permissions-Policy`, and
  COOP/COEP. **Caveat:** GitHub Pages *cannot* serve custom headers — the Pages
  deploy needs a `<meta http-equiv>` CSP fallback, while `_headers`/nginx covers
  Cloudflare and the E5 bundle; state this or the header story silently fails on
  one of the two live hosts. Regression-gate with the existing puppeteer suites
  so a mis-scoped policy cannot silently break WASM/KaTeX.
- **Effort S. Risk medium** (CSP breaking WASM/KaTeX — mitigated by the browser
  test suite). Doctrine-fit: hardens static delivery, no backend.

---

**E3. Signed, SBOM-bearing, tagged releases + supply-chain hardening**

- **Problem.** Nothing is published: no git tags, no GitHub Releases, no
  signed/checksummed artifacts, no SBOM, no provenance. Every workflow pins
  third-party Actions to *mutable* major tags (`actions/checkout@v4`,
  `mymindstorm/setup-emsdk@v14`, `cloudflare/wrangler-action@v3`) — a live
  supply-chain exposure on every build and deploy. The enterprise buys
  *installed* software here and cannot verify the artifacts it would run.
- **Sketch.** Stage it. **Tier-1 (do now):** a `release.yml` on `v*` tags that
  builds CLI binaries + the WASM/web dist, publishes a GitHub Release with
  `SHA256SUMS`, generates a CycloneDX SBOM (the engine's real "no deps beyond
  stdlib" becomes a machine-readable asset; the web SBOM derives from the
  committed `package-lock.json`), repins **all** Actions to full commit SHAs,
  and adds CodeQL (C++ + JS), `dependabot.yml`, and secret scanning as PR gates.
  Derive the version from the tag (kills the `0.6.0` drift). Also wire the
  *strongest existing-but-ungated* assets into a scheduled job — the 45k+
  round-trip/differential fuzzer (`tools/run_fuzz.sh`) and the ASan/UBSan build
  (`MATHSOLVER_SANITIZE`, present in `CMakeLists.txt` but invoked by no
  workflow) — so a questionnaire can cite *enforced* fuzzing + sanitizers.
  **Tier-2 (defer):** cosign keyless signing + Fulcio/Rekor + SLSA provenance
  attestation — high value only once artifacts are actually consumed downstream.
- **Effort M. Risk low.** Doctrine-fit: entirely CI-side; strengthens trust in
  an offline artifact. Depends on E0 (can't publish without a license).

---

**E4. Enterprise Trust & Compliance Pack** *(convert the doctrine into
procurement artifacts)*

- **Problem.** The "nothing is sent to a server" claim is marketing prose, never
  a reviewable artifact, so the product's single biggest latent advantage is
  invisible in procurement. Buyers require, before purchase, a data-handling
  statement, a security-questionnaire response, and — uniquely load-bearing for
  a *math* product — an accessibility conformance report. The 100%-client-side
  architecture already satisfies most GDPR/CCPA/residency demands by
  construction; there is simply no document a reviewer can accept.
- **Sketch.** Add `docs/compliance/`: **(1)** `DATA-HANDLING.md` giving the
  precise data flow — stateless engine, computation in-browser via the WASM
  worker, state confined to the seven `mathsolver.*` `localStorage` keys and to
  *user-initiated* share-link URL fragments (documented as user-controlled,
  zero server retention), with a GDPR/CCPA analysis concluding there is no
  server-side processor relationship for computation. This file **must** own the
  one real leak — share-links embedding expressions and resolved variable values
  into URLs and browser history — not gloss it (see E-miss "share-link control").
  **(2)** a one-page security whitepaper (architecture + E1 threat model +
  no-backend blast radius) and a pre-filled SIG-lite/CAIQ-lite. **(3)** an
  export-control note (a CAS with no crypto almost certainly self-classifies as
  EAR99/NLR — but it must be *stated* to clear gov/defense procurement). **Split
  out and defer the VPAT/ACR** until real accessibility remediation lands (E6):
  an honest VPAT today reads mostly "Partially/Does Not Support" for math a11y
  and hurts more than it helps; an over-claimed one is a liability.
- **Effort M. Risk low** (the only risk is truthfulness — scope conservatively,
  never over-claim WCAG). Doctrine-fit: highest-*leverage* item; it monetizes
  the privacy stance as sellable collateral without touching the engine.

---

**E5. Enterprise deployment package: signed, air-gapped self-host bundle**

- **Problem.** On-prem/air-gapped deployment — "nothing leaves our network" — is
  the hard gate for gov/defense/finance/critical-infrastructure buyers, and the
  static/WASM/no-backend design is air-gap-friendly *by construction*. But the
  only deployment paths are public GitHub Pages and Cloudflare Pages, and the
  only container is a dev-only Vite image (`compose.yaml` runs `npm run dev`).
  Regulated buyers cannot `git clone && build`.
- **Sketch.** A production multi-stage `Dockerfile` (build WASM+web in the pinned
  `emscripten/emsdk:6.0.3` image already used across CI, then serve `web/dist`
  from minimal nginx/caddy with the E2 CSP/headers baked in) plus an **offline
  bundle** release asset: a self-contained tarball of the built static site +
  `SHA256SUMS` + SBOM (from E3), runnable behind any static file server with
  zero egress. Add `docs/deploy/AIR-GAP.md` and a supported-versions/upgrade
  policy. **De-scope the Helm chart** — a static site's "server" is any file
  server; K8s ceremony no buyer has asked for is over-build. Add Helm only when
  a real customer demands it.
- **Effort M. Risk medium.** Doctrine-fit: pure amplification — the offline
  stance *becomes* the enterprise product; no backend, no server-side compute.
  Depends on E0 (redistribution license), E3 (signatures/SBOM), E2 (headers).

---

**E6. Accessible-math pipeline: native MathML from the AST + speech + explorer**

- **Problem.** Results render as visual KaTeX HTML; `katex-action.ts` yields only
  *presentation* MathML reconstructed from a LaTeX string (semantically lossy),
  and the grapher, plugin tables, and console cells expose no speech, alt-text,
  or keyboard exploration. Under WCAG 2.1 SC 1.1.1/1.3.1 that fails AA, and
  there is no VPAT. This is a **hard procurement gate**: Section 508, EN 301 549,
  and most education/government RFPs require an accessibility conformance report
  before purchase, so MathSolver is currently un-buyable by those channels
  regardless of engine quality. MathML is *also* the interchange substrate E7,
  E-edu, and the code-printer siblings all build on.
- **Sketch.** Add a first-class MathML emitter to the printer track
  (`include/mathsolver/printer.hpp`): a `to_mathml(Expr, {presentation|content})`
  sibling that walks the same 7-node AST the LaTeX printer walks. Emitting
  directly from `Kind={Number,Symbol,Constant,Add,Mul,Pow,Function}` produces
  **true Content MathML** (`apply`/`plus`/`times`/`power`/`csymbol`) that the
  KaTeX-from-LaTeX path structurally cannot, plus Presentation MathML carrying
  MathML-4 `intent` attributes to disambiguate speech. Reuse the printer's
  round-trip discipline with a new MathML acceptance battery. Expose a `mathml`
  embind export next to `latex`. On the frontend: generate a
  Speech-Rule-Engine-style spoken string from the same AST walk (one C++
  function), attach it as `aria-label` on the KaTeX host, add an arrow-key
  subexpression explorer, add `prefers-reduced-motion` (the shimmer at
  `App.svelte:971-989` animates unconditionally), a skip-to-content link, and
  `aria-controls` linking the mode tabs (`App.svelte:660-691`) to their panels.
  **Split the engineering from the audit:** ship the MathML+speech+explorer
  first (doctrine-pure); the external WCAG audit + VPAT is a *dependent* business
  deliverable that cannot be promised until the hardest sub-problem — the
  HTML5-canvas grapher (`GraphCanvas`), which has no DOM semantics and needs a
  parallel data-table/sonification path — is actually addressed.
- **Effort L. Risk medium.** Doctrine-fit: a pure stateless engine function on
  the existing AST, 100% client-side, no new node types — it *converts* the
  offline stance into a procurement asset.

---

**E7. Notebook & session interop: export/import (.ipynb, Markdown, HTML, PDF,
CSV) + per-cell copy**

- **Problem.** The Console notebook — the primary reusable work product — is
  `localStorage`-only with no export, and `NotebookCell.svelte` has **no copy
  affordance**: a console result cannot even be copied out individually. The
  only export anywhere is a graph PNG. Combined with the silent 80-cell
  persistence cap that discards oldest cells, real work is trapped and lossy.
- **Sketch.** Add `web/src/lib/notebook/export.ts` serializing the cell model to:
  **(a)** Jupyter `.ipynb` with a `mathsolver` kernelspec (LaTeX/MathML in
  markdown+output metadata) — framed as **high-fidelity export with best-effort
  import**, since no runnable kernel exists yet, so re-opened notebooks only
  re-run MathSolver-authored cells; **(b)** Markdown with embedded LaTeX/MathML;
  **(c)** a single self-contained HTML file (inline KaTeX CSS + MathML, reusing
  the `PluginResult` renderer, CSP-safe inline pattern); **(d)** print-to-PDF via
  a print stylesheet (no new dependency); **(e)** CSV in/out for the grapher
  regression tables and `fit()`/`stats()` inputs. Add a per-cell `CopyButton`
  (LaTeX / MathML / plain) reusing existing `CopyButton`/`CopyField`. Trigger an
  export-before-trim prompt when the cell cap is about to discard work.
- **Effort M. Risk low.** Doctrine-fit: fully client-side, reuses the hardened
  `graph/share.ts` pattern and the E6 MathML emitter; engine stays pure.

---

**E8. Local-first workspace document + sync-adapter seam**

- **Problem.** All seven `mathsolver.*` `localStorage` stores are per-browser and
  siloed, so work is trapped: clearing site data, switching browsers, or moving
  devices loses everything, and each store silently trims (notebook CAP 80,
  history 50, docs 40) and swallows quota errors. There is no backup, export, or
  recovery path.
- **Sketch.** Introduce a `Workspace` aggregate that snapshots all `mathsolver.*`
  keys into one **versioned** JSON document, reusing the `{v:1,…}` envelope every
  store already emits and the never-throw/bounds discipline of
  `graph/share.ts::decodeState`. Define a thin `SyncAdapter` interface
  (`load`/`save`/`subscribe`); the default `LocalStorageAdapter` reproduces
  today's behavior byte-for-byte, so the engine and doctrine are untouched. Ship
  the immediate offline value first — **Export/Import Workspace as a JSON file**
  (mirroring `GraphCalculator.savePNG`'s `toBlob`→anchor download) and migrate
  bulky data (cell results) to IndexedDB to end silent CAP trimming. Own a
  schema-migration framework (`{v:1}→{v:n}` up-migration + tolerant down-level
  reads) from day one — a client on an older build must not corrupt a newer
  synced document. **Do not over-abstract** the adapter for hypothetical
  backends (YAGNI); lead with Export/Import + IndexedDB. This seam is the single
  substrate every later cloud/self-host feature plugs into.
- **Effort L. Risk medium.** Doctrine-fit: pure additive layer above the
  stateless engine — exactly the "statefulness confined to the session/app
  layer" invariant variable-assignment.md §3 established for vars. Default
  adapter stays `localStorage`, so keep-local remains the default.

---

**E9. Published engine SDKs: `@mathsolver/engine` (npm) — compute stays local**

- **Problem.** `web/package.json` is `private:true` and nothing is published. Any
  integrator must clone the repo, rebuild the WASM (`tools/build_wasm.sh`), and
  hand-wire a worker. The 36-export embind ABI is a genuine API reachable only
  in-process from bespoke in-repo code. This is the #1 "how do I actually use
  this engine in my product" blocker — and closing it needs no server.
- **Sketch.** Extract `web/src/lib/engine/{index.ts,worker.ts,types.ts}` —
  *already* a typed, id-matched, timeout-guarded async RPC client — into a
  standalone `@mathsolver/engine` package that bundles `mathsolver.js` +
  `mathsolver.wasm` and re-exports `call`/`engineReady` plus per-verb typed
  helpers. **Ship npm first** (fast, high-value). **Revise the Python path:** the
  Emscripten output is JS-glue targeting web/worker/node, **not** a standalone
  WASI module, so "load the same `.wasm` under `wasmtime`" does not work; the
  realistic PyPI path is a pybind11 module over `mathsolver_core` shipping native
  wheels across manylinux/macOS/Windows — a real, ongoing effort, treated as a
  *second* milestone. A Homebrew formula builds the CLI. All generated/validated
  against the frozen verb contract (E12).
- **Effort L. Risk medium.** Doctrine-fit: **strengthening** — computation runs
  in the consumer's own process/browser, so "an npm/pip math engine that phones
  home to nobody" turns the offline stance into the selling point. Hard
  prerequisite: **E0 (LICENSE)** — you cannot responsibly publish without one.

---

**E10. Real MCP server (stdio-local by default)** *(make the docs true)*

- **Problem.** `next-features.md:22-23` claims MathSolver "ships as … an MCP
  server." It does not exist ("mcp" appears once in the whole tree — that doc
  line). This is a documentation-integrity failure that fails due diligence, and
  it forfeits the hottest integration channel: a deterministic, self-verifying,
  never-silently-wrong CAS is exactly what LLM agents need for trustworthy math
  (the proven Wolfram "API for LLMs" pattern).
- **Sketch.** Ship `@mathsolver/mcp`, a thin server importing `@mathsolver/engine`
  (E9) — or, even more cheaply, shelling the existing CLI — and registering each
  engine verb as an MCP tool with a typed JSON input schema (`solve`, `integrate`,
  `derivative`, `simplify`, `limit`, `dsolve`, `laplace`, `series`, `solveSystem`,
  plus `plugins`/`pluginCall`). Each tool returns the JSON envelope **and**
  rendered LaTeX so the model shows its work, plus a `verified:true` flag on
  self-verified integrals/solutions that agents can key off — a real
  differentiator. Default transport is **stdio**, so the server runs locally and
  the tool call executes in-process: no network, no egress.
- **Effort S. Risk low.** Doctrine-fit: none with stdio — the tool computes on the
  user's machine, nothing egresses. Fixes a due-diligence defect *and* opens the
  agent channel; ship it early.

---

**E11. Embeddable `<mathsolver-calc>` web component + JS embed API**

- **Problem.** The engine is reachable only as the monolithic Svelte SPA or the
  CLI; there is no embeddable widget and no host-page API. Competitors spread
  through embeds (`Desmos.Calculator(node, opts)`, Wolfram Widget Builder) into
  blogs, docs, and LMS pages. This is the missing reach/adoption channel.
- **Sketch.** A framework-agnostic custom element (with an iframe fallback) that
  mounts the E9 worker client and reuses the existing `PluginResult`/`ResultSolve`
  renderers into any host DOM node. Public API mirroring Desmos:
  `MathSolver.mount(el, {expr, mode, readonly})`, `.setExpression()`,
  `.getResult()` returning the JSON envelope + LaTeX + MathML, and `change`/`submit`
  events. Because it embeds the same `mathsolver.wasm`, computation runs in the
  embedder's browser. Reuse the already-hardened `graph/share.ts` decoder for
  `#g=` grapher embeds. **This introduces the project's first external
  API-stability commitment** — a permanent semver + deprecation cost — so scope
  the v1 surface deliberately small.
- **Effort M–L. Risk low.** Doctrine-fit: reinforcing ("an embeddable calculator
  with no backend and no tracking"). Depends on E0 (publishable) and E9.

---

**E12. Freeze a versioned Engine API v1 (single-source verb contract)**

- **Problem.** The 36-export embind surface has no declared version or stability
  contract; the block/envelope schema is hand-duplicated across C++ `std::format`
  literals and `web/src/lib/engine/types.ts` with no shared source (drift risk),
  and `PluginResult.svelte`'s `{#if type===…}` chain has **no default branch**,
  so a newer/unknown block renders nothing silently. Every SDK/MCP/embed consumer
  binds to an unversioned, silently-changing surface.
- **Sketch.** Define **Engine API v1**: a machine-readable `verbs.json` (verb
  name, argument arity/types, envelope + block shapes) that becomes the **single
  source** from which the embind registration, the TS `types.ts` union, and the
  SDK stubs are generated/checked — killing C++↔TS drift. Add an `apiVersion`
  field to every envelope, a companion self-describing `GET`/introspection verb
  (so an old SDK can negotiate against a newer engine), a default/fallback block
  branch in `PluginResult.svelte` with a visible "update to view" notice, and a
  written SemVer + deprecation policy. Single-source the version
  (CMake → generated header → `package.json`).
- **Effort M. Risk low.** Doctrine-fit: neutral/supportive — pure release/contract
  discipline over the existing engine, no server, no privacy impact. Foundational:
  de-risks E9, E10, E11, and the Tier-1 daemon.

---

**E13. Templates & worksheet gallery** *(onboarding + the education seed)*

- **Problem.** New users face a blank console with no curated starting point, and
  education buyers want reusable teacher-authored activities. Both an adoption and
  an education-channel gap.
- **Sketch.** A curated, **bundled** gallery of starter worksheets as static
  assets, reusing the existing `cookbook.ts`/`reference.ts` curated-content
  pattern. Render in `NotebooksPanel.svelte` with one-click "open as new
  notebook" (= a `DocsStore.save` of the template's command lines). v1 needs no
  backend. Org-private libraries and a community gallery defer to Tier 2 and must
  carry the same signing/trust caution any untrusted-content surface needs.
- **Effort S. Risk low.** Doctrine-fit: fully static/offline; perfect fit.

---

**E14. Local-only diagnostics (Tier-0 telemetry): export, never transmit**

- **Problem.** There is zero observability, so if productized there is no way to
  learn which inputs crash the WASM engine or fail in specific browsers, and no
  support diagnostics. But naive telemetry directly negates "nothing sent to a
  server," and once a transmit path exists it tends to become default-on.
- **Sketch.** Build **only** the doctrine-pure tier: the engine client
  (`web/src/lib/engine/index.ts` + `worker.ts`) keeps an in-memory ring buffer of
  failing inputs + engine `version` + error, and the user can **export a redacted
  JSON diagnostic bundle** to attach to a bug report. Nothing is transmitted.
  Defer any transmitting tier until a paying self-host customer supplies their
  **own** collector (then: build-time opt-in, PII-free, pointed at the customer's
  OpenTelemetry/Sentry — never a MathSolver endpoint, public site ships OFF).
- **Effort S. Risk low.** Doctrine-fit: Tier-0 is local-only, genuinely useful,
  cheap. Do **not** build the OTel schema speculatively.

---

**E15. Symbolic-to-code generation (lambdify / code printers)** *(engineering
differentiator, printer-track sibling)*

- **Problem.** No path emits runnable C / JavaScript / Python-NumPy from the
  canonical AST — a *defining* CAS interop feature (SymPy `lambdify`, MATLAB
  `matlabFunction`/`ccode`). It is squarely in-wheelhouse: the engine already
  walks the 7-node AST to print LaTeX, and a code-printer is the same traversal.
- **Sketch.** A code-printer track alongside the E6 MathML emitter: `to_code(Expr,
  {c|js|python})` walking `Add/Mul/Pow/Function`, gated by the same round-trip
  discipline and acceptance batteries. Fully offline, no new nodes. Turns a
  verified symbolic result into a deployable numeric artifact — the "integrate
  MathSolver into my pipeline" value the platform axis wants.
- **Effort M. Risk low.** Doctrine-fit: pure stateless printer on the existing AST.

---

**E16. Installable PWA (service worker + web app manifest)**

- **Problem.** Despite the "runs in your browser" framing there is no service
  worker or manifest (`web/public/` holds only `favicon.svg` + `robots.txt`), so
  the app is not installable and not *guaranteed* offline after first load. Every
  consumer competitor (Wolfram, Symbolab, GeoGebra) ships installable apps, and a
  PWA is the reliable substrate for the offline exam mode in E-edu.
- **Sketch.** Add a service worker (precache the static WASM+web bundle) and a web
  app manifest. Reuses the existing static build entirely; makes the app
  installable and reliably offline. Doctrine-perfect and cheap.
- **Effort S–M. Risk low.** Doctrine-fit: reinforces the offline value prop; no
  backend.

---

**E17. WYSIWYG math input (`<math-field>`-style editor + virtual keyboard)**

- **Problem.** Input is a plain `textarea` (`ExpressionInput.svelte`) accepting
  only typed LaTeX/ASCII — awkward on touch and below the market bar (MathLive,
  MathType). Research is explicit that the real student input barrier is
  touch/LaTeX entry, not English-intent parsing.
- **Sketch.** Integrate (or build a lightweight equivalent of) a reusable visual
  math-field component with a virtual keyboard, exporting LaTeX/MathML into the
  existing parser so all parsing rigor and caret diagnostics are preserved. Also
  strengthens the E11 embed story (embedders want input UX) and is itself an
  accessibility win (keyboard-navigable structured input). Prefer this over
  rule-based natural-language input, which is a long tail of near-misses that
  reads as *more* broken than a predictable syntax.
- **Effort M. Risk medium.** Doctrine-fit: presentation-layer, client-side, engine
  untouched.

---

**E18. Education assessment core (offline) + exam mode**

- **Problem.** No assessment surface exists (no question model, no auto-grading,
  no exam mode) despite the engine already owning the symbolic-equivalence
  machinery CAS auto-graders (STACK, Möbius) are built on. Auto-grading by
  mathematical *equivalence* is the defining capability of the education market.
- **Sketch.** A pure/stateless engine layer: **(a)** `check_equivalent(student,
  expected, {domain, form-constraints})` layering three primitives already in the
  tree — structural `compare_expr` on `simplify(student)` vs `simplify(reference)`;
  symbolic-zero `simplify(student − reference) → 0` (reusing `cancel`/`together`);
  and numeric differential agreement (porting the `differential_eval` oracle from
  `tools/fuzz_roundtrip.cpp:269`, EvalError-on-both = agreement, 1e-9 tol). It
  reports the **strongest tier reached** and an honest "inconclusive," because
  equivalence is undecidable and real-domain numeric agreement can false-positive
  across branch cuts and removable singularities — the confidence semantics must
  be conservative and loudly documented, since graders will treat "equivalent" as
  ground truth. Domain/form constraints overlap **T5 assumptions**
  (docs/proposals/next-features.md) — coordinate so this does not reinvent an
  assumptions framework. **(b)** Verified, seed-deterministic parameterized
  problem generation (generate → solve → keep only if self-verified and "clean").
  **(c)** A GeoGebra-style locked-down **exam mode** — a near-free payoff of the
  already-offline guarantee (no network, tamper-evident). All client-side, zero
  backend; stands alone for the education market. Expose `check_equivalent` as
  `ms_check` (embind), a `check` CLI subcommand, and an MCP tool (E10).
- **Effort L. Risk medium.** Doctrine-fit: doctrine-perfect — pure engine,
  offline, self-verifying; exam mode is a *direct* payoff of the offline stance.

---

### Tier 1 — one conscious server extension (self-host, opt-in, keep-local default)

Build **at most one** backend, and only after the Tier-0 surface is exhausted.
It computes **no math** — the WASM engine remains the private core (§5). Both
items below share that single self-hostable service.

---

**E19. `mathsolver-serve`: self-hostable headless REST + OpenAPI compute daemon**

- **Problem.** There is no REST/gRPC/HTTP surface — the engine is reachable only
  as CLI or in-browser WASM. Organizations that want one *shared* on-prem instance
  (a university, an internal engineering service), or language-agnostic access,
  must reimplement a transport.
- **Sketch.** A new **non-WASM** target `mathsolver_serve` linking
  `mathsolver_core` **+ `mathsolver_plugins`** — which also fixes the confirmed
  CLI/plugin asymmetry (`CMakeLists.txt:110-111`, the CLI links core only). It
  reuses the exact `guarded([&]{…})` dispatch bodies from `wasm/bindings.cpp`:
  each verb becomes `POST /v1/{verb}` taking `{args:[…]}` and returning the
  identical JSON envelope — same code, different transport. Generate the OpenAPI
  3.1 spec from the E12 `verbs.json`. **Fold in the operability essentials as
  definition-of-done, not a later feature:** bearer API keys, per-key token-bucket
  rate limits (`X-RateLimit-Remaining`/`Retry-After`), `/healthz`+`/readyz`, and a
  basic Prometheus `/metrics` (request count/latency/error per verb+key) scraped
  by the customer's own stack. **Merge, don't build async jobs:** add a
  *synchronous* `POST /v1/batch` (array in, array of envelopes out, bounded by the
  same per-request limits); **drop** the async queue/webhook/durable-job apparatus
  — no CAS verb runs long enough to justify it, and durable job state is exactly
  the persisted-user-data surface the doctrine avoids. **Position it correctly:**
  for the common "backend math for my own app" case the E9 in-process SDK already
  wins (no network hop, no TLS, no DoS surface); the daemon's *unique* value is a
  shared on-prem instance + language-agnostic access.
- **Effort L. Risk medium.** Doctrine-fit: real tension with "nothing sent to a
  server," resolved **only** by the self-host/on-prem framing — v1 ships with **no
  hosted multi-tenant option**; the customer runs it inside their network, so
  "nothing sent to *our* server; you own the server." Additive layer above the
  unchanged pure engine.
- **Hard prerequisite (cross-cutting):** deterministic **per-request resource
  limits** — a hard memory cap + recursion/step budget — before it faces *any*
  caller. A CAS over HTTP is a DoS magnet (pathological factorials/integrands can
  OOM under `ALLOW_MEMORY_GROWTH` or spin); a wall-clock timeout alone is
  insufficient. This budget aligns with the deterministic/never-silently-wrong
  doctrine and also underpins any future untrusted-code surface.

---

**E20. Opt-in accounts & sync (self-hostable first; SSO as an enterprise tier)**

- **Problem.** No accounts, no cloud persistence, no cross-device continuity — the
  defining limitation for a commercial notebook product, where saved work must
  follow the user and be recoverable. Enterprise/education procurement gates
  additionally on federated SSO (SAML/OIDC) and, for regulated buyers, on
  self-host so data never leaves their network.
- **Sketch.** A remote `SyncAdapter` (from E8) backed by a small **stateless**
  sync service that stores **opaque** per-user workspace blobs. The CAS engine
  stays 100% client-side WASM; the server only stores/relays blobs and **never
  computes math**. Package it identically as a signed container for **self-host /
  on-prem** — ship that path *first* (best doctrine fit). **Stage the crypto
  decision up front (this is an architectural fork, not a toggle):** MVP =
  encryption-*at-rest* under a customer/tenant key, so server-mediated sharing,
  RBAC, and audit are *possible*; offer full **zero-knowledge E2E** as an explicit
  opt-in "private mode" that **consciously forgoes** server-side collaboration
  (you cannot re-key, index, or access-control ciphertext you cannot read, and a
  lost key = unrecoverable data). Add SSO (SAML/OIDC via a broker such as
  Keycloak/WorkOS) as a separate enterprise tier. Requires a **multi-device
  conflict story** — last-writer-wins backed by revision history for recovery, or
  a per-document CRDT — plus a visible "saved / syncing / stale / offline"
  indicator, or the sync layer recreates data loss at the account level. Also
  requires an **account/key-recovery flow** (recovery-key/escrow) up front, or
  encrypted-mode churn is guaranteed.
- **Effort XL. Risk high.** Doctrine-fit: highest-tension item — the project's
  first server. Reconciled by keep-local default, opt-in only, engine-untouched,
  and a self-host SKU. This is company-building, not a feature: **stage it**, and
  do not start until Tier 0 has shipped and demand is real.

---

### Tier 2 — demand-gated (build only when a contract funds it)

Named for completeness and to establish the dependency graph. Each is deep-stack
work with no value until a real customer needs it. **Do not start speculatively.**

- **E21. Team/Enterprise control plane** — org→workspace→folder→notebook
  multi-tenancy, RBAC (owner/editor/commenter/viewer), SCIM 2.0 provisioning, an
  immutable exportable **audit log** streamable to a SIEM, and an admin console.
  A **separate `server/` module, deliberately outside `mathsolver_core`** so the
  engine stays stateless; does no math. This is where SOC 2 scope, seat billing,
  and the commercial edition attach. **Effort XL. Risk high.** Depends on E0, E5,
  E20, E19. *The correct eventual revenue vehicle; the wrong thing to start.*
- **E22. Entitlement, metering & billing** — offline-signable license keys for
  the air-gap/CLI bundle (the Wolfram MathLM / MATLAB FlexLM analog regulated
  buyers expect), usage metering wired into a billing integration
  (Stripe/Metronome/Orb), seat/consumption plans. **This is the machinery every
  "commercial edition" claim silently assumes and no other item builds.** Without
  it E0's commercial license has nothing to enforce. **Effort L. Risk medium.**
  Depends on E21's gateway.
- **E23. Self-hostable LTI 1.3 Advantage tool provider** — OIDC third-party login
  + JWKS, Deep Linking 2.0, AGS grade-passback, NRPS roster sync; hosts the engine
  headless for grading and pushes **only scores** to the LMS while student work
  stays client-side. Plus QTI 3.0 item import/export (accessible items via E6
  MathML). The single biggest institutional pull — but a first-ever server,
  security-sensitive, competing with entrenched free incumbents (STACK, WeBWorK,
  Möbius). **Effort XL. Risk high.** Depends on E18 (offline core ships value
  first), E6, E20. Deliver the offline answer-key + printable worksheet path
  before the LTI provider.
- **E24. Real-time collaborative editing (CRDT) + presence** — model the cell
  array as Yjs/Automerge; because the engine is deterministic and stateless, only
  *inputs* cross the wire and each client recomputes identical verified output
  locally, so no math state is transmitted. **But** shared session variables and
  slider write-through re-introduce ordering hazards CRDTs are meant to avoid, and
  it is the lowest ROI-per-effort in the set for a fundamentally single-player
  audience. **Effort XL. Risk high.** Prefer async handoff (open-a-shared-copy via
  E7 + revision history) first; build full CRDT only if demand proves out.
- **E25. Cell comments / review threads** — anchored to the stable `Cell.id`,
  useful only once notebooks are shared/team-owned (annotating a private offline
  notebook has near-zero value), so it is a collaboration feature layered on E20 +
  sharing, not a standalone local-first item. **Effort M. Risk medium.** Depends
  on E20 + notebook sharing.
- **E26. Reliability / DR / SRE baseline** — the moment any tier persists user
  work (E20/E21), backup/restore with stated RTO/RPO, a status page, and an
  incident-response runbook become contractual obligations. Fold into the
  definition-of-done of any persisting service rather than treating as
  post-launch. **Effort M. Risk medium.**
- **E27. OSS governance scaffolding** — `CONTRIBUTING.md`, `CODEOWNERS`,
  `CODE_OF_CONDUCT.md`, issue/PR templates, and a **DCO/CLA** decision (without
  which inbound patches cannot cleanly be relicensed or IP-cleared). Cheap, and a
  natural companion to E0 the moment external contributions become possible.
  **Effort S. Risk low.**

**Dropped by design:** a **dynamic third-party plugin marketplace** (sandboxed
nested-WASM loading + signing + registry). It is the only proposal that breaks
*both* founding invariants at once — in-process purity *and* the no-server stance
— for the weakest payoff, with no demonstrated population of third-party
CAS-plugin authors and no enterprise buyer asking for it. Keep only the small
**per-invoke timeout/step budget** (folds into E19's resource limits) so a
runaway in-process plugin cannot hang the single WASM thread. The low-risk
*internal* plugin-contract hardening — JSON-Schema single-source for the block
union with codegen for both the C++ writers and `types.ts`, a default block
branch, and `engineApiVersion` validation — is worth doing as part of **E12**,
but do **not** freeze an *external* third-party ABI for consumers who do not
exist. Revisit a real dynamic-loading RFC only after demonstrated external-author
demand.

---

## 4. Cross-cutting prerequisites the tiers share

Three things underpin multiple features and must be owned explicitly, not
discovered late:

- **The LICENSE (E0)** gates E9/E10/E11/E13 publishing, E5 redistribution, and
  every commercial item. It is the literal first commit.
- **Deterministic per-request resource limits** (memory cap + step/recursion
  budget) underpin E19 and any future network/agent surface, and fold in the
  dropped-marketplace's one useful piece. The only guard today is the web client's
  20 s terminate-and-restart.
- **Structured machine-readable output** (MathML from E6, MathJSON, code from
  E15) is what makes the engine genuinely *integratable*; an API that only returns
  LaTeX strings forces every consumer to re-parse. These belong adjacent to the
  E12 verb contract.
- **A cross-host conformance suite** (CLI == WASM == REST return byte-identical
  envelopes for the same input) — extend the existing `resolution_vectors.tsv`
  pattern already shared between C++ tests and the web suite — is cheap insurance
  that de-risks the whole multi-host story and enforces E12 in CI. Without it,
  drift between CLI/WASM/REST is a silent regression class.

---

## 5. The central doctrine tension — and its resolution

This is the intellectual core of the proposal. The founding stance —
*stateless, real-domain, 7-node AST, nothing sent to a server, 100% static,
keep-local* — appears to be in direct conflict with every enterprise feature.
It is not, once three things are made explicit.

### 5.1 There are TWO invariants, not one

The corpus talks about "the doctrine" as a single wall. It is really two
independent invariants:

1. **Engine purity** — stateless, real-domain, 7-node canonical AST,
   exact-where-possible, every result self-verified.
2. **Privacy / deployment** — nothing sent to a server, static, offline,
   keep-local.

Almost every Axis-A math feature touches **only (1)**; almost every Axis-B
enterprise feature touches **only (2)**. They never actually collide (the one
proposal that stressed both at once — a sandboxed plugin marketplace — is
dropped above). Once separated:

- Invariant (1) is extended the way next-features.md §3 already sanctions:
  additively, *beside* the AST, with the fuzzers as the gate — exactly how
  assumptions, units, and inequalities all live **outside** the 7-node form.
- Invariant (2) is extended by the local-first + opt-in-server pattern below.

Treating them as one monolithic "don't touch the doctrine" rule would wrongly
block the cheap Tier-0 wins. Separating them lets us ship ~80% of the value with
**zero** privacy-doctrine cost.

### 5.2 The load-bearing reconciliation: the server never computes math

Computation and identity/persistence are **orthogonal**. In every credible
enterprise proposal here (E19 daemon, E20 sync, E21 control plane, E23 LTI) the
math still runs client-side in the same stateless WASM engine; the backend only
brokers **opaque blobs, identity, access control, and scores**. So "nothing sent
to a server" becomes "no *math* is sent to a server; you optionally sync
ciphertext/artifacts you control" — without lying. Make it an architectural law:

> **`mathsolver_core` and the WASM module take no network and no state
> dependency, ever. All server code lives in a separate module (`server/`,
> `mathsolver_serve`) that treats the engine as a pure library. Default every
> install to keep-local; make cloud/self-host strictly opt-in.**

This is the same boundary variable-assignment.md §3 drew for session state
("the engine stays pure; statefulness is confined to the session layer"),
generalized from one browser tab to an optional deployment.

### 5.3 The privacy stance is a latent *asset*, not just a constraint

The deepest irony: the very doctrine that appears to block enterprise adoption
already satisfies the **hardest and most expensive** enterprise gates *by
construction* — data residency, sub-processor risk, air-gap, breach
blast-radius, and the GDPR/CCPA processor relationship. SaaS vendors spend years
retrofitting "the data never leaves your device"; MathSolver has it natively. It
is invisible in procurement only because it is marketing prose, not a reviewable
artifact. This is why the recommended sequence front-loads the **doctrine-pure
enterprise-trust layer** (E0–E5): those are documents and CI, not engine
changes, yet they unlock edu/gov/enterprise review for an architecture that is
**already compliant**. Building a backend before shipping them would be strictly
lower ROI.

### 5.4 Explicit tension flags carried through the design

- **Client-side paywalls do not work here.** Any "free answer / paid steps" gate
  on a static, account-less app is trivially bypassed (view-source, localStorage,
  re-run the WASM). Progressive disclosure and hint ladders are kept purely as
  *pedagogy* (they also fix the "results are terminal, no drill-down" UX gap);
  any **real** gating couples to the opt-in server tier (E21), never to the
  static client. Never put "monetization" and "static client-side gate" in the
  same sentence.
- **Telemetry is where good intentions erode the promise.** Default-off,
  local-first, self-host-only (E14). The public site transmits nothing.
- **E2E-vs-collaboration is a fork, decided up front** (E20): encryption-at-rest
  MVP so collaboration/RBAC/audit are possible; full E2E as an explicit opt-in
  mode that consciously forgoes them.

---

## 6. Licensing & monetization

This section is opinionated because the absence of decisions here blocks
everything and invites drift.

### 6.1 Fix the LICENSE now (E0)

No `LICENSE`/`COPYING`/`NOTICE` exists; `web/package.json` is `"private": true`.
Default all-rights-reserved makes the code un-adoptable and un-publishable. Land
a permissive **Apache-2.0** license over the whole current codebase immediately,
with SPDX headers, `NOTICE`, and `THIRD-PARTY-LICENSES.md`. Apache (not MIT) for
the patent grant and OSPO/embed friendliness; **not AGPL** — it scares OSPOs and
kills the embed/SDK adoption the whole platform strategy depends on.

### 6.2 Open core is the model the doctrine implies

Value is captured **not in the engine** (which should be maximally adopted,
embedded, and offline to win the developer and education channels) **but in the
enterprise-trust layer** the offline architecture cannot itself provide:
governed sharing, SSO/SCIM, audit, on-prem support, IP indemnification, SLA. The
free/offline engine is the top of the funnel that makes the enterprise layer
sellable. Concretely:

- **Now:** permissive Apache-2.0 core. Nothing else.
- **When there is a control plane to sell (E21):** add a `COMMERCIAL.md` offering
  a separate commercial/OEM license carrying support, indemnification, and the
  right to ship the closed control plane. Do **not** author the dual-license
  machinery before the thing it licenses exists.

### 6.3 A first-pass edition matrix (drives sequencing, not a price sheet)

| Edition | Contains | Deployment |
|---|---|---|
| **Free / OSS** | Full offline engine + web app (Workbench/Console/Graph), CLI/REPL, PWA (E16), `@mathsolver/engine` SDK (E9), MCP server (E10), embed widget (E11), MathML/a11y (E6), notebook export (E7), templates (E13), offline assessment + exam mode (E18) | static site / self-host / `npm`/`pip` |
| **Pro** *(individual)* | Convenience cloud sync of one account's workspace (E20, encryption-at-rest), extended history, priority support | opt-in hosted or self-host |
| **Team** | Shared workspaces, notebook sharing with permissions, revision history, comments (E25), RBAC (E21) | self-host or hosted |
| **Enterprise** | SSO/SCIM, audit log to SIEM, `mathsolver-serve` on-prem (E19), air-gap bundle (E5), LTI (E23), entitlement/metering (E22), support SLA, compliance pack (E4) | on-prem / air-gapped |

The engine and the entire single-player experience stay **free and offline** in
every edition. Monetization lives strictly in the governance/trust/deployment
layer. **Entitlement enforcement (E22) is the prerequisite that makes any paid
edition real** — without it the matrix is aspirational.

---

## 7. Recommended sequence

The rule: **do not build the first server until Tier 0 has shipped.** Then build
exactly *one* self-hostable service (E19/E20) and let the rest reuse it.

**Release 1 — "Legally adoptable & trustworthy" (all doctrine-pure).**
E0 LICENSE → E1 SECURITY.md → E3 tagged/SBOM releases + Actions SHA-pinning →
E2 CSP/headers → E4 compliance pack (data-handling + whitepaper + SIG/CAIQ; VPAT
deferred). This is the cheapest, highest-ROI block and unblocks all publishing.

**Release 2 — "Integratable engine" (developer + agent channels).**
E12 Engine API v1 (single-source verb contract) → E9 `@mathsolver/engine` (npm
first) → E10 real MCP server (fixes the docs, opens agents) → E15 code
generation. Add E11 embed widget once E9 lands.

**Release 3 — "Adoptable by education & gov" (accessibility + interop, still
doctrine-pure).** E6 MathML/speech/explorer (then the VPAT once canvas-a11y is
addressed) → E7 notebook export + per-cell copy → E8 local-first workspace +
Export/Import → E13 templates → E16 PWA → E18 offline assessment + exam mode →
E17 WYSIWYG input. Ship E5 air-gap bundle here for on-prem evaluators.

**Release 4 — "One conscious server" (opt-in, self-host, demand-gated).**
E19 `mathsolver-serve` (with resource limits, keys, rate limits, health, metrics,
sync batch) as on-prem-only → E20 opt-in sync (self-host first, encryption-at-
rest MVP, SSO tier). E14 stays Tier-0 local-only throughout.

**Beyond — Tier 2, funded by real contracts only.** E21 control plane, E22
entitlement/billing, E23 LTI, E24 collaboration, E25 comments, E26 SRE/DR, E27
governance scaffolding.

In parallel and independent of all of the above, the **Axis-A** spine continues
on its own track per docs/proposals/next-features.md (T1 bignum → T2 complex,
docs/proposals/complex-domain.md → T3 step-by-step, then T4/T5/T6). Nothing here
competes with or reorders it; the two axes ship concurrently.

---

## 8. Explicit non-goals (and why)

- **A hosted multi-tenant SaaS as the default product.** The default is and
  stays keep-local/offline. Any cloud is opt-in, and the flagship enterprise
  deployment is **self-host / on-prem**. We are not building a "MathSolver Cloud"
  that competes with its own privacy value proposition.
- **Server-side math.** `mathsolver_core` and the WASM module never gain a
  network or state dependency. The engine computes the same way whether or not a
  customer runs a server. A backend brokers identity/blobs/scores — never
  expressions.
- **A dynamic third-party plugin marketplace.** Breaks both founding invariants
  for the weakest payoff, with no author population and no buyer demand (§3,
  Dropped). Plugins stay compiled-in; only the *internal* contract is hardened
  (E12). Revisit only on demonstrated external-author demand.
- **Async job orchestration / durable job queues / webhooks (as scoped in
  ideation).** No CAS verb runs long enough to justify it, and durable job state
  is exactly the persisted-user-data surface the doctrine avoids. A synchronous
  `POST /v1/batch` on E19 covers the real need.
- **Default-on or vendor-endpoint telemetry.** The public site transmits nothing;
  diagnostics are local-only export (E14). Any transmitting tier is build-time
  opt-in to a *customer-owned* collector.
- **A client-side paywall on the static app.** Architecturally unenforceable
  (§5.4); real gating couples only to the opt-in server tier.
- **A VPAT before the accessibility work is real.** An honest VPAT today reads
  mostly "Does Not Support" for math a11y; we ship the MathML/speech/explorer
  engineering (E6) first and attest only what the app actually passes.
- **Natural-language / LLM query parsing as an acquisition feature.** Rule-based
  NL is a long tail of near-misses that reads as more broken than predictable
  syntax; the real input barrier is touch entry, addressed by WYSIWYG input
  (E17). A *local* MCP server (E10) is the doctrine-clean way to serve the
  LLM/agent channel instead.
- **Re-planning Axis A.** Bignum, complex, assumptions, matrices, inequalities,
  number theory, units, step-by-step are owned by docs/proposals/next-features.md
  and docs/proposals/complex-domain.md. This document references them; it does not
  duplicate or reorder them.

---

## 9. Status

- **Everything in this document — planned; no proposal yet.** Each item graduates
  to its own `docs/proposals/<name>.md` before implementation.
- **Hard prerequisite for the category:** E0 (LICENSE) is unstarted and blocks
  all publishing/redistribution work.
- **Integrity fixes with a home now:** E0 (missing LICENSE) and E10 (the MCP
  server claimed in docs/proposals/next-features.md but absent from the tree).
