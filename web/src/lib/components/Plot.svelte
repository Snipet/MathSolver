<script lang="ts">
  import { call } from "../engine";

  interface Props {
    input: string;
    variable: string;
    lo: number;
    hi: number;
    showDerivative: boolean;
    showAntiderivative: boolean;
    /** Bump to force a re-sample (e.g. explicit Compute click). */
    resampleNonce?: number;
    /** Reports whether a closed-form antiderivative exists for the input. */
    onantiavailable?: (ok: boolean) => void;
  }

  let {
    input,
    variable,
    lo,
    hi,
    showDerivative,
    showAntiderivative,
    resampleNonce = 0,
    onantiavailable,
  }: Props = $props();

  const N = 400;
  const HEIGHT = 380;
  const ORANGE = "#f59e0b";

  let fYs = $state<(number | null)[] | null>(null);
  let dYs = $state<(number | null)[] | null>(null);
  let aYs = $state<(number | null)[] | null>(null);
  let sampleError = $state("");
  let sampling = $state(false);
  let sampleToken = 0;

  const rangeValid = $derived(
    Number.isFinite(lo) && Number.isFinite(hi) && hi > lo,
  );

  // Debounced re-sample on any relevant change.
  $effect(() => {
    const expr = input.trim();
    const v = variable || "x";
    const l = lo;
    const h = hi;
    const wantD = showDerivative;
    const wantA = showAntiderivative;
    void resampleNonce;
    const my = ++sampleToken;
    if (!expr || !rangeValid) {
      fYs = dYs = aYs = null;
      sampleError = "";
      sampling = false;
      return;
    }
    sampling = true;
    const timer = setTimeout(async () => {
      try {
        const f = await call("sample", [expr, v, l, h, N]);
        if (my !== sampleToken) return;
        if (!f.ok) {
          fYs = dYs = aYs = null;
          sampleError = f.error;
          sampling = false;
          return;
        }

        let d: (number | null)[] | null = null;
        if (wantD) {
          const dr = await call("derivative", [expr, v]);
          if (dr.ok) {
            const ds = await call("sample", [dr.plain, v, l, h, N]);
            if (ds.ok) d = ds.ys;
          }
        }

        // Always probe integrability so the overlay checkbox state is honest.
        let a: (number | null)[] | null = null;
        const ir = await call("integrate", [expr, v]);
        if (my !== sampleToken) return;
        const antiOk = ir.ok && ir.solved === true;
        onantiavailable?.(antiOk);
        if (wantA && ir.ok && ir.solved) {
          const as = await call("sample", [ir.plain, v, l, h, N]);
          if (as.ok) a = as.ys;
        }

        if (my !== sampleToken) return;
        fYs = f.ys;
        dYs = d;
        aYs = a;
        sampleError = "";
        sampling = false;
      } catch (e) {
        if (my !== sampleToken) return;
        fYs = dYs = aYs = null;
        sampleError = e instanceof Error ? e.message : String(e);
        sampling = false;
      }
    }, 300);
    return () => clearTimeout(timer);
  });

  const allNull = $derived(
    fYs !== null && fYs.every((y) => y === null || !Number.isFinite(y)),
  );

  // --- drawing -------------------------------------------------------------

  let host: HTMLDivElement | undefined = $state();
  let canvas: HTMLCanvasElement | undefined = $state();
  let width = $state(0);
  let themeTick = $state(0);

  $effect(() => {
    const el = host;
    if (!el) return;
    const ro = new ResizeObserver((entries) => {
      width = Math.max(0, Math.floor(entries[0].contentRect.width));
    });
    ro.observe(el);
    return () => ro.disconnect();
  });

  // Redraw when the theme changes: data-theme flips or system scheme changes.
  $effect(() => {
    const bump = () => {
      themeTick += 1;
    };
    const mo = new MutationObserver(bump);
    mo.observe(document.documentElement, {
      attributes: true,
      attributeFilter: ["data-theme"],
    });
    const mq = window.matchMedia("(prefers-color-scheme: dark)");
    mq.addEventListener("change", bump);
    return () => {
      mo.disconnect();
      mq.removeEventListener("change", bump);
    };
  });

  $effect(() => {
    void themeTick;
    const c = canvas;
    const w = width;
    const f = fYs;
    const d = dYs;
    const a = aYs;
    const l = lo;
    const h = hi;
    if (!c || w < 60) return;
    draw(c, w, l, h, f, d, a);
  });

  function cssColor(el: Element, name: string, fallback: string): string {
    const v = getComputedStyle(el).getPropertyValue(name).trim();
    return v || fallback;
  }

  function quantile(sorted: number[], q: number): number {
    if (sorted.length === 0) return 0;
    const pos = (sorted.length - 1) * q;
    const base = Math.floor(pos);
    const rest = pos - base;
    const next = sorted[base + 1];
    return next !== undefined
      ? sorted[base] + rest * (next - sorted[base])
      : sorted[base];
  }

  /**
   * Interior local extrema (strict direction changes) of a sampled series.
   * Poles/asymptotes produce huge "extrema" at the jump, which harmlessly
   * disqualifies the detail zoom below.
   */
  function collectExtrema(ys: (number | null)[], out: number[]) {
    for (let i = 1; i < ys.length - 1; i++) {
      const a = ys[i - 1];
      const b = ys[i];
      const c = ys[i + 1];
      if (a === null || b === null || c === null) continue;
      if (!Number.isFinite(a) || !Number.isFinite(b) || !Number.isFinite(c))
        continue;
      const d1 = b - a;
      const d2 = c - b;
      if ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) out.push(b);
    }
  }

  /**
   * Fit a y-range to the finite values.
   *
   * Outliers are clipped only when the spread is genuinely dominated by them:
   * the clip bound is max(4·IQR beyond the quartiles, the 1st/99th
   * percentiles), so at least 98% of finite samples always stay in range
   * (keeps 1/x readable without ever cutting into the bulk of the data).
   *
   * Separately, smooth functions with huge tails (e.g. x^3 - 3x) squash their
   * local max/min into a flat line even though nothing is an outlier. When all
   * interior local extrema sit in a tiny slice (<5%) of the fitted range, zoom
   * so the extrema band occupies about a quarter of the plot height — the
   * shape stays visible while the tails exit steeply.
   */
  function fitY(vals: number[], extrema: number[] = []): [number, number] {
    if (vals.length === 0) return [-1, 1];
    const s = [...vals].sort((x, y) => x - y);
    let min = s[0];
    let max = s[s.length - 1];
    const q1 = quantile(s, 0.25);
    const q3 = quantile(s, 0.75);
    const iqr = q3 - q1;
    if (iqr > 1e-9) {
      min = Math.max(min, Math.min(q1 - 4 * iqr, quantile(s, 0.01)));
      max = Math.min(max, Math.max(q3 + 4 * iqr, quantile(s, 0.99)));
    }
    min = Math.max(min, -1e6);
    max = Math.min(max, 1e6);
    if (extrema.length >= 2) {
      const eLo = Math.min(...extrema);
      const eHi = Math.max(...extrema);
      const eSpan = eHi - eLo;
      if (eSpan > 1e-12 && eSpan < 0.05 * (max - min) && eHi > min && eLo < max) {
        const mid = (eLo + eHi) / 2;
        const half = 2 * eSpan;
        const zLo = Math.max(min, mid - half);
        const zHi = Math.min(max, mid + half);
        if (zHi - zLo > 1e-12) {
          min = zLo;
          max = zHi;
        }
      }
    }
    if (!(max > min)) {
      min -= 1;
      max += 1;
    }
    const pad = (max - min) * 0.08;
    return [min - pad, max + pad];
  }

  function niceStep(range: number, targetTicks: number): number {
    const raw = range / Math.max(1, targetTicks);
    const mag = 10 ** Math.floor(Math.log10(raw));
    const n = raw / mag;
    const mult = n < 1.5 ? 1 : n < 3.5 ? 2 : n < 7.5 ? 5 : 10;
    return mult * mag;
  }

  function tickLabel(v: number, step: number): string {
    const decimals = Math.max(0, -Math.floor(Math.log10(step)) + (step < 1 ? 1 : 0));
    const s = v.toFixed(Math.min(decimals, 6));
    return String(parseFloat(s));
  }

  function draw(
    c: HTMLCanvasElement,
    w: number,
    l: number,
    h: number,
    f: (number | null)[] | null,
    d: (number | null)[] | null,
    a: (number | null)[] | null,
  ) {
    const dpr = window.devicePixelRatio || 1;
    c.width = Math.round(w * dpr);
    c.height = Math.round(HEIGHT * dpr);
    c.style.width = w + "px";
    c.style.height = HEIGHT + "px";
    const ctx = c.getContext("2d");
    if (!ctx) return;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, w, HEIGHT);

    const colBorder = cssColor(c, "--border", "#ccc");
    const colMuted = cssColor(c, "--fg-muted", "#888");
    const colAccent = cssColor(c, "--accent", "#2563eb");
    const colOk = cssColor(c, "--ok", "#16a34a");
    const colPanel = cssColor(c, "--bg-panel", "#fff");

    ctx.fillStyle = colPanel;
    ctx.fillRect(0, 0, w, HEIGHT);

    const m = { left: 52, right: 14, top: 14, bottom: 30 };
    const pw = w - m.left - m.right;
    const ph = HEIGHT - m.top - m.bottom;
    if (pw <= 0 || ph <= 0) return;

    const series: { ys: (number | null)[]; color: string; lw: number }[] = [];
    if (f) series.push({ ys: f, color: colAccent, lw: 2 });
    if (d) series.push({ ys: d, color: ORANGE, lw: 1.5 });
    if (a) series.push({ ys: a, color: colOk, lw: 1.5 });

    const finite: number[] = [];
    const extrema: number[] = [];
    for (const s of series) {
      for (const y of s.ys)
        if (y !== null && Number.isFinite(y)) finite.push(y);
      collectExtrema(s.ys, extrema);
    }
    const [yLo, yHi] = fitY(finite, extrema);

    const xOf = (x: number) => m.left + ((x - l) / (h - l)) * pw;
    const yOf = (y: number) => m.top + ((yHi - y) / (yHi - yLo)) * ph;

    ctx.font =
      "11px " +
      (getComputedStyle(c).getPropertyValue("--font-sans").trim() ||
        "sans-serif");

    // Gridlines + ticks.
    const xStep = niceStep(h - l, 7);
    const yStep = niceStep(yHi - yLo, 6);
    ctx.lineWidth = 1;

    ctx.strokeStyle = colBorder;
    ctx.fillStyle = colMuted;
    ctx.textAlign = "center";
    ctx.textBaseline = "top";
    for (let x = Math.ceil(l / xStep) * xStep; x <= h + 1e-9; x += xStep) {
      const px = Math.round(xOf(x)) + 0.5;
      ctx.globalAlpha = 0.55;
      ctx.beginPath();
      ctx.moveTo(px, m.top);
      ctx.lineTo(px, m.top + ph);
      ctx.stroke();
      ctx.globalAlpha = 1;
      ctx.fillText(tickLabel(x, xStep), px, m.top + ph + 6);
    }
    ctx.textAlign = "right";
    ctx.textBaseline = "middle";
    for (let y = Math.ceil(yLo / yStep) * yStep; y <= yHi + 1e-9; y += yStep) {
      const py = Math.round(yOf(y)) + 0.5;
      ctx.globalAlpha = 0.55;
      ctx.beginPath();
      ctx.moveTo(m.left, py);
      ctx.lineTo(m.left + pw, py);
      ctx.stroke();
      ctx.globalAlpha = 1;
      ctx.fillText(tickLabel(y, yStep), m.left - 7, py);
    }

    // Zero axes, slightly stronger.
    ctx.strokeStyle = colMuted;
    ctx.globalAlpha = 0.8;
    if (yLo < 0 && yHi > 0) {
      const py = Math.round(yOf(0)) + 0.5;
      ctx.beginPath();
      ctx.moveTo(m.left, py);
      ctx.lineTo(m.left + pw, py);
      ctx.stroke();
    }
    if (l < 0 && h > 0) {
      const px = Math.round(xOf(0)) + 0.5;
      ctx.beginPath();
      ctx.moveTo(px, m.top);
      ctx.lineTo(px, m.top + ph);
      ctx.stroke();
    }
    ctx.globalAlpha = 1;

    // Series, clipped to the plot rect; nulls break the polyline.
    ctx.save();
    ctx.beginPath();
    ctx.rect(m.left, m.top, pw, ph);
    ctx.clip();
    for (const s of series) {
      ctx.strokeStyle = s.color;
      ctx.lineWidth = s.lw;
      ctx.lineJoin = "round";
      ctx.beginPath();
      let pen = false;
      const n = s.ys.length;
      for (let i = 0; i < n; i++) {
        const y = s.ys[i];
        if (y === null || !Number.isFinite(y)) {
          pen = false;
          continue;
        }
        const x = l + ((h - l) * i) / (n - 1);
        const px = xOf(x);
        const py = yOf(y);
        if (pen) ctx.lineTo(px, py);
        else {
          ctx.moveTo(px, py);
          pen = true;
        }
      }
      ctx.stroke();
    }
    ctx.restore();

    // Plot frame.
    ctx.strokeStyle = colBorder;
    ctx.lineWidth = 1;
    ctx.strokeRect(m.left + 0.5, m.top + 0.5, pw - 1, ph - 1);
  }
