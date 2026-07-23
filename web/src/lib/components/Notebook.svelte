<script lang="ts">
  import { tick } from "svelte";
  import { notebook } from "../notebook/notebook.svelte";
  import { consoleUi } from "../notebook/console-ui.svelte";
  import { completionItems, type RefItem } from "../notebook/reference";
  import {
    buildConsolePreview,
    type ConsolePreview,
  } from "../notebook/preview";
  import {
    suggestVerbs,
    suggestionCommand,
    type VerbSuggestion,
  } from "../notebook/suggest";
  import { splitAssignment } from "../vars/session";
  import { vars } from "../vars.svelte";
  import { untrack } from "svelte";

  // Panel → sliders: when the session environment changes (a Variables-panel
  // edit, an unset, a := line), cell sliders follow and their cells re-run.
  // untrack: the sync reads and writes cell state; only vars.active may
  // re-trigger it, or the slider writes would loop the effect.
  $effect(() => {
    void vars.active;
    untrack(() => notebook.syncFromVars());
  });
  import ConsoleReference from "./ConsoleReference.svelte";
  import ConsolePrompt from "./ConsolePrompt.svelte";
  import Katex from "./Katex.svelte";
  import NotebookCell from "./NotebookCell.svelte";
  import SpanHighlight from "./SpanHighlight.svelte";

  let { ready = false, engineError = "" }: { ready?: boolean; engineError?: string } =
    $props();

  let input = $state("");
  let computing = $state(false);
  let recall = $state<number | null>(null);
  let listEl: HTMLDivElement | undefined = $state();
  let exprInput: ReturnType<typeof ConsolePrompt> | undefined = $state();

  /** The cell number the prompt line will become when run. */
  const promptIndex = $derived(notebook.cells.length + 1);

  // Highest cell id already present when this console mounted; only cells run
  // afterwards (id greater than this) get the entrance animation — so history
  // restored on load, and a mode-switch remount, never re-animate.
  const animBaseline = notebook.cells.reduce((m, c) => Math.max(m, c.id), 0);

  const EXAMPLE_GROUPS = [
    {
      title: "Try it",
      items: ["2x + 3x", "(x+1)(x-2)", "factor x^2 - 5x + 6"],
    },
    {
      title: "Calculus",
      items: [
        "diff sin(x^2), x",
        "integrate x*sin(x), x",
        "plot sin(x)/x, -20, 20",
      ],
    },
    {
      title: "Tools",
      items: ["wave", "wave 240, absorbing"],
    },
    {
      title: "Solve",
      items: [
        "solve x^2 = 4, x",
        "solve x + y = 3; x - y = 1, x, y",
        "eval x^2 + y, x=3, y=0.5",
      ],
    },
    {
      title: "Variables",
      items: ["a := 2", "a*x^2 + 1", "vars"],
    },
    {
      title: "Plugins",
      items: [
        "dsp.butter lowpass, 4, 1000, 48000",
        "dsp.fir lowpass, 63, 1000, 48000",
        "plugins",
      ],
    },
  ];

  // --- completion + usage hints ---------------------------------------------
  let completions = $state<RefItem[]>([]);
  void completionItems().then((c) => (completions = c));

  /** The first token while it is still being typed (no space yet). */
  const typingToken = $derived.by(() => {
    const m = /^\s*([A-Za-z_][A-Za-z0-9_.]*)$/.exec(input);
    return m ? m[1] : null;
  });

  let dismissedFor = $state<string | null>(null);
  const suggestions = $derived.by(() => {
    if (!typingToken || input === dismissedFor) return [];
    const tok = typingToken.toLowerCase();
    return completions
      .filter((c) => c.insert.toLowerCase().startsWith(tok))
      .slice(0, 8);
  });
  let selIdx = $state(0);
  $effect(() => {
    void suggestions.length;
    selIdx = 0;
  });

  /** Usage synopsis once a known command word is complete (space typed). */
  const activeUsage = $derived.by(() => {
    const m = /^\s*([A-Za-z_][A-Za-z0-9_.]*)\s+/.exec(input);
    if (!m) return null;
    return completions.find((c) => c.insert === m[1])?.usage ?? null;
  });

  // --- ghost hint: the next argument slot(s), inline at the caret -----------
  // Shown at a word boundary (input ends with space or comma); hidden while a
  // word is being typed. Alternatives are joined with " | ".

  /** Per-synopsis argument slots of a usage string ("·" separates synopses). */
  function synopses(usage: string, insert: string): string[][] {
    return usage.split("·").map((syn) => {
      let s = syn.trim();
      if (s.toLowerCase().startsWith(insert.toLowerCase()))
        s = s.slice(insert.length);
      // Prose notes "(or just type …)" go; call shapes like y(0)=v stay.
      s = s.replace(/\s+\([^)]*\)/g, "").replace(/[[\]]/g, "");
      return s
        .split(",")
        .map((t) => t.trim())
        .filter(Boolean);
    });
  }

  const ghostHint = $derived.by(() => {
    const line = input;
    if (!line || computing) return "";
    const endsSpace = /\s$/.test(line);
    const endsComma = /,$/.test(line);
    if (!endsSpace && !endsComma) return ""; // mid-word: previewer hidden
    const m = /^\s*(\S+)\s([\s\S]*)$/.exec(line);
    if (!m) return "";
    const [, head, rest] = m;
    if (splitAssignment(line)) return "";
    const items = completions.filter(
      (c) => c.insert.toLowerCase() === head.toLowerCase(),
    );
    if (items.length === 0) return "";
    // Which argument the caret is in: top-level commas of the tail.
    let depth = 0;
    let nCommas = 0;
    let lastComma = -1;
    for (let i = 0; i < rest.length; i++) {
      const ch = rest[i];
      if (ch === "(" || ch === "[" || ch === "{") depth++;
      else if (ch === ")" || ch === "]" || ch === "}")
        depth = Math.max(0, depth - 1);
      else if (ch === "," && depth === 0) {
        nCommas++;
        lastComma = i;
      }
    }
    const starting = rest.slice(lastComma + 1).trim() === "";
    const idx = starting ? nCommas : nCommas + 1;
    const opts: string[] = [];
    for (const it of items) {
      for (const slots of synopses(it.usage, it.insert)) {
        let s = slots[idx];
        // A variadic tail ("<var> …", "y'(0)=v, …") keeps hinting.
        if (!s && slots.length > 0 && idx >= slots.length) {
          const last = slots[slots.length - 1];
          if (last.includes("…")) s = last;
        }
        if (s && !opts.includes(s)) opts.push(s);
      }
    }
    if (opts.length === 0) return "";
    const body = opts.join(" | ").replace(/\s*\|\s*/g, " | ");
    if (!starting) return ", " + body;
    return endsComma && !endsSpace ? " " + body : body;
  });

  // --- live typeset preview of the prompt (debounced engine analyze) --------
  let preview = $state<ConsolePreview | null>(null);
  let previewSeq = 0;
  $effect(() => {
    const text = input.trim();
    const popupOpen = suggestions.length > 0;
    const my = ++previewSeq;
    if (!text || popupOpen || !ready) {
      preview = null;
      return;
    }
    const timer = setTimeout(async () => {
      try {
        const p = await buildConsolePreview(text);
        if (my === previewSeq) preview = p;
      } catch {
        if (my === previewSeq) preview = null;
      }
    }, 180);
    return () => clearTimeout(timer);
  });

  // --- verb suggestions for a bare line (debounced) -------------------------
  // When the input names no command, offer the verbs worth trying on it, shown
  // as chips beside the "as parsed" preview. Empty while the completion popup
  // is open (the user is already picking a verb).
  let verbSuggestions = $state<VerbSuggestion[]>([]);
  let suggestSeq = 0;
  $effect(() => {
    const text = input.trim();
    const popupOpen = suggestions.length > 0;
    const my = ++suggestSeq;
    if (!text || popupOpen || !ready) {
      verbSuggestions = [];
      return;
    }
    const timer = setTimeout(async () => {
      try {
        const s = await suggestVerbs(text);
        if (my === suggestSeq) verbSuggestions = s;
      } catch {
        if (my === suggestSeq) verbSuggestions = [];
      }
    }, 200);
    return () => clearTimeout(timer);
  });

  // --- symbol palette (parser-supported unicode, inserted at the caret) -----
  const PALETTE: { label: string; pre: string; post: string; title: string }[] = [
    { label: "π", pre: "π", post: "", title: "pi" },
    { label: "√", pre: "√(", post: ")", title: "square root" },
    { label: "|x|", pre: "|", post: "|", title: "absolute value" },
    { label: "x²", pre: "", post: "²", title: "square" },
    { label: "×", pre: "×", post: "", title: "multiply" },
    { label: "÷", pre: "÷", post: "", title: "divide" },
    { label: "°", pre: "", post: "°", title: "degrees" },
    { label: ":=", pre: " := ", post: "", title: "assign a variable" },
  ];

  function acceptSuggestion(item: RefItem) {
    input = item.insert + " ";
    recall = null;
    void tick().then(() => exprInput?.focusEnd());
  }

  // --- prompt inserts from the reference panel ------------------------------
  $effect(() => {
    if (!consoleUi.insertRequest) return;
    const text = consoleUi.consume();
    if (text !== null) {
      input = text;
      recall = null;
      void tick().then(() => exprInput?.focusEnd());
    }
  });

  // --- run / clear ----------------------------------------------------------
  async function scrollToBottom() {
    await tick();
    if (listEl) listEl.scrollTop = listEl.scrollHeight;
  }

  async function run() {
    const text = input.trim();
    if (!text || computing || !ready) return;
    computing = true;
    input = "";
    recall = null;
    dismissedFor = null;
    void scrollToBottom();
    try {
      await notebook.run(text);
    } finally {
      computing = false;
      void scrollToBottom();
      exprInput?.focusEnd();
    }
  }

  async function runText(text: string) {
    if (computing || !ready) return;
    input = text;
    await run();
  }

  function editText(text: string) {
    input = text;
    recall = null;
    void tick().then(() => exprInput?.focusEnd());
  }

  function clearConsole() {
    notebook.clear();
    recall = null;
    exprInput?.focusEnd();
  }

  // --- keyboard: suggestions, history recall, shortcuts ---------------------
  function onkeydownextra(e: KeyboardEvent) {
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "l") {
      e.preventDefault();
      clearConsole();
      return;
    }

    if (suggestions.length > 0) {
      if (e.key === "ArrowDown") {
        e.preventDefault();
        selIdx = (selIdx + 1) % suggestions.length;
        return;
      }
      if (e.key === "ArrowUp") {
        e.preventDefault();
        selIdx = (selIdx - 1 + suggestions.length) % suggestions.length;
        return;
      }
      if (e.key === "Tab") {
        e.preventDefault();
        acceptSuggestion(suggestions[selIdx]);
        return;
      }
      if (e.key === "Escape") {
        e.preventDefault();
        dismissedFor = input;
        return;
      }
      // Enter falls through: run the line as typed.
    } else if (verbSuggestions.length > 0 && e.key === "Tab") {
      // No completion popup, but bare-line verb suggestions are showing: Tab
      // fills the first (Enter then runs it), mirroring the completion Tab.
      e.preventDefault();
      input = suggestionCommand(verbSuggestions[0], input) + " ";
      recall = null;
      void tick().then(() => exprInput?.focusEnd());
      return;
    }

    // Shell-style history recall (single-line input only).
    if (e.key !== "ArrowUp" && e.key !== "ArrowDown") {
      recall = null;
      return;
    }
    const inputs = notebook.inputs;
    if (inputs.length === 0) return;
    const ta = e.target as HTMLTextAreaElement | null;
    if (ta && ta.value.includes("\n")) return;
    e.preventDefault();
    if (e.key === "ArrowUp") {
      recall = recall === null ? inputs.length - 1 : Math.max(0, recall - 1);
      input = inputs[recall];
    } else if (recall !== null) {
      if (recall < inputs.length - 1) {
        recall += 1;
        input = inputs[recall];
      } else {
        recall = null;
        input = "";
      }
    }
  }
