<script lang="ts">
  // Reconstructs the input in a mono block with the [begin,end) span marked.
  // Engine spans are UTF-8 byte offsets; convert them to UTF-16 code-unit
  // indices so the mark stays put when the input contains non-ASCII (π, ², √).
  let { input, begin, end }: { input: string; begin?: number; end?: number } =
    $props();

  function byteToCodeUnit(s: string, byteOff: number): number {
    let bytes = 0;
    let i = 0;
    while (i < s.length && bytes < byteOff) {
      const cp = s.codePointAt(i)!;
      bytes += cp <= 0x7f ? 1 : cp <= 0x7ff ? 2 : cp <= 0xffff ? 3 : 4;
      i += cp > 0xffff ? 2 : 1;
    }
    return i;
  }

  const seg = $derived.by(() => {
    if (begin === undefined || end === undefined) return null;
    // Fast path: pure-ASCII input has identical byte and code-unit offsets.
    const ascii = !/[^\x00-\x7f]/.test(input);
    const bi = ascii ? begin : byteToCodeUnit(input, begin);
    const ei = ascii ? end : byteToCodeUnit(input, end);
    const b = Math.max(0, Math.min(bi, input.length));
    let e = Math.max(b, Math.min(ei, input.length));
    if (e === b) e = Math.min(b + 1, input.length);
    return {
      pre: input.slice(0, b),
      mid: input.slice(b, e) || " ",
      post: input.slice(e),
    };
  });
</script>

{#if seg}
  <pre class="src">{seg.pre}<mark>{seg.mid}</mark>{seg.post}</pre>
{/if}

<style>
  .src {
    font-family: var(--font-mono);
    font-size: 0.9rem;
    margin: 0.35rem 0 0;
    padding: 0.4rem 0.6rem;
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) / 2);
    white-space: pre-wrap;
    word-break: break-all;
    overflow-x: auto;
  }
  mark {
    background: color-mix(in srgb, var(--error) 16%, transparent);
    color: var(--error);
    text-decoration: underline wavy var(--error);
    text-decoration-skip-ink: none;
    text-underline-offset: 3px;
    border-radius: 2px;
  }
</style>
