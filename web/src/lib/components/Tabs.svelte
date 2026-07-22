<script lang="ts">
  import type { TabDef, TabId } from "../tabs";

  let {
    tabs,
    active,
    onselect,
  }: { tabs: TabDef[]; active: TabId; onselect: (id: TabId) => void } = $props();

  let listEl: HTMLDivElement | undefined = $state();

  function onkeydown(e: KeyboardEvent) {
    const i = tabs.findIndex((t) => t.id === active);
    let n = -1;
    if (e.key === "ArrowRight") n = (i + 1) % tabs.length;
    else if (e.key === "ArrowLeft") n = (i - 1 + tabs.length) % tabs.length;
    else if (e.key === "Home") n = 0;
    else if (e.key === "End") n = tabs.length - 1;
    if (n < 0) return;
    e.preventDefault();
    onselect(tabs[n].id);
    (listEl?.children[n] as HTMLElement | undefined)?.focus();
  }
</script>

<div class="tabs" role="tablist" aria-label="Operation" bind:this={listEl}>
  {#each tabs as t (t.id)}
    <button
      role="tab"
      id={"tab-" + t.id}
      aria-selected={t.id === active}
      aria-controls="workbench-panel"
      tabindex={t.id === active ? 0 : -1}
      class:active={t.id === active}
      onclick={() => onselect(t.id)}
      {onkeydown}
    >
      {t.label}
    </button>
  {/each}
</div>

<style>
  .tabs {
    display: flex;
    gap: 0.15rem;
    overflow-x: auto;
    border-bottom: 1px solid var(--rule);
    padding-bottom: 0;
    scrollbar-width: thin;
  }
  button {
    font-family: var(--font-sans);
    font-size: 0.9rem;
    font-weight: 500;
    letter-spacing: 0;
    color: var(--fg-muted);
    background: none;
    border: none;
    border-bottom: 2px solid transparent;
    margin-bottom: -1px;
    padding: 0.5rem 0.8rem 0.55rem;
    cursor: pointer;
    white-space: nowrap;
    flex: 0 0 auto;
    transition: color 130ms ease, border-color 130ms ease;
  }
  button:hover {
    color: var(--fg);
  }
  button.active {
    color: var(--accent);
    border-bottom-color: var(--accent);
    font-weight: 600;
  }
</style>