</script>

<div class="console">
  <div class="console-head">
    <div class="head-text">
      <span class="head-title">Console</span>
      <span class="head-hint">
        Line-by-line math, Mathematica style. Every command is listed in the
        <strong>Commands</strong> panel; the <strong>Cookbook</strong> has worked
        recipes — click any to drop it into the prompt.
      </span>
    </div>
    <button
      class="clear-btn"
      onclick={clearConsole}
      disabled={notebook.cells.length === 0}
      title="Clear the console (Ctrl+L)"
    >
      Clear
    </button>
  </div>

  <details class="ref-inline">
    <summary>Commands &amp; Cookbook</summary>
    <div class="ref-inline-body">
      <ConsoleReference />
    </div>
  </details>

  <div class="cells" bind:this={listEl} role="log" aria-label="Console output">
    {#if notebook.cells.length === 0}
      <div class="empty">
        <p class="empty-lead">
          Enter any expression to simplify it, an equation to solve it, or call
          a function directly — results appear here as In/Out cells.
        </p>
        {#each EXAMPLE_GROUPS as group (group.title)}
          <div class="example-group">
            <span class="example-title">{group.title}</span>
            <div class="examples" role="group" aria-label={group.title}>
              {#each group.items as ex (ex)}
                <button class="example-chip" onclick={() => void runText(ex)}>
                  {ex}
                </button>
              {/each}
            </div>
          </div>
        {/each}
        <p class="empty-note">
          Type <code>help</code> for the full grammar. All computation runs
          locally in your browser via WebAssembly — nothing is sent to a server.
        </p>
      </div>
    {:else}
      {#each notebook.cells as cell, i (cell.id)}
        <NotebookCell
          {cell}
          index={i + 1}
          islast={i === notebook.cells.length - 1}
          animate={cell.id > animBaseline}
          onrerun={(t) => void runText(t)}
          onedit={editText}
          onrun={(t) => void runText(t)}
        />
      {/each}
    {/if}
  </div>

  <div class="prompt-wrap">
    {#if suggestions.length > 0}
      <ul class="suggest" role="listbox" aria-label="Command suggestions">
        {#each suggestions as s, i (s.insert)}
          <li>
            <button
              class="suggest-item"
              class:selected={i === selIdx}
              role="option"
              aria-selected={i === selIdx}
              onmousedown={(e) => {
                e.preventDefault(); // keep textarea focus
                acceptSuggestion(s);
              }}
            >
              <code class="suggest-cmd">{s.insert}</code>
              <span class="suggest-hint">{s.hint}</span>
            </button>
          </li>
        {/each}
        <li class="suggest-help">Tab to accept · ↑↓ to choose · Esc to dismiss</li>
      </ul>
    {:else if preview && preview.kind === "math"}
      <div class="console-preview" data-testid="console-preview">
        <span class="preview-lead">as parsed:</span>
        <Katex latex={preview.latex} />
        {#if preview.note}
          <span class="preview-note">{preview.note}</span>
        {/if}
      </div>
    {:else if preview && preview.kind === "error"}
      <div class="console-preview has-error" data-testid="console-preview">
        <p class="preview-error">{preview.error}</p>
        {#if preview.source}
          <SpanHighlight
            input={preview.source}
            begin={preview.begin}
            end={preview.end}
          />
        {/if}
      </div>
    {:else if activeUsage}
      <p class="usage-hint"><code>{activeUsage}</code></p>
    {/if}

    {#if verbSuggestions.length > 0}
      <div
        class="verb-suggest"
        role="group"
        aria-label="Suggested commands"
        data-testid="verb-suggest"
      >
        <span class="verb-suggest-lead">try:</span>
        {#each verbSuggestions as s (s.label)}
          <button
            class="verb-chip"
            title={s.hint}
            onmousedown={(e) => e.preventDefault() /* keep focus */}
            onclick={() => void runText(suggestionCommand(s, input))}
          >
            {s.label}
          </button>
        {/each}
        <span class="verb-suggest-tab">Tab</span>
      </div>
    {/if}

    <div class="prompt" class:busy={computing}>
      <ConsolePrompt
        bind:this={exprInput}
        bind:value={input}
        index={promptIndex}
        placeholder={notebook.cells.length === 0 ? "try: solve x^2 = 4, x" : ""}
        hint={ghostHint}
        {ready}
        {computing}
        onrun={run}
        {onkeydownextra}
      />
      {#if engineError}
        <p class="engine-error" role="alert">
          The math engine failed to load ({engineError}). Reload the page to try
          again.
        </p>
      {/if}
    </div>

    <div class="prompt-foot">
      <div class="palette" role="group" aria-label="Symbol palette">
        {#each PALETTE as p (p.label)}
          <button
            class="palette-chip"
            title={p.title}
            tabindex="-1"
            onmousedown={(e) => {
              e.preventDefault(); // keep textarea focus
              exprInput?.insertAtCursor(p.pre, p.post);
            }}
          >
            {p.label}
          </button>
        {/each}
      </div>
      {#if !ready && !engineError}
        <p class="loading-note">loading engine…</p>
      {:else}
        <p class="key-hints">
          <kbd>Enter</kbd> run · <kbd>Shift+Enter</kbd> newline · <kbd>↑</kbd
          ><kbd>↓</kbd> history · <kbd>Tab</kbd> complete · <kbd>Ctrl+L</kbd> clear
        </p>
      {/if}
    </div>
  </div>
</div>

<style>
  .console {
    flex: 1;
    min-height: 0;
    display: flex;
    flex-direction: column;
    gap: 0.65rem;
    padding-top: 0.9rem;
  }
  .console-head {
    display: flex;
    align-items: flex-start;
    justify-content: space-between;
    gap: 1rem;
    flex: 0 0 auto;
  }
  .head-text {
    display: flex;
    flex-direction: column;
    gap: 0.1rem;
    min-width: 0;
  }
  .head-title {
    font-weight: 700;
    font-size: 1.05rem;
    letter-spacing: -0.01em;
  }
  .head-hint {
    font-size: 0.82rem;
    color: var(--fg-muted);
  }
  .clear-btn {
    flex: 0 0 auto;
    font: inherit;
    font-size: 0.82rem;
    color: var(--fg-muted);
    background: var(--bg-panel);
    border: 1px solid var(--border);
    border-radius: 999px;
    padding: 0.25rem 0.8rem;
    cursor: pointer;
  }
  .clear-btn:hover:not(:disabled) {
    color: var(--accent);
    border-color: var(--accent);
  }
  .clear-btn:disabled {
    opacity: 0.5;
    cursor: default;
  }

  /* Mobile-only collapsible command reference (the sidebar covers desktop). */
  .ref-inline {
    flex: 0 0 auto;
    border: 1px solid var(--border);
    border-radius: var(--radius);
    background: var(--bg-panel);
  }
  .ref-inline summary {
    cursor: pointer;
    padding: 0.5rem 0.8rem;
    font-size: 0.88rem;
    font-weight: 600;
    color: var(--fg-muted);
  }
  .ref-inline-body {
    padding: 0 0.8rem 0.8rem;
    max-height: 40vh;
    overflow-y: auto;
  }
  @media (min-width: 1100px) {
    .ref-inline {
      display: none;
    }
  }

  /* Flat, full-height cell area: no box — cells separated by hairlines. */
  .cells {
    flex: 1;
    min-height: 8rem;
    overflow-y: auto;
    padding: 0 0.25rem 0.5rem 0;
    scroll-behavior: smooth;
  }

  .empty {
    display: flex;
    flex-direction: column;
    gap: 0.9rem;
    padding: 0.5rem 0.25rem 1rem;
  }
  .empty-lead {
    margin: 0;
    font-size: 0.95rem;
    color: var(--fg);
    max-width: 60ch;
  }
  .example-group {
    display: flex;
    flex-direction: column;
    gap: 0.3rem;
  }
  .example-title {
    font-size: 0.72rem;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.06em;
    color: var(--fg-muted);
  }
  .examples {
    display: flex;
    gap: 0.4rem;
    flex-wrap: wrap;
  }
  .example-chip {
    font-family: var(--font-mono);
    font-size: 0.8rem;
    color: var(--fg-muted);
    background: var(--bg-panel);
    border: 1px solid var(--border);
    border-radius: 999px;
    padding: 0.2rem 0.7rem;
    cursor: pointer;
  }
  .example-chip:hover {
    color: var(--accent);
    border-color: var(--accent);
  }
  .empty-note {
    margin: 0.25rem 0 0;
    font-size: 0.78rem;
    color: var(--fg-muted);
  }
  .empty-note code {
    font-family: var(--font-mono);
    background: var(--bg-panel);
    border: 1px solid var(--border);
    border-radius: 4px;
    padding: 0.02em 0.35em;
  }

  /* Prompt pinned at the bottom, with the suggestion popup above it. */
  .prompt-wrap {
    position: relative;
    flex: 0 0 auto;
    border-top: 1px solid var(--border);
    padding-top: 0.35rem;
  }
  .suggest {
    position: absolute;
    bottom: calc(100% + 0.35rem);
    left: 0;
    width: min(30rem, 100%);
    list-style: none;
    margin: 0;
    padding: 0.25rem;
    background: var(--bg-panel);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    box-shadow: 0 6px 24px rgb(0 0 0 / 12%);
    z-index: 20;
  }
  .suggest-item {
    display: flex;
    align-items: baseline;
    gap: 0.6rem;
    width: 100%;
    text-align: left;
    font: inherit;
    color: inherit;
    background: none;
    border: none;
    border-radius: calc(var(--radius) / 2);
    padding: 0.3rem 0.55rem;
    cursor: pointer;
  }
  .suggest-item:hover,
  .suggest-item.selected {
    background: color-mix(in srgb, var(--accent) 10%, transparent);
  }
  .suggest-cmd {
    font-family: var(--font-mono);
    font-size: 0.85rem;
    color: var(--fg);
    flex: 0 0 auto;
  }
  .suggest-item.selected .suggest-cmd {
    color: var(--accent);
  }
  .suggest-hint {
    font-size: 0.76rem;
    color: var(--fg-muted);
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .suggest-help {
    padding: 0.25rem 0.55rem 0.15rem;
    font-size: 0.7rem;
    color: var(--fg-muted);
    border-top: 1px dashed var(--border);
    margin-top: 0.2rem;
  }
  .usage-hint {
    margin: 0 0 0.35rem;
    font-size: 0.78rem;
    color: var(--fg-muted);
  }
  .usage-hint code {
    font-family: var(--font-mono);
  }

  .console-preview {
    display: flex;
    align-items: baseline;
    gap: 0.55rem;
    flex-wrap: wrap;
    margin: 0 0 0.4rem;
    padding: 0.35rem 0.6rem;
    border-left: 3px solid color-mix(in srgb, var(--accent) 55%, transparent);
    background: color-mix(in srgb, var(--accent) 5%, transparent);
    border-radius: 0 calc(var(--radius) / 2) calc(var(--radius) / 2) 0;
    min-height: 1.6rem;
  }
  .console-preview.has-error {
    display: block;
    border-left-color: color-mix(in srgb, var(--error) 60%, transparent);
    background: color-mix(in srgb, var(--error) 5%, transparent);
  }
  .preview-lead {
    font-size: 0.74rem;
    color: var(--fg-muted);
    flex: 0 0 auto;
  }
  .preview-note {
    font-size: 0.74rem;
    color: var(--fg-muted);
    font-style: italic;
  }
  .preview-error {
    margin: 0;
    font-size: 0.82rem;
    color: var(--error);
  }

  /* Verb suggestions for a bare line: a quiet row of "try this" chips under
     the parsed preview. */
  .verb-suggest {
    display: flex;
    align-items: center;
    gap: 0.35rem;
    flex-wrap: wrap;
    margin: -0.15rem 0 0.4rem;
    padding: 0 0.6rem;
  }
  .verb-suggest-lead {
    font-size: 0.74rem;
    color: var(--fg-muted);
    flex: 0 0 auto;
  }
  .verb-chip {
    font-family: var(--font-mono);
    font-size: 0.78rem;
    color: var(--fg-muted);
    background: var(--bg-panel);
    border: 1px solid var(--border);
    border-radius: 999px;
    padding: 0.12rem 0.6rem;
    cursor: pointer;
    transition: color 120ms ease, border-color 120ms ease;
  }
  .verb-chip:hover {
    color: var(--accent);
    border-color: var(--accent);
  }
  .verb-suggest-tab {
    font-family: var(--font-mono);
    font-size: 0.66rem;
    color: var(--fg-muted);
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) / 3);
    padding: 0 0.3rem;
    opacity: 0.7;
  }

  .palette {
    display: flex;
    gap: 0.3rem;
    flex-wrap: wrap;
  }
  .palette-chip {
    font-family: var(--font-mono);
    font-size: 0.78rem;
    line-height: 1;
    color: var(--fg-muted);
    background: transparent;
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) / 2);
    padding: 0.24rem 0.45rem;
    cursor: pointer;
  }
  .palette-chip:hover {
    color: var(--accent);
    border-color: var(--accent);
    background: var(--bg-panel);
  }

  .prompt {
    display: flex;
    flex-direction: column;
    gap: 0.35rem;
    position: relative;
  }
  /* "Thinking" accent while the engine computes. The 130ms animation-delay
     means a fast result (the common case) finishes before the bar ever shows,
     so quick commands don't flicker; only a genuinely slow compute pulses. */
  .prompt.busy::before {
    content: "";
    position: absolute;
    left: -0.55rem;
    top: 0.15rem;
    bottom: 0.15rem;
    width: 2px;
    border-radius: 2px;
    background: var(--accent);
    animation: prompt-pulse 0.75s ease-in-out 130ms infinite;
  }
  @keyframes prompt-pulse {
    0%,
    100% {
      opacity: 0.2;
    }
    50% {
      opacity: 1;
    }
  }
  @media (prefers-reduced-motion: reduce) {
    .prompt.busy::before {
      animation: none;
      opacity: 0.6;
    }
  }

  /* Slim utility row under the prompt: symbol palette left, key hints right. */
  .prompt-foot {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 0.75rem;
    flex-wrap: wrap;
    margin-top: 0.45rem;
    padding: 0 0.25rem;
  }
  .key-hints {
    margin: 0;
    font-size: 0.72rem;
    color: var(--fg-muted);
    user-select: none;
    white-space: nowrap;
  }
  @media (max-width: 720px) {
    .key-hints {
      display: none;
    }
  }
  kbd {
    font-family: var(--font-mono);
    font-size: 0.68rem;
    border: 1px solid var(--border);
    border-bottom-width: 2px;
    border-radius: 4px;
    padding: 0 0.3em;
    background: var(--bg-panel);
  }
  .loading-note {
    margin: 0;
    font-size: 0.82rem;
    color: var(--fg-muted);
    font-style: italic;
  }
  .engine-error {
    margin: 0;
    font-size: 0.88rem;
    color: var(--error);
    background: color-mix(in srgb, var(--error) 8%, transparent);
    border: 1px solid color-mix(in srgb, var(--error) 40%, transparent);
    border-radius: var(--radius);
    padding: 0.55rem 0.75rem;
  }
</style>
