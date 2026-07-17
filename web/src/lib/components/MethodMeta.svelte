<script lang="ts">
  let { method = "", warnings = [] }: { method?: string; warnings?: string[] } =
    $props();
</script>

{#if method || warnings.length}
  <div class="meta">
    {#if method}
      <span class="pill">method: {method}</span>
    {/if}
    {#if warnings.length}
      <ul class="warnings">
        {#each warnings as w (w)}
          <li>{w}</li>
        {/each}
      </ul>
    {/if}
  </div>
{/if}

<style>
  .meta {
    margin-top: 0.75rem;
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
    align-items: flex-start;
  }
  .pill {
    font-size: 0.75rem;
    color: var(--fg-muted);
    border: 1px solid var(--border);
    border-radius: 999px;
    padding: 0.1rem 0.6rem;
    background: var(--bg);
  }
  .warnings {
    margin: 0;
    padding: 0.4rem 0.75rem 0.4rem 1.75rem;
    font-size: 0.85rem;
    background: var(--warn-bg);
    color: var(--warn-fg);
    border-radius: calc(var(--radius) / 2);
    width: 100%;
  }
  .warnings li + li {
    margin-top: 0.15rem;
  }
</style>
