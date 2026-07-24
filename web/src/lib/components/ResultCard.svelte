<script lang="ts">
  import type { Outcome } from "../outcome";
  import ResultTransform from "./ResultTransform.svelte";
  import ResultDsolve from "./ResultDsolve.svelte";
  import VectorFieldChart from "./VectorFieldChart.svelte";
  import ResultSolve from "./ResultSolve.svelte";
  import ResultSystem from "./ResultSystem.svelte";
  import ResultIntegral from "./ResultIntegral.svelte";
  import ResultSteps from "./ResultSteps.svelte";
  import ResultDefinite from "./ResultDefinite.svelte";
  import ResultEvaluate from "./ResultEvaluate.svelte";
  import PluginResult from "./PluginResult.svelte";
  import SeriesChart from "./SeriesChart.svelte";
  import WaveField from "./WaveField.svelte";
  import SpanHighlight from "./SpanHighlight.svelte";
  import Katex from "./Katex.svelte";

  let { outcome, flat = false }: { outcome: Outcome | null; flat?: boolean } =
    $props();

  const computedFrom = $derived(
    outcome &&
      outcome.kind !== "error" &&
      outcome.kind !== "assignment" &&
      outcome.kind !== "plugin" &&
      outcome.kind !== "chart" &&
      outcome.kind !== "vecfield" &&
      outcome.kind !== "wave"
      ? (outcome.computedFrom ?? null)
      : null,
  );
</script>

<section class="result-region" aria-live="polite" aria-label="Result">
  {#if outcome}
    <div class="card" class:error-card={outcome.kind === "error"} class:flat>
      {#if outcome.kind === "transform"}
        <ResultTransform result={outcome.result} />
      {:else if outcome.kind === "dsolve"}
        <ResultDsolve result={outcome.result} />
      {:else if outcome.kind === "solve"}
        <ResultSolve variable={outcome.variable} result={outcome.result} />
      {:else if outcome.kind === "system"}
        <ResultSystem result={outcome.result} />
      {:else if outcome.kind === "integral"}
        <ResultIntegral result={outcome.result} />
      {:else if outcome.kind === "steps"}
        <ResultSteps result={outcome.result} variable={outcome.variable} />
      {:else if outcome.kind === "definite"}
        <ResultDefinite from={outcome.from} to={outcome.to} result={outcome.result} />
      {:else if outcome.kind === "evaluate"}
        <ResultEvaluate result={outcome.result} />
      {:else if outcome.kind === "plugin"}
        <PluginResult
          plugin={outcome.plugin}
          command={outcome.command}
          result={outcome.result}
        />
      {:else if outcome.kind === "chart"}
        <div class="chart-result">
          <p class="chart-title">{outcome.title}</p>
          <SeriesChart
            x={outcome.x}
            series={outcome.series}
            xlabel={outcome.xlabel}
            ylabel={outcome.ylabel}
          />
        </div>
      {:else if outcome.kind === "vecfield"}
        <VectorFieldChart fx={outcome.fx} fy={outcome.fy} result={outcome.result} />
      {:else if outcome.kind === "wave"}
        <WaveField
          compact
          columns={outcome.columns}
          speed={outcome.speed}
          damping={outcome.damping}
          boundary={outcome.boundary}
        />
      {:else if outcome.kind === "assignment"}
        <div class="assignment">
          <Katex latex={outcome.latex} display />
          <p class="assignment-note">saved to the Variables panel</p>
        </div>
      {:else if outcome.kind === "error"}
        <p class="error-msg">{outcome.message}</p>
        <SpanHighlight
          input={outcome.input}
          begin={outcome.begin}
          end={outcome.end}
        />
      {/if}
      {#if computedFrom}
        <p class="computed-from" data-testid="computed-from">
          <span class="lead">computed from:</span>
          <Katex latex={computedFrom.latex} />
        </p>
      {/if}
    </div>
  {/if}
</section>

<style>
  .result-region {
    min-height: 0;
  }
  /* The answer reads as a gently-highlighted output cell (a whisper of accent
     tint + a neutral hairline), not a heavy blockquote with a thick accent bar
     — the solid --accent is reserved for interactive elements. */
  .card {
    background: var(--accent-soft);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 0.85rem 1.1rem;
    box-shadow: var(--shadow-sm);
  }
  /* Tighten the typeset display so a short answer doesn't float in a tall,
     empty frame — the result should read as confident, not lonely. */
  .card:not(.flat) :global(.katex-display) {
    margin: 0.3em 0 0.1em;
  }
  .error-card {
    border-color: color-mix(in srgb, var(--error) 45%, var(--border));
    background: color-mix(in srgb, var(--error) 6%, var(--bg-panel));
  }
  /* Console cells render flat: no box, results sit directly on the page. */
  .card.flat {
    background: transparent;
    border: none;
    border-radius: 0;
    padding: 0;
  }
  /* In the full-bleed console a centered display block floats a small
     result in acres of empty space, far from its Out label — flat cells
     read top-to-bottom, so the math sits left in the flow, tight. KaTeX
     centers at two levels (.katex-display and its block-level .katex
     child), so both need the override. */
  .card.flat :global(.katex-display) {
    text-align: left;
    margin: 0.15em 0;
  }
  .card.flat :global(.katex-display > .katex) {
    text-align: left;
  }
  .card.flat.error-card {
    border-left: 3px solid var(--error);
    background: color-mix(in srgb, var(--error) 5%, transparent);
    border-radius: 0 calc(var(--radius) / 2) calc(var(--radius) / 2) 0;
    padding: 0.5rem 0.75rem;
  }
  .error-msg {
    margin: 0;
    color: var(--error);
    font-size: 0.95rem;
  }
  .computed-from {
    margin: 0.75rem 0 0;
    display: flex;
    align-items: baseline;
    gap: 0.5rem;
    flex-wrap: wrap;
    font-size: 0.82rem;
    color: var(--fg-muted);
    border-top: 1px dashed var(--border);
    padding-top: 0.6rem;
  }
  .computed-from .lead {
    flex: 0 0 auto;
  }
  .assignment-note {
    margin: 0.5rem 0 0;
    font-size: 0.82rem;
    color: var(--fg-muted);
  }
  .chart-result {
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
    min-width: 0;
  }
  .chart-title {
    margin: 0;
    font-family: var(--font-mono);
    font-size: 0.85rem;
    color: var(--fg-muted);
  }
</style>
