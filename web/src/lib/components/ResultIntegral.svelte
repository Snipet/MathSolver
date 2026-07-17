<script lang="ts">
  import type { Ok } from "../outcome";
  import type { IntegrateResult } from "../engine/types";
  import Katex from "./Katex.svelte";
  import CopyField from "./CopyField.svelte";
  import MethodMeta from "./MethodMeta.svelte";

  let { result }: { result: Ok<IntegrateResult> } = $props();
</script>

{#if result.solved}
  <Katex latex={result.latex + " + C"} display />
  <div class="sources">
    <CopyField label="Plain" text={result.plain + " + C"} />
    <CopyField label="LaTeX" text={result.latex + " + C"} />
  </div>
{:else}
  <p class="banner">No closed form found</p>
  <p class="hint">
    The engine could not express this antiderivative in elementary functions.
    A definite integral can still be evaluated numerically.
  </p>
{/if}
<MethodMeta method={result.method} warnings={result.warnings} />

<style>
  .sources {
    margin-top: 0.75rem;
    display: grid;
    gap: 0.4rem;
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
