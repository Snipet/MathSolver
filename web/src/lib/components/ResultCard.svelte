<script lang="ts">
  import type { Outcome } from "../outcome";
  import ResultTransform from "./ResultTransform.svelte";
  import ResultSolve from "./ResultSolve.svelte";
  import ResultSystem from "./ResultSystem.svelte";
  import ResultIntegral from "./ResultIntegral.svelte";
  import ResultDefinite from "./ResultDefinite.svelte";
  import ResultEvaluate from "./ResultEvaluate.svelte";
  import SpanHighlight from "./SpanHighlight.svelte";

  let { outcome }: { outcome: Outcome | null } = $props();
</script>

<section class="result-region" aria-live="polite" aria-label="Result">
  {#if outcome}
    <div class="card" class:error-card={outcome.kind === "error"}>
      {#if outcome.kind === "transform"}
        <ResultTransform result={outcome.result} />
      {:else if outcome.kind === "solve"}
        <ResultSolve variable={outcome.variable} result={outcome.result} />
      {:else if outcome.kind === "system"}
        <ResultSystem result={outcome.result} />
      {:else if outcome.kind === "integral"}
        <ResultIntegral result={outcome.result} />
      {:else if outcome.kind === "definite"}
        <ResultDefinite from={outcome.from} to={outcome.to} result={outcome.result} />
      {:else if outcome.kind === "evaluate"}
        <ResultEvaluate result={outcome.result} />
      {:else if outcome.kind === "error"}
        <p class="error-msg">{outcome.message}</p>
        <SpanHighlight
          input={outcome.input}
          begin={outcome.begin}
          end={outcome.end}
        />
      {/if}
    </div>
  {/if}
</section>

<style>
  .result-region {
    min-height: 0;
  }
  .card {
    background: var(--bg-panel);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 1rem 1.1rem;
  }
  .error-card {
    border-color: color-mix(in srgb, var(--error) 50%, var(--border));
    background: color-mix(in srgb, var(--error) 6%, var(--bg-panel));
  }
  .error-msg {
    margin: 0;
    color: var(--error);
    font-size: 0.95rem;
  }
</style>
