<script lang="ts">
  import { history, type HistoryEntry } from "../history.svelte";
  import { TABS, type TabId } from "../tabs";

  let { onrestore }: { onrestore: (e: HistoryEntry) => void } = $props();

  function tabLabel(id: TabId): string {
    return TABS.find((t) => t.id === id)?.label ?? id;
  }

  function when(ts: number): string {
    const d = new Date(ts);
    const today = new Date();
    const sameDay = d.toDateString() === today.toDateString();
    const time = d.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
    return sameDay
      ? time
      : `${d.toLocaleDateString([], { month: "short", day: "numeric" })} ${time}`;
  }
</script>

<div class="history">
  <div class="head">
    <h2>History</h2>
    {#if history.entries.length}
      <button class="clear" onclick={() => history.clear()}>Clear</button>
    {/if}
  </div>
  {#if history.entries.length === 0}
    <p class="empty">Computations you run will appear here.</p>
  {:else}
    <ul>
      {#each history.entries as e (e.id)}
        <li>
          <button
            class="entry"
            onclick={() => onrestore(e)}
            aria-label={`Restore ${tabLabel(e.tab)}: ${e.input}`}
          >
            <span class="top">
              <span class="tab-name">{tabLabel(e.tab)}</span>
              <span class="time">{when(e.ts)}</span>
            </span>
            <span class="input">{e.input}</span>
            {#if e.summary}
              <span class="summary">{e.summary}</span>
            {/if}
          </button>
        </li>
      {/each}
    </ul>
  {/if}
</div>

<style>
  .history {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
    min-width: 0;
  }
  .head {
    display: flex;
    align-items: baseline;
    justify-content: space-between;
  }
  h2 {
    margin: 0;
    font-size: 0.85rem;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    color: var(--fg-muted);
  }
  .clear {
    font: inherit;
    font-size: 0.8rem;
    color: var(--fg-muted);
    background: none;
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) / 2);
    padding: 0.1rem 0.5rem;
    cursor: pointer;
  }
  .clear:hover {
    color: var(--error);
    border-color: var(--error);
  }
  .empty {
    margin: 0;
    font-size: 0.85rem;
    color: var(--fg-muted);
  }
  ul {
    list-style: none;
    margin: 0;
    padding: 0;
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
    max-height: 70vh;
    overflow-y: auto;
    scrollbar-width: thin;
  }
  .entry {
    font: inherit;
    text-align: left;
    width: 100%;
    display: flex;
    flex-direction: column;
    gap: 0.1rem;
    background: var(--bg-panel);
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) / 2);
    padding: 0.45rem 0.6rem;
    cursor: pointer;
    color: var(--fg);
    min-width: 0;
  }
  .entry:hover {
    border-color: var(--accent);
  }
  .top {
    display: flex;
    justify-content: space-between;
    gap: 0.5rem;
    font-size: 0.72rem;
    color: var(--fg-muted);
  }
  .tab-name {
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.04em;
  }
  .input {
    font-family: var(--font-mono);
    font-size: 0.85rem;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .summary {
    font-size: 0.78rem;
    color: var(--fg-muted);
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
</style>
