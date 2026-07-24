<script lang="ts">
  // Console cookbook: worked, explained recipes grouped by topic. Each recipe
  // has a short rationale and one or more numbered console lines; clicking a
  // line drops it into the prompt (consoleUi channel). The console runs one
  // line at a time, so multi-step recipes are numbered to show the order.
  import { COOKBOOK } from "../notebook/cookbook";
  import { consoleUi } from "../notebook/console-ui.svelte";

  // First section open by default; the rest collapsed to keep the panel scannable.
  let open = $state<Record<string, boolean>>(
    Object.fromEntries(COOKBOOK.map((s, i) => [s.id, i === 0])),
  );

  function insert(code: string) {
    consoleUi.insert(code);
  }
</script>

<section class="cookbook" aria-label="Console cookbook">
  <h2 class="cb-title">Cookbook</h2>
  <p class="cb-sub">
    Worked recipes — click any line to drop it into the prompt, then press Enter.
  </p>

  {#each COOKBOOK as section (section.id)}
    <div class="cb-section">
      <button
        class="cb-section-head"
        aria-expanded={open[section.id]}
        onclick={() => (open[section.id] = !open[section.id])}
      >
        <svg
          class="chev"
          class:down={open[section.id]}
          viewBox="0 0 16 16"
          aria-hidden="true"
        >
          <path
            d="M6 4l4 4-4 4"
            fill="none"
            stroke="currentColor"
            stroke-width="1.6"
            stroke-linecap="round"
            stroke-linejoin="round"
          />
        </svg>
        <span class="cb-section-title">{section.title}</span>
        <span class="cb-count">{section.recipes.length}</span>
      </button>

      {#if open[section.id]}
        <p class="cb-blurb">{section.blurb}</p>
        <ul class="cb-recipes">
          {#each section.recipes as r, ri (ri)}
            <li class="cb-recipe">
              <h4 class="cb-recipe-title">{r.title}</h4>
              <p class="cb-why">{r.why}</p>
              <ol class="cb-steps" class:single={r.steps.length === 1}>
                {#each r.steps as step, i (i)}
                  <li>
                    <button
                      class="cb-step"
                      title="Insert into the prompt"
                      onclick={() => insert(step.code)}
                    >
                      <code>{step.code}</code>
                    </button>
                    {#if step.note}
                      <span class="cb-note">{step.note}</span>
                    {/if}
                  </li>
                {/each}
              </ol>
            </li>
          {/each}
        </ul>
      {/if}
    </div>
  {/each}
</section>

<style>
  .cookbook {
    min-width: 0;
  }
  .cb-title {
    margin: 0;
    font-size: 0.95rem;
    font-weight: 700;
    letter-spacing: -0.01em;
  }
  .cb-sub {
    margin: 0.15rem 0 0.7rem;
    font-size: 0.75rem;
    color: var(--fg-muted);
  }
  .cb-section {
    border-top: 1px solid color-mix(in srgb, var(--border) 72%, transparent);
  }
  .cb-section-head {
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
  .cb-section-title {
    font-size: 0.82rem;
    font-weight: 600;
  }
  .cb-section-head:hover .cb-section-title {
    color: var(--accent);
  }
  .cb-count {
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
  .cb-blurb {
    margin: 0 0 0.5rem;
    padding-left: 0.15rem;
    font-size: 0.74rem;
    color: var(--fg-muted);
    line-height: 1.5;
  }
  .cb-recipes {
    list-style: none;
    margin: 0 0 0.5rem;
    padding: 0;
    display: flex;
    flex-direction: column;
    gap: 0.7rem;
  }
  .cb-recipe {
    padding: 0.15rem 0.15rem 0.15rem 0.55rem;
    border-left: 2px solid var(--border);
  }
  .cb-recipe-title {
    margin: 0;
    font-size: 0.78rem;
    font-weight: 600;
    color: var(--fg);
  }
  .cb-why {
    margin: 0.2rem 0 0.4rem;
    font-size: 0.73rem;
    color: var(--fg-muted);
    line-height: 1.5;
  }
  .cb-steps {
    margin: 0;
    padding: 0;
    list-style: none;
    counter-reset: step;
    display: flex;
    flex-direction: column;
    gap: 0.3rem;
  }
  .cb-steps:not(.single) > li {
    counter-increment: step;
  }
  .cb-steps:not(.single) > li::before {
    content: counter(step);
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: 1.05rem;
    height: 1.05rem;
    margin-right: 0.4rem;
    font-family: var(--font-mono);
    font-size: 0.62rem;
    color: var(--fg-muted);
    background: var(--bg-inset, var(--bg));
    border-radius: 999px;
    flex: 0 0 auto;
  }
  .cb-steps > li {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    gap: 0.1rem 0.45rem;
    min-width: 0;
  }
  .cb-step {
    font: inherit;
    text-align: left;
    background: var(--bg-inset, var(--bg));
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) / 2);
    padding: 0.22rem 0.5rem;
    cursor: pointer;
    min-width: 0;
    max-width: 100%;
    overflow-x: auto;
    transition: border-color 120ms ease;
  }
  .cb-step code {
    font-family: var(--font-mono);
    font-size: 0.74rem;
    color: var(--fg);
    white-space: pre;
  }
  .cb-step:hover {
    border-color: var(--accent);
  }
  .cb-step:hover code {
    color: var(--accent);
  }
  .cb-note {
    font-size: 0.69rem;
    color: var(--fg-muted);
    line-height: 1.4;
  }
</style>
