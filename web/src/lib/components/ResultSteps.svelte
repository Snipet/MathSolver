<script lang="ts">
  import type { Ok } from "../outcome";
  import type { ExplainResult } from "../engine/types";
  import Katex from "./Katex.svelte";
  import SourceFields from "./SourceFields.svelte";

  let {
    result,
    op,
    variable,
  }: { result: Ok<ExplainResult>; op: "diff" | "integrate"; variable: string } = $props();

  // An indefinite integral carries the implicit constant of integration.
  const answerPlain = $derived(result.plain + (op === "integrate" ? " + C" : ""));
  const answerLatex = $derived(result.latex + (op === "integrate" ? " + C" : ""));
</script>

{#if result.solved}
  <ol class="steps">
    {#each result.steps as step, i (i)}
      <li>
        <span class="rule">{step.rule}</span>
        <Katex latex={step.latex} />
      </li>
    {/each}
  </ol>

  <div class="answer">
    <span class="answer-label">
      {#if op === "integrate"}∫ … d{variable} ={:else}d/d{variable} ={/if}
    </span>
    <Katex latex={answerLatex} display />
  </div>

  <SourceFields
    fields={[
      { label: "Plain", text: answerPlain },
      { label: "LaTeX", text: answerLatex },
    ]}
  />
{:else}
  <p class="banner">No closed form found</p>
  <p class="hint">
    The engine could not express this antiderivative in elementary functions, so
    there are no steps to show. A definite integral can still be evaluated
    numerically.
  </p>
{/if}

<style>
  .steps {
    margin: 0 0 0.6rem;
    padding: 0;
    list-style: none;
    counter-reset: step;
  }
  .steps li {
    counter-increment: step;
    display: flex;
    flex-wrap: wrap;
    align-items: baseline;
    gap: 0.5rem;
    padding: 0.35rem 0;
    border-bottom: 1px solid var(--border, #2a2a30);
  }
  .steps li::before {
    content: counter(step);
    flex: 0 0 auto;
    min-width: 1.3rem;
    text-align: right;
    font-variant-numeric: tabular-nums;
    color: var(--muted, #7a7a85);
    font-size: 0.8rem;
  }
  .rule {
    flex: 0 0 auto;
    font-size: 0.72rem;
    text-transform: uppercase;
    letter-spacing: 0.03em;
    color: var(--accent, #6ea8fe);
    background: color-mix(in srgb, var(--accent, #6ea8fe) 12%, transparent);
    border-radius: 4px;
    padding: 0.05rem 0.4rem;
    align-self: center;
  }
  .answer {
    display: flex;
    align-items: baseline;
    gap: 0.5rem;
    flex-wrap: wrap;
  }
  .answer-label {
    font-size: 0.82rem;
    color: var(--muted, #7a7a85);
  }
  .banner {
    margin: 0;
    padding: 0.5rem 0.75rem;
    border-radius: calc(var(--radius) / 2);
    font-size: 0.95rem;
    background: var(--bg);
    border: 1px solid var(--border);
    color: var(--fg);
  }
  .hint {
    margin: 0.4rem 0 0;
    font-size: 0.85rem;
    color: var(--fg-muted);
  }
</style>
