<script lang="ts">
  // Generic data-series line chart for plugin "series" blocks (e.g. a filter's
  // magnitude response). Unlike Plot.svelte, no engine calls: the data arrives
  // fully sampled. Canvas rendering, theme-aware, optional log-x axis; null
  // samples break the line.

  interface Series {
    label: string;
    ys: (number | null)[];
  }

  interface Props {
    x: number[];
    series: Series[];
    xlabel?: string;
    ylabel?: string;
    logx?: boolean;
  }

  let { x, series, xlabel = "", ylabel = "", logx = false }: Props = $props();

  const HEIGHT = 260;
  const PAD = { l: 52, r: 12, t: 10, b: 30 };
  const COLORS = ["var(--accent)", "#f59e0b", "#10b981", "#8b5cf6"];

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

  // Redraw on theme flips (same approach as Plot.svelte).
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

  function cssColor(el: Element, name: string, fallback: string): string {
    const v = getComputedStyle(el).getPropertyValue(name).trim();
    return v || fallback;
  }

  /** Round v to a "nice" tick step (1/2/5 x 10^k). */
  function niceStep(range: number, target: number): number {
    const raw = range / Math.max(1, target);
    const mag = Math.pow(10, Math.floor(Math.log10(raw)));
    const norm = raw / mag;
    const step = norm < 1.5 ? 1 : norm < 3.5 ? 2 : norm < 7.5 ? 5 : 10;
    return step * mag;
  }

  function fmtTick(v: number): string {
    const a = Math.abs(v);
    if (a >= 1e4) return v.toExponential(0).replace("+", "");
    if (a >= 1000) return String(Math.round(v));
    return String(parseFloat(v.toPrecision(3)));
  }

  $effect(() => {
    void themeTick;
    const c = canvas;
    const w = width;
    const xs = x;
    const all = series;
    if (!c || w < 80 || xs.length < 2) return;

    const dpr = window.devicePixelRatio || 1;
    c.width = Math.floor(w * dpr);
    c.height = Math.floor(HEIGHT * dpr);
    const ctx = c.getContext("2d");
    if (!ctx) return;
    ctx.scale(dpr, dpr);

    const fg = cssColor(c, "--fg-muted", "#667");
    const grid = cssColor(c, "--border", "#ccc");
    const accent = cssColor(c, "--accent", "#2563eb");
    const palette = COLORS.map((col) =>
      col.startsWith("var(") ? accent : col,
    );

    // x mapping (optionally log).
    const usableLog = logx && xs.every((v) => v > 0);
    const tx = (v: number) => (usableLog ? Math.log10(v) : v);
    const x0 = tx(xs[0]);
    const x1 = tx(xs[xs.length - 1]);
    const plotW = w - PAD.l - PAD.r;
    const plotH = HEIGHT - PAD.t - PAD.b;
    const px = (v: number) => PAD.l + ((tx(v) - x0) / (x1 - x0)) * plotW;

    // y range over finite samples.
    let lo = Infinity;
    let hi = -Infinity;
    for (const s of all) {
      for (const yv of s.ys) {
        if (yv !== null && Number.isFinite(yv)) {
          lo = Math.min(lo, yv);
          hi = Math.max(hi, yv);
        }
      }
    }
    if (!Number.isFinite(lo) || !Number.isFinite(hi)) return;
    if (hi - lo < 1e-9) {
      hi += 1;
      lo -= 1;
    }
    const pad = (hi - lo) * 0.06;
    lo -= pad;
    hi += pad;
    const py = (v: number) => PAD.t + (1 - (v - lo) / (hi - lo)) * plotH;

    ctx.clearRect(0, 0, w, HEIGHT);
    ctx.font = "11px system-ui, sans-serif";

    // y grid + ticks.
    ctx.strokeStyle = grid;
    ctx.fillStyle = fg;
    ctx.lineWidth = 1;
    const ystep = niceStep(hi - lo, 6);
    for (let v = Math.ceil(lo / ystep) * ystep; v <= hi; v += ystep) {
      const yy = py(v);
      ctx.beginPath();
      ctx.moveTo(PAD.l, yy);
      ctx.lineTo(w - PAD.r, yy);
      ctx.stroke();
      ctx.textAlign = "right";
      ctx.textBaseline = "middle";
      ctx.fillText(fmtTick(v), PAD.l - 6, yy);
    }

    // x ticks: decades when log, nice steps otherwise.
    ctx.textAlign = "center";
    ctx.textBaseline = "top";
    if (usableLog) {
      for (let d = Math.ceil(x0); d <= Math.floor(x1); d++) {
        const v = Math.pow(10, d);
        const xx = px(v);
        ctx.beginPath();
        ctx.moveTo(xx, PAD.t);
        ctx.lineTo(xx, HEIGHT - PAD.b);
        ctx.stroke();
        ctx.fillText(fmtTick(v), xx, HEIGHT - PAD.b + 6);
      }
    } else {
      const xstep = niceStep(xs[xs.length - 1] - xs[0], 7);
      for (
        let v = Math.ceil(xs[0] / xstep) * xstep;
        v <= xs[xs.length - 1];
        v += xstep
      ) {
        const xx = px(v);
        ctx.beginPath();
        ctx.moveTo(xx, PAD.t);
        ctx.lineTo(xx, HEIGHT - PAD.b);
        ctx.stroke();
        ctx.fillText(fmtTick(v), xx, HEIGHT - PAD.b + 6);
      }
    }

    // axis labels.
    if (xlabel) {
      ctx.textAlign = "center";
      ctx.fillText(xlabel, PAD.l + plotW / 2, HEIGHT - 14);
    }
    if (ylabel) {
      ctx.save();
      ctx.translate(12, PAD.t + plotH / 2);
      ctx.rotate(-Math.PI / 2);
      ctx.textAlign = "center";
      ctx.textBaseline = "bottom";
      ctx.fillText(ylabel, 0, 0);
      ctx.restore();
    }

    // series lines.
    all.forEach((s, si) => {
      ctx.strokeStyle = palette[si % palette.length];
      ctx.lineWidth = 1.8;
      ctx.beginPath();
      let pen = false;
      for (let i = 0; i < xs.length && i < s.ys.length; i++) {
        const yv = s.ys[i];
        if (yv === null || !Number.isFinite(yv)) {
          pen = false;
          continue;
        }
        const xx = px(xs[i]);
        const yy = Math.max(PAD.t, Math.min(HEIGHT - PAD.b, py(yv)));
        if (pen) ctx.lineTo(xx, yy);
        else ctx.moveTo(xx, yy);
        pen = true;
      }
      ctx.stroke();
    });
  });
</script>

<div class="chart" bind:this={host}>
  <canvas bind:this={canvas} style:width="100%" style:height="{HEIGHT}px"></canvas>
  {#if series.length > 1}
    <div class="legend">
      {#each series as s, i (s.label)}
        <span class="key">
          <span
            class="swatch"
            style:background={i === 0 ? "var(--accent)" : COLORS[i % COLORS.length]}
          ></span>
          {s.label}
        </span>
      {/each}
    </div>
  {/if}
</div>

<style>
  .chart {
    width: 100%;
    min-width: 0;
  }
  canvas {
    display: block;
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) / 2);
    background: var(--bg);
  }
  .legend {
    display: flex;
    gap: 1rem;
    flex-wrap: wrap;
    margin-top: 0.35rem;
    font-size: 0.78rem;
    color: var(--fg-muted);
  }
  .key {
    display: inline-flex;
    align-items: center;
    gap: 0.35rem;
  }
  .swatch {
    width: 14px;
    height: 3px;
    border-radius: 2px;
  }
</style>
