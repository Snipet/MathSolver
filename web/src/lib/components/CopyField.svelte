<script lang="ts">
  let { label, text }: { label: string; text: string } = $props();

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

<div class="copy-field">
  <span class="label">{label}</span>
  <code class="text">{text}</code>
  <button class:copied onclick={copy} aria-label={`Copy ${label} source`}>
    {copied ? "✓ Copied" : "Copy"}
  </button>
</div>

<style>
  .copy-field {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    min-width: 0;
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) / 2);
    background: var(--bg);
    padding: 0.3rem 0.3rem 0.3rem 0.6rem;
    font-size: 0.85rem;
  }
  .label {
    color: var(--fg-muted);
    flex: 0 0 auto;
  }
  .text {
    font-family: var(--font-mono);
    flex: 1;
    min-width: 0;
    overflow-x: auto;
    white-space: nowrap;
    scrollbar-width: thin;
  }
  button {
    font: inherit;
    font-size: 0.8rem;
    flex: 0 0 auto;
    min-width: 4.6em;
    color: var(--fg-muted);
    background: var(--bg-panel);
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) / 2);
    padding: 0.2rem 0.5rem;
    cursor: pointer;
  }
  button:hover {
    color: var(--fg);
  }
  button.copied {
    color: var(--ok);
    border-color: var(--ok);
  }
</style>
