<script lang="ts">
  import { renderKatex } from "../katex-action";

  let { latex, display = false }: { latex: string; display?: boolean } = $props();
</script>

<span class="katex-host" class:display use:renderKatex={{ latex, display }}></span>

<style>
  .katex-host.display {
    display: block;
    overflow-x: auto;
    padding: 0.25rem 0;
  }
</style>