</script>

<div class="plot" bind:this={host}>
  <div class="legend" aria-hidden={fYs === null}>
    {#if fYs}
      <span class="item"><span class="swatch f"></span>f</span>
      {#if dYs}
        <span class="item"><span class="swatch d"></span>f′</span>
      {/if}
      {#if aYs}
        <span class="item"><span class="swatch a"></span>antiderivative</span>
      {/if}
      {#if sampling}
        <span class="status">sampling…</span>
      {/if}
    {/if}
  </div>
  <div class="canvas-wrap">
    <canvas bind:this={canvas} aria-label="Function plot"></canvas>
    {#if sampleError}
      <p class="overlay error">{sampleError}</p>
    {:else if allNull}
      <p class="overlay">No plottable values in this range.</p>
    {:else if fYs === null && !sampling}
      <p class="overlay">
        {rangeValid
          ? "Enter a function to plot."
          : "Enter a valid range (from < to)."}
      </p>
    {/if}
  </div>
</div>

<style>
  .plot {
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
    min-width: 0;
  }
  .legend {
    display: flex;
    gap: 1rem;
    align-items: center;
    font-size: 0.8rem;
    color: var(--fg-muted);
    min-height: 1.2rem;
    flex-wrap: wrap;
  }
  .item {
    display: inline-flex;
    align-items: center;
    gap: 0.35rem;
  }
  .swatch {
    width: 14px;
    height: 3px;
    border-radius: 2px;
    display: inline-block;
  }
  .swatch.f {
    background: var(--accent);
  }
  .swatch.d {
    background: #f59e0b;
  }
  .swatch.a {
    background: var(--ok);
  }
  .status {
    font-style: italic;
  }
  .canvas-wrap {
    position: relative;
    border: 1px solid var(--border);
    border-radius: var(--radius);
    overflow: hidden;
    background: var(--bg-panel);
  }
  canvas {
    display: block;
    max-width: 100%;
  }
  .overlay {
    position: absolute;
    inset: 0;
    display: flex;
    align-items: center;
    justify-content: center;
    margin: 0;
    padding: 1rem;
    text-align: center;
    color: var(--fg-muted);
    font-size: 0.9rem;
    background: color-mix(in srgb, var(--bg-panel) 75%, transparent);
    pointer-events: none;
  }
  .overlay.error {
    color: var(--error);
  }
</style>
