<script lang="ts">
  // Clickable console command reference: built-in verbs plus the live plugin
  // catalog. Clicking an entry inserts its command into the prompt (via the
  // consoleUi channel) so new users can discover the grammar by doing.
  import { allGroups } from "../notebook/reference";
  import type { RefGroup } from "../notebook/reference";
  import { consoleUi } from "../notebook/console-ui.svelte";

  let groups = $state<RefGroup[]>([]);
  void allGroups().then((g) => (groups = g));

  function pick(insert: string) {
    consoleUi.insert(insert === ":=" ? "" : insert + " ");
  }
</script>

<section class="cmd-ref" aria-label="Console command reference">
  <h2 class="ref-title">Commands</h2>
  <p class="ref-sub">Click to insert into the prompt.</p>
  {#each groups as group (group.title)}
    <div class="ref-group">
      <h3>{group.title}</h3>
      <ul>
        {#each group.items as item (item.insert + item.usage)}
          <li>
            <button
              class="ref-item"
              onclick={() => pick(item.insert)}
              title={item.hint}
            >
              <code class="ref-usage">{item.usage}</code>
              <span class="ref-hint">{item.hint}</span>
            </button>
          </li>
        {/each}
      </ul>
    </div>
  {/each}
</section>

<style>
  .cmd-ref {
    min-width: 0;
  }
  .ref-title {
    margin: 0;
    font-size: 0.95rem;
    font-weight: 700;
    letter-spacing: -0.01em;
  }
  .ref-sub {
    margin: 0.15rem 0 0.6rem;
    font-size: 0.75rem;
    color: var(--fg-muted);
  }
  .ref-group {
    margin-bottom: 0.85rem;
  }
  .ref-group h3 {
    margin: 0 0 0.25rem;
    font-size: 0.72rem;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.06em;
    color: var(--fg-muted);
  }
  .ref-group ul {
    list-style: none;
    margin: 0;
    padding: 0;
    display: flex;
    flex-direction: column;
  }
  .ref-item {
    display: flex;
    flex-direction: column;
    align-items: flex-start;
    gap: 0.05rem;
    width: 100%;
    text-align: left;
    font: inherit;
    color: inherit;
    background: none;
    border: none;
    border-radius: calc(var(--radius) / 2);
    padding: 0.3rem 0.45rem;
    cursor: pointer;
  }
  .ref-item:hover {
    background: color-mix(in srgb, var(--accent) 8%, transparent);
  }
  .ref-usage {
    font-family: var(--font-mono);
    font-size: 0.76rem;
    color: var(--fg);
    word-break: break-word;
  }
  .ref-item:hover .ref-usage {
    color: var(--accent);
  }
  .ref-hint {
    font-size: 0.72rem;
    color: var(--fg-muted);
  }
</style>
