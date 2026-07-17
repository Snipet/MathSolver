<script lang="ts">
  import { untrack } from "svelte";
  import { call, engineReady } from "./lib/engine";
  import type { AnalyzeResult, SolveResult, SystemResult } from "./lib/engine/types";
  import { TABS, type TabId } from "./lib/tabs";
  import type { Ok, Outcome } from "./lib/outcome";
  import { history, type HistoryEntry } from "./lib/history.svelte";
  import { fmt, hasTopLevelSemicolon, numOr } from "./lib/format";
  import Tabs from "./lib/components/Tabs.svelte";
  import ThemeToggle from "./lib/components/ThemeToggle.svelte";
  import ExpressionInput from "./lib/components/ExpressionInput.svelte";
  import ParsePreview from "./lib/components/ParsePreview.svelte";
  import ResultCard from "./lib/components/ResultCard.svelte";
  import Plot from "./lib/components/Plot.svelte";
  import History from "./lib/components/History.svelte";

  // --- engine readiness ------------------------------------------------------
  let ready = $state(false);
  let version = $state("");
  let versionLoaded = $state(false);
  let engineError = $state("");
  engineReady()
    .then(async () => {
      const v = await call("version", []);
      ready = true;
      versionLoaded = true;
      if (v.ok) version = v.version;
    })
    .catch((e: unknown) => {
      versionLoaded = true;
      engineError = e instanceof Error ? e.message : String(e);
    });

  // --- workbench state -------------------------------------------------------
  let tab = $state<TabId>("simplify");
  const tabDef = $derived(TABS.find((t) => t.id === tab) ?? TABS[0]);

  let input = $state("");
  let computing = $state(false);
  let outcome = $state<Outcome | null>(null);

  // Per-tab parameters.
  let variable = $state("x");
  let systemVars = $state<string[]>([]);
  let useRange = $state(false);
  let rangeLo = $state(-100);
  let rangeHi = $state(100);
  let definite = $state(false);
  let intFrom = $state("0");
  let intTo = $state("1");
  let bindings = $state<Record<string, number>>({});
  let plotLo = $state(-10);
  let plotHi = $state(10);
  let plotD = $state(false);
  let plotA = $state(false);
  let plotNonce = $state(0);
  let antiAvailable = $state(true);

  // --- live parse preview (debounced analyze) --------------------------------
  let analysis = $state<AnalyzeResult | null>(null);
  let analyzeSeq = 0;
  $effect(() => {
    const text = input.trim();
    const my = ++analyzeSeq;
    if (!text) {
      analysis = null;
      return;
    }
    const timer = setTimeout(async () => {
      try {
        const r = await call("analyze", [text]);
        if (my === analyzeSeq) analysis = r;
      } catch (e) {
        if (my === analyzeSeq)
          analysis = { ok: false, error: e instanceof Error ? e.message : String(e) };
      }
    }, 250);
    return () => clearTimeout(timer);
  });

  const symbols = $derived(analysis?.ok ? analysis.symbols : []);
  const isSystem = $derived(
    (analysis?.ok && analysis.kind === "system") || hasTopLevelSemicolon(input),
  );

  // Reconcile per-tab params when the free symbols change. Bail while the
  // input is empty or transiently unparsable so mid-edit states don't wipe
  // user-entered values (e.g. evaluate bindings) — keep the last good set.
  $effect(() => {
    if (!analysis?.ok) return;
    const syms = symbols;
    untrack(() => {
      if (syms.length > 0 && !syms.includes(variable)) variable = syms[0];
      const kept = systemVars.filter((s) => syms.includes(s));
      if (
        kept.length !== systemVars.length ||
        (kept.length === 0 && syms.length > 0)
      ) {
        systemVars = kept.length > 0 ? kept : [...syms];
      }
      const nb: Record<string, number> = {};
      let changed = false;
      for (const s of syms) {
        nb[s] = bindings[s] ?? 1;
        if (!(s in bindings)) changed = true;
      }
      if (changed || Object.keys(bindings).length !== syms.length) bindings = nb;
    });
  });

  function toggleSystemVar(s: string) {
    systemVars = systemVars.includes(s)
      ? systemVars.filter((x) => x !== s)
      : [...systemVars, s];
  }

  // --- compute ---------------------------------------------------------------
  function fail(r: { error: string; begin?: number; end?: number }, text: string) {
    outcome = { kind: "error", message: r.error, begin: r.begin, end: r.end, input: text };
  }

  async function pickVariable(text: string): Promise<string> {
    if (analysis?.ok && analysis.symbols.includes(variable)) return variable;
    const a = await call("analyze", [text]);
    if (a.ok) {
      if (a.symbols.includes(variable)) return variable;
      if (a.symbols.length > 0) return a.symbols[0];
    }
    return variable || "x";
  }

  function solveSummary(v: string, r: Ok<SolveResult>): string {
    switch (r.status) {
      case "noRealSolution":
        return "no real solutions";
      case "allReals":
        return `true for all ${v}`;
      case "unsolved":
        return "could not solve";
      default:
        return r.solutions.map((s) => `${v} = ${s.plain}`).join("; ");
    }
  }

  function systemSummary(r: Ok<SystemResult>): string {
    if (r.status === "noSolution") return "no solution";
    if (r.status === "unsolved") return "could not solve";
    const parts = r.values.map((v) => `${v.symbol} = ${v.plain}`);
    return parts.join("; ") || "underdetermined";
  }

  async function compute() {
    const text = input.trim();
    if (!text || computing || !ready) return;
    const op: TabId = tab;
    computing = true;
    try {
      switch (op) {
        case "simplify":
        case "expand":
        case "factor": {
          const r = await call(op, [text]);
          if (!r.ok) {
            fail(r, text);
            break;
          }
          outcome = { kind: "transform", result: r };
          history.add({ tab: op, input: text, params: {}, summary: r.plain });
          break;
        }
        case "derivative": {
          const v = await pickVariable(text);
          const r = await call("derivative", [text, v]);
          if (!r.ok) {
            fail(r, text);
            break;
          }
          outcome = { kind: "transform", result: r };
          history.add({
            tab: op,
            input: text,
            params: { variable: v },
            summary: r.plain,
          });
          break;
        }
        case "integral": {
          const v = await pickVariable(text);
          if (definite) {
            const from = intFrom.trim() || "0";
            const to = intTo.trim() || "1";
            const r = await call("integrateDefinite", [text, v, from, to]);
            if (!r.ok) {
              fail(r, text);
              break;
            }
            outcome = { kind: "definite", from, to, result: r };
            history.add({
              tab: op,
              input: text,
              params: { variable: v, definite: true, from, to },
              summary: r.status === "unsolved" ? "no closed form" : r.plain,
            });
          } else {
            const r = await call("integrate", [text, v]);
            if (!r.ok) {
              fail(r, text);
              break;
            }
            outcome = { kind: "integral", variable: v, result: r };
            history.add({
              tab: op,
              input: text,
              params: { variable: v, definite: false },
              summary: r.solved ? `${r.plain} + C` : "no closed form",
            });
          }
          break;
        }
        case "solve": {
          if (isSystem) {
            let vars = systemVars;
            if (vars.length === 0) {
              const a = await call("analyze", [text]);
              vars = a.ok ? a.symbols : [];
            }
            const r = await call("solveSystem", [text, vars.join(",")]);
            if (!r.ok) {
              fail(r, text);
              break;
            }
            outcome = { kind: "system", result: r };
            history.add({
              tab: op,
              input: text,
              params: { vars: [...vars] },
              summary: systemSummary(r),
            });
          } else {
            const v = await pickVariable(text);
            const lo = numOr(rangeLo, -100);
            const hi = numOr(rangeHi, 100);
            const r = await call("solve", [text, v, lo, hi, useRange]);
            if (!r.ok) {
              fail(r, text);
              break;
            }
            outcome = { kind: "solve", variable: v, result: r };
            history.add({
              tab: op,
              input: text,
              params: { variable: v, useRange, lo, hi },
              summary: solveSummary(v, r),
            });
          }
          break;
        }
        case "evaluate": {
          const b = Object.entries(bindings)
            .map(([k, v]) => `${k}=${numOr(v, 0)}`)
            .join(",");
          const r = await call("evaluate", [text, b]);
          if (!r.ok) {
            fail(r, text);
            break;
          }
          outcome = { kind: "evaluate", result: r };
          history.add({
            tab: op,
            input: text,
            params: { bindings: { ...bindings } },
            summary: r.value === null ? "undefined" : fmt(r.value),
          });
          break;
        }
        case "plot": {
          const v = await pickVariable(text);
          if (v !== variable) variable = v;
          const lo = numOr(plotLo, -10);
          const hi = numOr(plotHi, 10);
          outcome = null;
          plotNonce++;
          history.add({
            tab: op,
            input: text,
            params: {
              variable: v,
              lo,
              hi,
              showDerivative: plotD,
              showAntiderivative: plotA,
            },
            summary: `plot on [${lo}, ${hi}]`,
          });
          break;
        }
      }
    } catch (e) {
      outcome = {
        kind: "error",
        message: e instanceof Error ? e.message : String(e),
        input: text,
      };
    } finally {
      computing = false;
    }
  }

  function selectTab(id: TabId) {
    if (id === tab) return;
    tab = id;
    outcome = null;
  }

  // --- history restore ---------------------------------------------------------
  function restore(e: HistoryEntry) {
    tab = e.tab;
    input = e.input;
    const p = e.params ?? {};
    if (typeof p.variable === "string") variable = p.variable;
    if (Array.isArray(p.vars))
      systemVars = p.vars.filter((x): x is string => typeof x === "string");
    if (typeof p.useRange === "boolean") useRange = p.useRange;
    if (typeof p.definite === "boolean") definite = p.definite;
    if (typeof p.from === "string") intFrom = p.from;
    if (typeof p.to === "string") intTo = p.to;
    if (typeof p.lo === "number") {
      if (e.tab === "plot") plotLo = p.lo;
      else rangeLo = p.lo;
    }
    if (typeof p.hi === "number") {
      if (e.tab === "plot") plotHi = p.hi;
      else rangeHi = p.hi;
    }
    if (p.bindings && typeof p.bindings === "object" && !Array.isArray(p.bindings)) {
      const nb: Record<string, number> = {};
      for (const [k, v] of Object.entries(p.bindings as Record<string, unknown>))
        if (typeof v === "number") nb[k] = v;
      bindings = nb;
    }
    if (typeof p.showDerivative === "boolean") plotD = p.showDerivative;
    if (typeof p.showAntiderivative === "boolean") plotA = p.showAntiderivative;
    outcome = null;
    void compute();
  }
