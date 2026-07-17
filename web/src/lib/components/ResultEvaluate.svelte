<script lang="ts">
  import type { Ok } from "../outcome";
  import type { EvaluateResult } from "../engine/types";
  import { fmt } from "../format";

  let { result }: { result: Ok<EvaluateResult> } = $props();
</script>

{#if result.value === null}
  <p class="undef">Undefined for the given values</p>
{:else}
  <p class="value">{fmt(result.value)}</p>
{/if}

<style>
  .value {
    margin: 0;
    font-family: var(--font-mono);
    font-size: 2rem;
    font-weight: 600;
    overflow-wrap: anywhere;
  }
  .undef {
    margin: 0;
    color: var(--fg-muted);
    font-size: 1rem;
  }
</style>
