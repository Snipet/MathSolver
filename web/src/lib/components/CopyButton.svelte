<script lang="ts">
  // Compact icon-only clipboard button for per-line copy affordances.
  let { text, label }: { text: string; label: string } = $props();

  let copied = $state(false);
  let timer: ReturnType<typeof setTimeout> | undefined;

  async function copy() {
    try {
      await navigator.clipboard.writeText(text);
    } catch {
      return;
    }
    copied = true;
    clearTimeout(timer);
    timer = setTimeout(() => (copied = false), 1400);
  }

  $effect(() => () => clearTimeout(timer));
</script>

<button class="copy-btn" class:copied onclick={copy} aria-label={label} title={label}>
  {#if copied}
    <span class="check" aria-hidden="true">✓</span>
  {:else}
    <svg viewBox="0 0 16 16" width="13" height="13" aria-hidden="true">
      <rect
        x="5.5"
        y="5.5"
        width="8"
        height="8"
        rx="1.5"
        fill="none"
        stroke="currentColor"
        stroke-width="1.5"
      />
      <path
        d="M10.5 2.5h-6a2 2 0 0 0-2 2v6"
        fill="none"
        stroke="currentColor"
        stroke-width="1.5"
        stroke-linecap="round"
      />
    </svg>
  {/if}
</button>

<style>
  .copy-btn {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: 1.7em;
    height: 1.7em;
    flex: 0 0 auto;
    color: var(--fg-muted);
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) / 2);
    padding: 0;
    cursor: pointer;
    font-size: 0.85rem;
    line-height: 1;
  }
  .copy-btn:hover {
    color: var(--fg);
    border-color: var(--accent);
  }
  .copy-btn.copied {
    color: var(--ok);
    border-color: var(--ok);
  }
  .check {
    font-size: 0.8em;
  }
</style>
