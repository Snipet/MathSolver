<script lang="ts">
  import { tick, untrack } from "svelte";
  import { call, engineReady } from "./lib/engine";
  import type {
    AnalyzeResult,
    SolveResult,
    SystemResult,
  } from "./lib/engine/types";
  import { TABS, type TabId } from "./lib/tabs";
  import type { Ok, Outcome } from "./lib/outcome";
  import { history, type HistoryEntry } from "./lib/history.svelte";
  import { fmt, hasTopLevelSemicolon, numOr, splitTopLevel } from "./lib/format";
  import { vars } from "./lib/vars.svelte";
  import { closure } from "./lib/vars/resolve";
  import { graph } from "./lib/graph/graph.svelte";
  import { decodeState } from "./lib/graph/share";
  import {
    splitAssignment,
    buildAssignPreview,
    swapEqSegments,
    applyEnv,
    type AssignPreview,
  } from "./lib/vars/session";
  import Tabs from "./lib/components/Tabs.svelte";
  import ThemeToggle from "./lib/components/ThemeToggle.svelte";
  import ExpressionInput from "./lib/components/ExpressionInput.svelte";
  import ParsePreview from "./lib/components/ParsePreview.svelte";
  import ResultCard from "./lib/components/ResultCard.svelte";
  import WaveField from "./lib/components/WaveField.svelte";
  import GraphCalculator from "./lib/components/GraphCalculator.svelte";
  import GraphReference from "./lib/components/GraphReference.svelte";
  import History from "./lib/components/History.svelte";
  import VariablesPanel from "./lib/components/VariablesPanel.svelte";
  import VarChips, { type Chip } from "./lib/components/VarChips.svelte";
  import Katex from "./lib/components/Katex.svelte";
  import SpanHighlight from "./lib/components/SpanHighlight.svelte";
  import Notebook from "./lib/components/Notebook.svelte";
  import NotebooksPanel from "./lib/components/NotebooksPanel.svelte";
  import ConsoleReference from "./lib/components/ConsoleReference.svelte";

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

  // --- top-level mode: tabbed workbench, line-by-line console, or graphing ---
  type Mode = "workbench" | "console" | "graph";
  const MODE_KEY = "mathsolver.mode";
  function loadMode(): Mode {
    try {
      const m = localStorage.getItem(MODE_KEY);
      return m === "console" || m === "graph" ? m : "workbench";
    } catch {
      return "workbench";
    }
  }
  // A shared link (#g=…) replaces the graph document and its variables. Because
  // this overwrites the user's local work (graph + app-wide session vars) and
  // persists immediately, confirm first when there is existing work.
  function importSharedLink(): boolean {
    let matched = false;
    try {
      const m = /^#g=(.+)$/.exec(location.hash);
      if (!m) return false;
      matched = true;
      const state = decodeState(m[1]);
      if (!state) return false;
      const hasWork =
        graph.rows.length > 1 || graph.rows.some((r) => r.text.trim()) || vars.rows.length > 0;
      if (
        hasWork &&
        !window.confirm(
          "Open this shared graph? It will replace your current graph and its variables.",
        )
      ) {
        return false;
      }
      graph.replaceAll(state.rows, state.view);
      if (state.vars.length) vars.importVars(state.vars);
      return true;
    } catch {
      return false;
    } finally {
      // Always strip a #g= hash (even on decline/failure) so a reload uses the
      // local document and a malformed hash doesn't linger in the address bar.
      if (matched) {
        try {
          window.history.replaceState(null, "", location.pathname + location.search);
        } catch {
          /* ignore */
        }
      }
    }
  }
  let mode = $state<Mode>(importSharedLink() ? "graph" : loadMode());
  $effect(() => {
    try {
      localStorage.setItem(MODE_KEY, mode);
    } catch {
      /* storage unavailable */
    }
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

  // --- live parse preview (debounced analyze) --------------------------------
  let analysis = $state<AnalyzeResult | null>(null);
  let assign = $state<AssignPreview | null>(null);
  let analyzeSeq = 0;
  $effect(() => {
    const text = input.trim();
    const my = ++analyzeSeq;
    const onSolve = tab === "solve";
    void vars.active; // re-run the cycle pre-check when the environment changes
    if (!text) {
      analysis = null;
      assign = null;
      return;
    }
    const parts = splitAssignment(text);
    const timer = setTimeout(async () => {
      try {
        if (parts) {
          const a = await buildAssignPreview(parts);
          if (my === analyzeSeq) {
            assign = a;
            analysis = null;
          }
        } else {
          const target = onSolve ? swapEqSegments(text).text : text;
          const r = await call("analyze", [target]);
          if (my === analyzeSeq) {
            analysis = r;
            assign = null;
          }
        }
      } catch (e) {
        if (my === analyzeSeq) {
          assign = null;
          analysis = { ok: false, error: e instanceof Error ? e.message : String(e) };
        }
      }
    }, 250);
    return () => clearTimeout(timer);
  });

  const symbols = $derived(analysis?.ok ? analysis.symbols : []);
  const isSystem = $derived(
    (analysis?.ok && analysis.kind === "system") || hasTopLevelSemicolon(input),
  );

  // --- session environment wiring (spec §5, §7, §8, §9.1) --------------------
  const eqBound = $derived(
    new Set(vars.active.filter((b) => b.kind === "equation").map((b) => b.name)),
  );

  // Seeds for the closure: the analyzed symbols (on the Solve tab, analysis
  // already ran over the eq-segment-swapped text, so the stored equations'
  // side symbols are included). `appliedEq` is which equation bindings the
  // current input applies as whole segments (§4), for the chips.
  const seedInfo = $derived.by(() => {
    const act = vars.active;
    const appliedEq: typeof act = [];
    if (tab === "solve") {
      for (const seg of splitTopLevel(input.trim())) {
        const b = act.find((x) => x.kind === "equation" && x.name === seg);
        if (b && !appliedEq.includes(b)) appliedEq.push(b);
      }
    }
    return { seeds: [...symbols], appliedEq };
  });

  // Exclusion-free closure: which symbols remain after full resolution.
  const envBase = $derived.by(() => {
    if (!analysis?.ok) return null;
    try {
      return closure(seedInfo.seeds, vars.active, []);
    } catch {
      return null;
    }
  });

  // Variable pickers offer the post-resolution residual first (that's what
  // is still free — the environment disambiguates, §7), then the raw input
  // symbols (an assigned name stays pickable: solving/differentiating for it
  // excludes its binding with a warning chip).
  const pickerSymbols = $derived.by(() => {
    const raw = symbols.filter((s) => !eqBound.has(s));
    if (!envBase) return raw;
    return [
      ...new Set([...envBase.residual.filter((s) => !eqBound.has(s)), ...raw]),
    ];
  });

  // Evaluate binds numerically only what resolution leaves free (§7 eval row).
  const evalSymbols = $derived(
    envBase ? envBase.residual.filter((s) => !eqBound.has(s)) : [],
  );

  // §7 table: designated symbols excluded from resolution, per operation.
  const excludedNames = $derived.by((): string[] => {
    switch (tab) {
      case "solve":
        return isSystem ? [...systemVars] : [variable];
      case "derivative":
      case "integral":
        return [variable];
      default:
        return [];
    }
  });

  function ignoreReason(v: string): string {
    switch (tab) {
      case "solve":
        return `solving for ${v}`;
      case "derivative":
        return `differentiating with respect to ${v}`;
      case "integral":
        return `integrating with respect to ${v}`;
      default:
        return "not applied";
    }
  }

  // Indicator chips (§9.1): applied bindings, amber exclusions, muted
  // equation names.
  const chips = $derived.by((): Chip[] => {
    if (!analysis?.ok || assign) return [];
    const act = vars.active;
    if (act.length === 0) return [];
    let env;
    try {
      env = closure(seedInfo.seeds, act, excludedNames);
    } catch {
      return [];
    }
    const out: Chip[] = [];
    for (const b of seedInfo.appliedEq)
      out.push({ kind: "applied", label: `${b.name} := ${b.value}`, symbol: b.name });
    for (const b of env.active)
      out.push({ kind: "applied", label: `${b.name} := ${b.value}`, symbol: b.name });
    for (const n of excludedNames) {
      const b = act.find((x) => x.name === n && x.kind === "expression");
      if (b && symbols.includes(n))
        out.push({
          kind: "ignored",
          label: `${b.name} := ${b.value} ignored (${ignoreReason(n)})`,
          symbol: n,
        });
    }
    for (const n of env.directEquationRefs)
      out.push({
        kind: "muted",
        label: `${n} is an equation — not applied here`,
        symbol: n,
      });
    return out;
  });

  // Suppress the live preview error while an identical error card is shown for
  // the same input — one mistake should read as one error, not two.
  const previewAnalysis = $derived.by(() => {
    if (
      outcome?.kind === "error" &&
      analysis &&
      !analysis.ok &&
      analysis.error === outcome.message &&
      input.trim() === outcome.input
    )
      return null;
    return analysis;
  });

  // Reconcile per-tab params when the free symbols change. Bail while the
  // input is empty or transiently unparsable so mid-edit states don't wipe
  // user-entered values (e.g. evaluate bindings) — keep the last good set.
  $effect(() => {
    if (!analysis?.ok) return;
    const picks = pickerSymbols;
    const evals = evalSymbols;
    untrack(() => {
      if (picks.length > 0 && !picks.includes(variable))
        variable = defaultVariable(picks, evals);
      const kept = systemVars.filter((s) => picks.includes(s));
      if (
        kept.length !== systemVars.length ||
        (kept.length === 0 && picks.length > 0)
      ) {
        systemVars = kept.length > 0 ? kept : [...picks];
      }
      const nb: Record<string, number> = {};
      let changed = false;
      for (const s of evals) {
        nb[s] = bindings[s] ?? 1;
        if (!(s in bindings)) changed = true;
      }
      if (changed || Object.keys(bindings).length !== evals.length) bindings = nb;
    });
  });

  /**
   * Default variable when the current pick is stale: prefer a symbol that is
   * still free after resolution (the environment disambiguates, §7); among
   * those — or, when every input symbol is assigned and no choice is
   * mathematically forced — prefer the conventional `x` over the
   * alphabetical accident (differentiating `x^2 + g` with everything
   * assigned should land on x, not g).
   */
  function defaultVariable(picks: string[], residual: string[]): string {
    const pool = residual.length > 0 ? residual : picks;
    return pool.includes("x") ? "x" : pool[0];
  }

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
    if (analysis?.ok && pickerSymbols.includes(variable)) return variable;
    const a = await call("analyze", [text]);
    if (a.ok) {
      let opts = a.symbols.filter((s) => !eqBound.has(s));
      let residual: string[] = [];
      try {
        const env = closure(a.symbols, vars.active, []);
        residual = env.residual.filter((s) => !eqBound.has(s));
        opts = [...new Set([...residual, ...opts])];
      } catch {
        /* defensive-only failure; fall back to the raw symbols */
      }
      if (opts.includes(variable)) return variable;
      if (opts.length > 0) return defaultVariable(opts, residual);
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

  /** Save a `name := value` line from the main input (§2). */
  async function computeAssignment(text: string) {
    const parts = splitAssignment(text)!;
    // Re-validate fresh (the debounced preview may lag the latest keystroke).
    const st = await buildAssignPreview(parts);
    if (st.error || !st.commit) {
      outcome = { kind: "error", message: st.error ?? "invalid assignment", input: text };
      return;
    }
    const res = vars.commitAssignment(st.commit);
    if (!res.ok) {
      outcome = { kind: "error", message: res.error, input: text };
      return;
    }
    // Echo the binding in canonical plain form (§2.3) — never a computed result.
    outcome = {
      kind: "assignment",
      name: st.commit.symbol,
      plain: `${st.commit.symbol} := ${st.commit.valuePlain}`,
      latex: st.latex!,
    };
  }

  async function compute() {
    const text = input.trim();
    if (!text || computing || !ready) return;
    const op: TabId = tab;
    computing = true;
    outcome = null; // a stale result must not outlive the click that replaces it
    try {
      if (splitAssignment(text)) {
        await computeAssignment(text);
        return;
      }
      switch (op) {
        case "simplify":
        case "expand":
        case "factor": {
          const env = await applyEnv(text, [], "expr");
          const r = await call(op, [env.text]);
          if (!r.ok) {
            fail(r, env.text);
            break;
          }
          outcome = { kind: "transform", result: r, computedFrom: env.computedFrom };
          history.add({ tab: op, input: text, params: {}, summary: r.plain });
          break;
        }
        case "derivative": {
          const v = await pickVariable(text);
          const env = await applyEnv(text, [v], "expr");
          const r = await call("derivative", [env.text, v]);
          if (!r.ok) {
            fail(r, env.text);
            break;
          }
          outcome = { kind: "transform", result: r, computedFrom: env.computedFrom };
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
          const env = await applyEnv(text, [v], "expr");
          if (definite) {
            // Bounds are ordinary expressions: resolve them fully (§7).
            const from = (await applyEnv(intFrom.trim() || "0", [], "expr")).text;
            const to = (await applyEnv(intTo.trim() || "1", [], "expr")).text;
            const r = await call("integrateDefinite", [env.text, v, from, to]);
            if (!r.ok) {
              fail(r, env.text);
              break;
            }
            outcome = {
              kind: "definite",
              from,
              to,
              result: r,
              computedFrom: env.computedFrom,
            };
            history.add({
              tab: op,
              input: text,
              params: { variable: v, definite: true, from, to },
              summary: r.status === "unsolved" ? "no closed form" : r.plain,
            });
          } else {
            const r = await call("integrate", [env.text, v]);
            if (!r.ok) {
              fail(r, env.text);
              break;
            }
            outcome = {
              kind: "integral",
              variable: v,
              result: r,
              computedFrom: env.computedFrom,
            };
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
            let sv = systemVars;
            if (sv.length === 0) {
              const a = await call("analyze", [text]);
              sv = a.ok ? a.symbols.filter((s) => !eqBound.has(s)) : [];
            }
            const env = await applyEnv(text, sv, "solve");
            const r = await call("solveSystem", [env.text, sv.join(",")]);
            if (!r.ok) {
              fail(r, env.text);
              break;
            }
            outcome = { kind: "system", result: r, computedFrom: env.computedFrom };
            history.add({
              tab: op,
              input: text,
              params: { vars: [...sv] },
              summary: systemSummary(r),
            });
          } else {
            const v = await pickVariable(text);
            const env = await applyEnv(text, [v], "solve");
            const lo = numOr(rangeLo, -100);
            const hi = numOr(rangeHi, 100);
            const r = await call("solve", [env.text, v, lo, hi, useRange]);
            if (!r.ok) {
              fail(r, env.text);
              break;
            }
            outcome = {
              kind: "solve",
              variable: v,
              result: r,
              computedFrom: env.computedFrom,
            };
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
          const env = await applyEnv(text, [], "expr");
          const b = Object.entries(bindings)
            .map(([k, v]) => `${k}=${numOr(v, 0)}`)
            .join(",");
          const r = await call("evaluate", [env.text, b]);
          if (!r.ok) {
            fail(r, env.text);
            break;
          }
          outcome = { kind: "evaluate", result: r, computedFrom: env.computedFrom };
          history.add({
            tab: op,
            input: text,
            params: { bindings: { ...bindings } },
            summary: r.value === null ? "undefined" : fmt(r.value),
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

  // Enter in any secondary control (bounds, bindings, ranges) computes, just
  // like Enter in the main textarea.
  function ctlKeydown(e: KeyboardEvent) {
    if (e.key !== "Enter") return;
    e.preventDefault();
    void compute();
  }

  let exprInput: ReturnType<typeof ExpressionInput> | undefined = $state();

  // Example chips fill the input, then hand focus back to the textarea with
  // the cursor at the end so Enter/typing continues the flow.
  async function useExample(ex: string) {
    input = ex;
    await tick();
    exprInput?.focusEnd();
  }

  // --- history restore ---------------------------------------------------------
  function restore(e: HistoryEntry) {
    mode = "workbench";
    // Guard against stale persisted entries (e.g. the retired "plot" tab).
    tab = TABS.some((t) => t.id === e.tab) ? e.tab : "simplify";
    input = e.input;
    const p = e.params ?? {};
    if (typeof p.variable === "string") variable = p.variable;
    if (Array.isArray(p.vars))
      systemVars = p.vars.filter((x): x is string => typeof x === "string");
    if (typeof p.useRange === "boolean") useRange = p.useRange;
    if (typeof p.definite === "boolean") definite = p.definite;
    if (typeof p.from === "string") intFrom = p.from;
    if (typeof p.to === "string") intTo = p.to;
    if (typeof p.lo === "number") rangeLo = p.lo;
    if (typeof p.hi === "number") rangeHi = p.hi;
    if (p.bindings && typeof p.bindings === "object" && !Array.isArray(p.bindings)) {
      const nb: Record<string, number> = {};
      for (const [k, v] of Object.entries(p.bindings as Record<string, unknown>))
        if (typeof v === "number") nb[k] = v;
      bindings = nb;
    }
    outcome = null;
    void compute();
  }
</script>

{#snippet variableSelect()}
  <label class="ctl">
    <span>Variable</span>
    <select bind:value={variable} disabled={pickerSymbols.length === 0}>
      {#if pickerSymbols.length === 0}
        <option value={variable}>{variable || "x"}</option>
      {/if}
      {#each pickerSymbols as s (s)}
        <option value={s}>{s}</option>
      {/each}
    </select>
  </label>
{/snippet}

<div
  class="app"
  class:console-mode={mode === "console"}
  class:graph-mode={mode === "graph"}
>
  <header class="site-header">
    <div class="header-inner">
      <span class="wordmark">MathSolver</span>
      {#if version}
        <span class="version-chip">v{version}</span>
      {:else if !versionLoaded}
        <span class="version-chip shimmer" aria-hidden="true"></span>
      {/if}
      <span class="tagline">a computer algebra system</span>
      <span class="spacer"></span>
      <div class="mode-switch" role="tablist" aria-label="View">
        <button
          role="tab"
          aria-selected={mode === "workbench"}
          class:active={mode === "workbench"}
          onclick={() => (mode = "workbench")}
          title="Workbench — step-by-step tools"
        >
          <svg viewBox="0 0 16 16" aria-hidden="true" fill="currentColor"><rect x="1.5" y="1.5" width="5.4" height="5.4" rx="1.2" /><rect x="9.1" y="1.5" width="5.4" height="5.4" rx="1.2" /><rect x="1.5" y="9.1" width="5.4" height="5.4" rx="1.2" /><rect x="9.1" y="9.1" width="5.4" height="5.4" rx="1.2" /></svg>
          <span>Workbench</span>
        </button>
        <button
          role="tab"
          aria-selected={mode === "graph"}
          class:active={mode === "graph"}
          onclick={() => (mode = "graph")}
          title="Graph — interactive graphing calculator"
        >
          <svg viewBox="0 0 16 16" aria-hidden="true" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M2.5 2v11.5H14" /><path d="M3 11c2.5 0 3-7 5.5-7s3 5 5.5 5" /></svg>
          <span>Graph</span>
        </button>
        <button
          role="tab"
          aria-selected={mode === "console"}
          class:active={mode === "console"}
          onclick={() => (mode = "console")}
          title="Console — line-by-line REPL"
        >
          <svg viewBox="0 0 16 16" aria-hidden="true" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M3 4.5 6.5 8 3 11.5" /><path d="M8.5 11.5H13" /></svg>
          <span>Console</span>
        </button>
      </div>
      <ThemeToggle />
    </div>
  </header>

  <div class="layout">
    <main class="column">
      {#if mode === "console"}
        <Notebook {ready} {engineError} />
      {:else if mode === "graph"}
        <GraphCalculator />
      {:else}
      <div class="sheet">
      <Tabs tabs={TABS} active={tab} onselect={selectTab} />

      <div
        id="workbench-panel"
        class="panel"
        role="tabpanel"
        aria-labelledby={"tab-" + tab}
      >
        {#if tab === "wave"}
          <WaveField columns={180} speed={0.5} damping={0.08} boundary="fixed" />
        {:else}
        <ExpressionInput
          bind:this={exprInput}
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
            <button class="example-chip" onclick={() => void useExample(ex)}>
              {ex}
            </button>
          {/each}
        </div>

        {#if assign && input.trim()}
          <div
            class="assign-preview"
            class:has-error={!!assign.error}
            role="status"
            data-testid="assign-preview"
          >
            {#if assign.error}
              <p class="assign-error">{assign.error}</p>
              {#if assign.span && assign.value}
                <SpanHighlight
                  input={assign.value}
                  begin={assign.span.begin}
                  end={assign.span.end}
                />
              {/if}
            {:else if assign.latex}
              <span class="lead">assignment:</span>
              <Katex latex={assign.latex} />
              <span class="assign-hint">Compute saves it to Variables</span>
            {/if}
          </div>
        {:else}
          <ParsePreview analysis={previewAnalysis} input={input.trim()} />
        {/if}

        <VarChips {chips} />

        {#if tab === "solve"}
          {#if isSystem}
            <div class="ctl-row" role="group" aria-label="Variables to solve for">
              <span class="ctl-label">Solve for</span>
              {#if pickerSymbols.length === 0}
                <span class="ctl-hint">variables appear once the system parses</span>
              {/if}
              {#each pickerSymbols as s (s)}
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
                  <input
                    type="number"
                    step="any"
                    bind:value={rangeLo}
                    onkeydown={ctlKeydown}
                  />
                </label>
                <label class="ctl">
                  <span>Hi</span>
                  <input
                    type="number"
                    step="any"
                    bind:value={rangeHi}
                    onkeydown={ctlKeydown}
                  />
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
                <input
                  type="text"
                  class="expr"
                  bind:value={intFrom}
                  placeholder="0"
                  onkeydown={ctlKeydown}
                />
              </label>
              <label class="ctl">
                <span>To</span>
                <input
                  type="text"
                  class="expr"
                  bind:value={intTo}
                  placeholder="pi"
                  onkeydown={ctlKeydown}
                />
              </label>
            {/if}
          </div>
        {:else if tab === "evaluate"}
          {#if evalSymbols.length > 0}
            <div class="ctl-row" role="group" aria-label="Variable values">
              {#each evalSymbols as s (s)}
                <label class="ctl">
                  <span>{s} =</span>
                  <input
                    type="number"
                    step="any"
                    bind:value={bindings[s]}
                    onkeydown={ctlKeydown}
                  />
                </label>
              {/each}
            </div>
          {/if}
        {/if}

        <ResultCard {outcome} />

        <details class="history-inline">
          <summary>Variables</summary>
          <div class="history-inline-body">
            <VariablesPanel />
          </div>
        </details>

        <details class="history-inline">
          <summary>History</summary>
          <div class="history-inline-body">
            <History onrestore={restore} />
          </div>
        </details>
        {/if}
      </div>
      </div>
      {/if}
    </main>

    <aside class="sidebar" aria-label="Session variables and reference">
      <VariablesPanel />
      {#if mode === "console"}
        <NotebooksPanel />
        <ConsoleReference />
      {:else if mode === "graph"}
        <GraphReference />
      {:else}
        <History onrestore={restore} />
      {/if}
    </aside>
  </div>

  {#if mode === "workbench"}
    <footer class="site-footer">
      <p>
        All computation runs locally in your browser via WebAssembly — nothing
        is sent to a server.{#if version}&nbsp;MathSolver engine v{version}.{/if}
      </p>
    </footer>
  {/if}
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
    padding: 0.7rem 1.25rem;
    display: flex;
    align-items: center;
    gap: 0.7rem;
  }
  .wordmark {
    font-family: var(--font-sans);
    font-weight: 700;
    font-size: 1.34rem;
    letter-spacing: -0.021em;
    color: var(--fg);
  }
  .version-chip {
    font-family: var(--font-mono);
    font-size: 0.7rem;
    color: var(--fg-muted);
    border: 1px solid var(--border);
    border-radius: 999px;
    padding: 0.05rem 0.55rem;
    background: var(--bg);
  }
  .tagline {
    font-family: var(--font-sans);
    font-size: 0.86rem;
    color: var(--fg-muted);
    padding-left: 0.6rem;
    margin-left: 0.2rem;
    border-left: 1px solid var(--rule);
  }
  @media (max-width: 900px) {
    .tagline {
      display: none;
    }
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

  .mode-switch {
    display: inline-flex;
    gap: 0.15rem;
    padding: 0.2rem;
    background: var(--bg-inset);
    border: 1px solid var(--border);
    border-radius: var(--radius);
  }
  .mode-switch button {
    display: inline-flex;
    align-items: center;
    gap: 0.4rem;
    font-family: var(--font-sans);
    font-size: 0.82rem;
    font-weight: 600;
    color: var(--fg-muted);
    background: transparent;
    border: none;
    border-radius: calc(var(--radius) - 0.2rem);
    padding: 0.32rem 0.75rem;
    cursor: pointer;
    transition: color 130ms ease, background 130ms ease, box-shadow 130ms ease;
  }
  .mode-switch button svg {
    width: 15px;
    height: 15px;
    flex: 0 0 auto;
  }
  .mode-switch button:hover:not(.active) {
    color: var(--fg);
  }
  .mode-switch button.active {
    color: var(--accent);
    background: var(--bg-panel);
    box-shadow: var(--shadow-sm);
  }
  @media (max-width: 680px) {
    .mode-switch button span {
      display: none;
    }
    .mode-switch button {
      padding: 0.35rem 0.5rem;
    }
  }

  .layout {
    flex: 1;
    width: 100%;
    max-width: 1240px;
    margin: 0 auto;
    padding: 1.85rem 1.25rem 2.5rem;
    display: grid;
    grid-template-columns: minmax(0, 1fr);
    gap: 2rem;
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
      display: flex;
      flex-direction: column;
      gap: 1.5rem;
      padding-top: 0.25rem;
    }
    .history-inline {
      display: none;
    }
  }

  /* The workbench presents as a sheet of paper resting on the desk. */
  .sheet {
    background: var(--bg-panel);
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) + 4px);
    box-shadow: var(--shadow);
    padding: 0.5rem 1.6rem 1.6rem;
  }
  .panel {
    display: flex;
    flex-direction: column;
    gap: 0.85rem;
    padding-top: 1.15rem;
  }

  /* Console mode: the console owns the viewport — the page never scrolls,
     the cell area does. Wider column, sidebar scrolls independently. */
  .app.console-mode {
    height: 100dvh;
    overflow: hidden;
  }
  .app.console-mode .header-inner {
    max-width: none;
  }
  /* Full-bleed: the console owns the whole viewport width, so no dead
     margin appears to the right of the sidebar on wide screens. */
  .app.console-mode .layout {
    min-height: 0;
    max-width: none;
    padding-top: 0;
    padding-bottom: 0.9rem;
  }
  .app.console-mode .column {
    max-width: none;
    min-height: 0;
    display: flex;
    flex-direction: column;
  }
  @media (min-width: 1100px) {
    .app.console-mode .layout {
      grid-template-columns: minmax(0, 1fr) 300px;
    }
    .app.console-mode .sidebar {
      overflow-y: auto;
      min-height: 0;
      padding-top: 0.9rem;
      scrollbar-width: thin;
    }
  }

  /* Graph mode: like the console, the graphing calculator owns the whole
     viewport so there's maximum room to work with expressions and sliders. */
  .app.graph-mode {
    height: 100dvh;
    overflow: hidden;
  }
  .app.graph-mode .header-inner {
    max-width: none;
  }
  .app.graph-mode .layout {
    min-height: 0;
    max-width: none;
    padding-top: 0.6rem;
    padding-bottom: 0.6rem;
  }
  .app.graph-mode .column {
    max-width: none;
    min-height: 0;
    display: flex;
    flex-direction: column;
  }
  .app.graph-mode :global(.calc) {
    flex: 1;
    min-height: 0;
  }
  @media (min-width: 1100px) {
    .app.graph-mode .layout {
      grid-template-columns: minmax(0, 1fr) 260px;
    }
    .app.graph-mode .sidebar {
      overflow-y: auto;
      min-height: 0;
      padding-top: 0.6rem;
      scrollbar-width: thin;
    }
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

  .assign-preview {
    font-size: 0.9rem;
    color: var(--fg-muted);
    min-height: 1.5rem;
    display: flex;
    align-items: baseline;
    gap: 0.5rem;
    flex-wrap: wrap;
  }
  .assign-preview.has-error {
    display: block;
  }
  .assign-preview .lead {
    flex: 0 0 auto;
  }
  .assign-error {
    margin: 0;
    color: var(--error);
  }
  .assign-hint {
    font-size: 0.78rem;
    font-style: italic;
  }
  .example-chip {
    font-family: var(--font-mono);
    font-size: 0.8rem;
    color: var(--fg-muted);
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: 999px;
    padding: 0.22rem 0.72rem;
    cursor: pointer;
    transition: color 130ms ease, border-color 130ms ease;
  }
  .example-chip:hover {
    color: var(--accent);
    border-color: var(--accent-line);
  }

  .ctl-row {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    flex-wrap: wrap;
    background: var(--bg-inset);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 0.6rem 0.8rem;
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
    background: var(--bg-panel);
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) / 2);
    padding: 0.25rem 0.45rem;
  }
  .ctl select:focus,
  .ctl input:focus {
    outline: none;
    border-color: var(--accent);
    box-shadow: 0 0 0 2px var(--accent-soft);
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
    background: var(--bg-panel);
    border: 1px solid var(--border);
    border-radius: 999px;
    padding: 0.15rem 0.7rem;
    cursor: pointer;
    transition: color 130ms ease, border-color 130ms ease;
  }
  .var-chip[aria-pressed="true"] {
    color: var(--accent-fg, #fff);
    background: var(--accent);
    border-color: var(--accent);
  }

  .history-inline {
    border: 1px solid var(--border);
    border-radius: var(--radius);
    background: var(--bg-inset);
  }
  .history-inline summary {
    cursor: pointer;
    padding: 0.55rem 0.8rem;
    font-family: var(--font-serif);
    font-size: 1rem;
    font-weight: 600;
    color: var(--fg);
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
