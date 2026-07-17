<script lang="ts">
  import type { AnalyzeResult } from "../engine/types";
  import { countTopLevelParts } from "../format";
  import Katex from "./Katex.svelte";
  import SpanHighlight from "./SpanHighlight.svelte";

  let { analysis, input }: { analysis: AnalyzeResult | null; input: string } =
    $props();
</script>

{#if analysis && input}
  {#if analysis.ok}
    <div class="preview ok">
      {#if analysis.kind === "system"}
        <span class="lead">as parsed:</span>
        system of {countTopLevelParts(input)} equations in {analysis.symbols.join(", ")}
      {:else}
        <span class="lead">as parsed:</span>
        <Katex latex={analysis.latex} />
      {/if}
    </div>
  {:else}
    <div class="preview error" role="status">
      <p class="msg">{analysis.error}</p>
      <SpanHighlight {input} begin={analysis.begin} end={analysis.end} />
    </div>
  {/if}
{/if}

<style>
  .preview {
    font-size: 0.9rem;
    color: var(--fg-muted);
    min-height: 1.5rem;
  }
  .preview.ok {
    display: flex;
    align-items: baseline;
    gap: 0.5rem;
    flex-wrap: wrap;
    overflow-x: auto;
  }
  .lead {
    flex: 0 0 auto;
  }
  .preview.error .msg {
    margin: 0;
    color: var(--error);
  }
</style>
