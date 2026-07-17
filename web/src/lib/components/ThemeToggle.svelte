<script lang="ts">
  import { theme, type ThemeMode } from "../theme.svelte";

  const labels: Record<ThemeMode, string> = {
    system: "Auto",
    light: "Light",
    dark: "Dark",
  };
  const next: Record<ThemeMode, ThemeMode> = {
    system: "light",
    light: "dark",
    dark: "system",
  };
</script>

<button
  class="theme-toggle"
  onclick={() => theme.cycle()}
  aria-label={`Theme: ${labels[theme.mode]}. Switch to ${labels[next[theme.mode]]}.`}
  title="Cycle theme"
>
  Theme: {labels[theme.mode]}
</button>

<style>
  .theme-toggle {
    font: inherit;
    font-size: 0.85rem;
    color: var(--fg-muted);
    background: var(--bg-panel);
    border: 1px solid var(--border);
    border-radius: 999px;
    padding: 0.3rem 0.8rem;
    cursor: pointer;
  }
  .theme-toggle:hover {
    color: var(--fg);
    border-color: var(--fg-muted);
  }
</style>
