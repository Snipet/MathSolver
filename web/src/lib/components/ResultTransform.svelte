<script lang="ts">
  import type { Ok } from "../outcome";
  import type { TransformResult } from "../engine/types";
  import Katex from "./Katex.svelte";
  import CopyField from "./CopyField.svelte";

  let { result }: { result: Ok<TransformResult> } = $props();
</script>

<Katex latex={result.latex} display />
<div class="sources">
  <CopyField label="Plain" text={result.plain} />
  <CopyField label="LaTeX" text={result.latex} />
</div>

<style>
  .sources {
    margin-top: 0.75rem;
    display: grid;
    gap: 0.4rem;
  }
</style>
