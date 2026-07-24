<script lang="ts">
  import type { Ok } from "../outcome";
  import type { ExplainResult } from "../engine/types";
  import Katex from "./Katex.svelte";
  import SourceFields from "./SourceFields.svelte";

  let { result, variable }: { result: Ok<ExplainResult>; variable: string } = $props();
</script>

<ol class="steps">
  {#each result.steps as step, i (i)}
    <li>
      <span class="rule">{step.rule}</span>
      <Katex latex={step.latex} />
    </li>
  {/each}
</ol>

<div class="answer">
  <span class="answer-label">d/d{variable} =</span>
  <Katex latex={result.latex} display />
</div>

<SourceFields
  fields={[
    { label: "Plain", text: result.plain },
    { label: "LaTeX", text: result.latex },
  ]}
/>

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
</style>