</script>

{#snippet variableSelect()}
  <label class="ctl">
    <span>Variable</span>
    <select bind:value={variable} disabled={symbols.length === 0}>
      {#if symbols.length === 0}
        <option value={variable}>{variable || "x"}</option>
      {/if}
      {#each symbols as s (s)}
        <option value={s}>{s}</option>
      {/each}
    </select>
  </label>
{/snippet}

<div class="app">
  <header class="site-header">
    <div class="header-inner">
      <span class="wordmark">MathSolver</span>
      {#if version}
        <span class="version-chip">v{version}</span>
      {:else if !versionLoaded}
        <span class="version-chip shimmer" aria-hidden="true"></span>
      {/if}
      <span class="spacer"></span>
      <ThemeToggle />
    </div>
  </header>

  <div class="layout">
    <main class="column">
      <Tabs tabs={TABS} active={tab} onselect={selectTab} />

      <div
        id="workbench-panel"
        class="panel"
        role="tabpanel"
        aria-labelledby={"tab-" + tab}
      >
        <ExpressionInput
          bind:value={input}
          placeholder={tabDef.placeholder}
          {computing}
          computeDisabled={!ready || computing || !input.trim()}
          oncompute={compute}
        />
        {#if engineError}
          <p class="engine-error" role="alert">
            The math engine failed to load ({engineError}). Reload the page to
            try again.
          </p>
        {:else if !ready}
          <p class="loading-note">loading engine…</p>
        {/if}

        <div class="examples" role="group" aria-label="Examples">
          {#each tabDef.examples as ex (ex)}
            <button class="example-chip" onclick={() => (input = ex)}>
              {ex}
            </button>
          {/each}
        </div>

        <ParsePreview {analysis} input={input.trim()} />

        {#if tab === "solve"}
          {#if isSystem}
            <div class="ctl-row" role="group" aria-label="Variables to solve for">
              <span class="ctl-label">Solve for</span>
              {#if symbols.length === 0}
                <span class="ctl-hint">variables appear once the system parses</span>
              {/if}
              {#each symbols as s (s)}
                <button
                  class="var-chip"
                  aria-pressed={systemVars.includes(s)}
                  onclick={() => toggleSystemVar(s)}
                >
                  {s}
                </button>
              {/each}
            </div>
          {:else}
            <div class="ctl-row">
              {@render variableSelect()}
              <label class="ctl checkbox">
                <input type="checkbox" bind:checked={useRange} />
                <span>Numeric search range</span>
              </label>
              {#if useRange}
                <label class="ctl">
                  <span>Lo</span>
                  <input type="number" step="any" bind:value={rangeLo} />
                </label>
                <label class="ctl">
                  <span>Hi</span>
                  <input type="number" step="any" bind:value={rangeHi} />
                </label>
              {/if}
            </div>
          {/if}
        {:else if tab === "derivative"}
          <div class="ctl-row">
            {@render variableSelect()}
          </div>
        {:else if tab === "integral"}
          <div class="ctl-row">
            {@render variableSelect()}
            <label class="ctl checkbox">
              <input type="checkbox" bind:checked={definite} />
              <span>Definite</span>
            </label>
            {#if definite}
              <label class="ctl">
                <span>From</span>
                <input type="text" class="expr" bind:value={intFrom} placeholder="0" />
              </label>
              <label class="ctl">
                <span>To</span>
                <input type="text" class="expr" bind:value={intTo} placeholder="pi" />
              </label>
            {/if}
          </div>
        {:else if tab === "evaluate"}
          {#if symbols.length > 0}
            <div class="ctl-row" role="group" aria-label="Variable values">
              {#each symbols as s (s)}
                <label class="ctl">
                  <span>{s} =</span>
                  <input type="number" step="any" bind:value={bindings[s]} />
                </label>
              {/each}
            </div>
          {/if}
        {:else if tab === "plot"}
          <div class="ctl-row">
            {@render variableSelect()}
            <label class="ctl">
              <span>From</span>
              <input type="number" step="any" bind:value={plotLo} />
            </label>
            <label class="ctl">
              <span>To</span>
              <input type="number" step="any" bind:value={plotHi} />
            </label>
            <label class="ctl checkbox">
              <input type="checkbox" bind:checked={plotD} />
              <span>f′ overlay</span>
            </label>
            <label class="ctl checkbox">
              <input
                type="checkbox"
                bind:checked={plotA}
                disabled={!antiAvailable}
              />
              <span>Antiderivative</span>
            </label>
            {#if !antiAvailable}
              <span class="ctl-hint">no closed form</span>
            {/if}
          </div>
        {/if}

        {#if tab === "plot"}
          <Plot
            {input}
            {variable}
            lo={numOr(plotLo, -10)}
            hi={numOr(plotHi, 10)}
            showDerivative={plotD}
            showAntiderivative={plotA}
            resampleNonce={plotNonce}
            onantiavailable={(ok) => {
              antiAvailable = ok;
              if (!ok && plotA) plotA = false;
            }}
          />
        {/if}

        <ResultCard {outcome} />

        <details class="history-inline">
          <summary>History</summary>
          <div class="history-inline-body">
            <History onrestore={restore} />
          </div>
        </details>
      </div>
    </main>

    <aside class="sidebar" aria-label="Computation history">
      <History onrestore={restore} />
    </aside>
  </div>

  <footer class="site-footer">
    <p>
      All computation runs locally in your browser via WebAssembly — nothing is
      sent to a server.{#if version}&nbsp;MathSolver engine v{version}.{/if}
    </p>
  </footer>
</div>

<style>
  :global(:focus-visible) {
    outline: 2px solid var(--accent);
    outline-offset: 2px;
  }

  .app {
    min-height: 100vh;
    display: flex;
    flex-direction: column;
  }

  .site-header {
    border-bottom: 1px solid var(--border);
    background: var(--bg-panel);
  }
  .header-inner {
    max-width: 1240px;
    margin: 0 auto;
    padding: 0.65rem 1rem;
    display: flex;
    align-items: center;
    gap: 0.6rem;
  }
  .wordmark {
    font-weight: 700;
    font-size: 1.15rem;
    letter-spacing: -0.02em;
  }
  .version-chip {
    font-family: var(--font-mono);
    font-size: 0.72rem;
    color: var(--fg-muted);
    border: 1px solid var(--border);
    border-radius: 999px;
    padding: 0.05rem 0.55rem;
    background: var(--bg);
  }
  .version-chip.shimmer {
    width: 3.4em;
    height: 1.25em;
    padding: 0;
    border: none;
    background: linear-gradient(
      90deg,
      var(--border) 25%,
      var(--bg-panel) 50%,
      var(--border) 75%
    );
    background-size: 200% 100%;
    animation: shimmer 1.2s linear infinite;
  }
  @keyframes shimmer {
    to {
      background-position: -200% 0;
    }
  }
  .spacer {
    flex: 1;
  }

  .layout {
    flex: 1;
    width: 100%;
    max-width: 1240px;
    margin: 0 auto;
    padding: 1.25rem 1rem 2rem;
    display: grid;
    grid-template-columns: minmax(0, 1fr);
    gap: 1.5rem;
  }
  .column {
    width: 100%;
    max-width: 900px;
    margin: 0 auto;
    min-width: 0;
  }
  .sidebar {
    display: none;
    min-width: 0;
  }
  @media (min-width: 1100px) {
    .layout {
      grid-template-columns: minmax(0, 900px) 280px;
      justify-content: center;
    }
    .sidebar {
      display: block;
      padding-top: 0.25rem;
    }
    .history-inline {
      display: none;
    }
  }

  .panel {
    display: flex;
    flex-direction: column;
    gap: 0.85rem;
    padding-top: 1rem;
  }

  .loading-note {
    margin: -0.35rem 0 0;
    font-size: 0.82rem;
    color: var(--fg-muted);
    font-style: italic;
  }

  .engine-error {
    margin: 0;
    font-size: 0.88rem;
    color: var(--error);
    background: color-mix(in srgb, var(--error) 8%, transparent);
    border: 1px solid color-mix(in srgb, var(--error) 40%, transparent);
    border-radius: var(--radius);
    padding: 0.55rem 0.75rem;
  }

  .examples {
    display: flex;
    gap: 0.4rem;
    flex-wrap: wrap;
  }
  .example-chip {
    font-family: var(--font-mono);
    font-size: 0.8rem;
    color: var(--fg-muted);
    background: var(--bg-panel);
    border: 1px solid var(--border);
    border-radius: 999px;
    padding: 0.2rem 0.7rem;
    cursor: pointer;
  }
  .example-chip:hover {
    color: var(--accent);
    border-color: var(--accent);
  }

  .ctl-row {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    flex-wrap: wrap;
    background: var(--bg-panel);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 0.6rem 0.75rem;
  }
  .ctl {
    display: inline-flex;
    align-items: center;
    gap: 0.4rem;
    font-size: 0.85rem;
    color: var(--fg-muted);
  }
  .ctl select,
  .ctl input[type="number"],
  .ctl input[type="text"] {
    font: inherit;
    font-family: var(--font-mono);
    color: var(--fg);
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) / 2);
    padding: 0.25rem 0.45rem;
  }
  .ctl input[type="number"] {
    width: 6.5em;
  }
  .ctl input.expr {
    width: 7.5em;
  }
  .ctl.checkbox {
    cursor: pointer;
  }
  .ctl.checkbox input {
    accent-color: var(--accent);
  }
  .ctl-label {
    font-size: 0.85rem;
    color: var(--fg-muted);
  }
  .ctl-hint {
    font-size: 0.78rem;
    color: var(--fg-muted);
    font-style: italic;
  }
  .var-chip {
    font-family: var(--font-mono);
    font-size: 0.85rem;
    color: var(--fg-muted);
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: 999px;
    padding: 0.15rem 0.7rem;
    cursor: pointer;
  }
  .var-chip[aria-pressed="true"] {
    color: var(--accent-fg, #fff);
    background: var(--accent);
    border-color: var(--accent);
  }

  .history-inline {
    border: 1px solid var(--border);
    border-radius: var(--radius);
    background: var(--bg-panel);
  }
  .history-inline summary {
    cursor: pointer;
    padding: 0.55rem 0.8rem;
    font-size: 0.9rem;
    font-weight: 600;
    color: var(--fg-muted);
  }
  .history-inline-body {
    padding: 0 0.8rem 0.8rem;
  }

  .site-footer {
    border-top: 1px solid var(--border);
    background: var(--bg-panel);
  }
  .site-footer p {
    max-width: 1240px;
    margin: 0 auto;
    padding: 0.8rem 1rem;
    font-size: 0.8rem;
    color: var(--fg-muted);
  }
</style>
