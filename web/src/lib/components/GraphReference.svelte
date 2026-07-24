<script lang="ts">
  // Grapher sidebar quick-reference: grouped, collapsible cheat-sheet of what
  // you can type in the expression list. Clicking a code chip drops it in as a
  // new row. Replaces the old wall-of-text tip. Data lives in graph/reference.ts.
  import { GRAPH_REFERENCE } from "../graph/reference";
  import { graph } from "../graph/graph.svelte";

  // First group open, the rest collapsed to keep the panel scannable.
  let open = $state<Record<string, boolean>>(
    Object.fromEntries(GRAPH_REFERENCE.map((g, i) => [g.title, i === 0])),
  );

  function insert(code: string): void {
    graph.addRow(code);
  }
</script>

<section class="graph-ref" aria-label="Grapher quick reference">
  <p class="gr-sub">
    Type functions, points, or relations on the left. Click an example to add it
    as a row.
  </p>

  {#each GRAPH_REFERENCE as group (group.title)}
    <div class="gr-group">
      <button
        class="gr-head"
        aria-expanded={open[group.title]}
        onclick={() => (open[group.title] = !open[group.title])}
      >
        <svg class="chev" class:down={open[group.title]} viewBox="0 0 16 16" aria-hidden="true">
          <path d="M6 4l4 4-4 4" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" />
        </svg>
        <span class="gr-title">{group.title}</span>
        <span class="gr-count">{group.entries.length}</span>
      </button>

      {#if open[group.title]}
        <ul class="gr-entries">
          {#each group.entries as entry, i (i)}
            <li class="gr-entry">
              {#if entry.code}
                <button class="gr-chip" title="Add as a new row" onclick={() => insert(entry.code!)}>
                  <code>{entry.code}</code>
                </button>
              {/if}
              <span class="gr-desc">{entry.desc}</span>
            </li>
          {/each}
        </ul>
      {/if}
    </div>
  {/each}
</section>

<style>
  .graph-ref {
    min-width: 0;
  }
  .gr-sub {
    margin: 0 0 0.6rem;
    font-size: 0.75rem;
    color: var(--fg-muted);
    line-height: 1.5;
  }
  .gr-group {
    border-top: 1px solid color-mix(in srgb, var(--border) 72%, transparent);
  }
  .gr-head {
    display: flex;
    align-items: center;
    gap: 0.4rem;
    width: 100%;
    text-align: left;
    font: inherit;
    color: var(--fg);
    background: none;
    border: none;
    padding: 0.5rem 0.15rem;
    cursor: pointer;
  }
  .gr-title {
    font-size: 0.82rem;
    font-weight: 600;
  }
  .gr-head:hover .gr-title {
    color: var(--accent);
  }
  .gr-count {
    margin-left: auto;
    font-family: var(--font-mono);
    font-size: 0.68rem;
    color: var(--fg-muted);
    background: var(--bg-inset, var(--bg));
    border-radius: 999px;
    padding: 0.05rem 0.45rem;
  }
  .chev {
    width: 13px;
    height: 13px;
    flex: 0 0 auto;
    color: var(--fg-muted);
    transition: transform 120ms ease;
  }
  .chev.down {
    transform: rotate(90deg);
  }
  .gr-entries {
    list-style: none;
    margin: 0 0 0.5rem;
    padding: 0 0 0 0.15rem;
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
  }
  .gr-entry {
    display: flex;
    align-items: baseline;
    flex-wrap: wrap;
    gap: 0.1rem 0.5rem;
    min-width: 0;
  }
  .gr-chip {
    font: inherit;
    text-align: left;
    background: var(--bg-inset, var(--bg));
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) / 2);
    padding: 0.16rem 0.45rem;
    cursor: pointer;
    max-width: 100%;
    overflow-x: auto;
    transition: border-color 120ms ease;
  }
  .gr-chip code {
    font-family: var(--font-mono);
    font-size: 0.74rem;
    color: var(--fg);
    white-space: pre;
  }
  .gr-chip:hover {
    border-color: var(--accent);
  }
  .gr-chip:hover code {
    color: var(--accent);
  }
  .gr-desc {
    font-size: 0.73rem;
    color: var(--fg-muted);
    line-height: 1.45;
  }
</style>
