<script lang="ts">
  // Interactive coordinate plane (Desmos-style): pan by drag, zoom by wheel /
  // pinch / buttons, dynamic axes + gridlines with axis-attached labels, and
  // multi-series rendering. Purely a view: it owns the viewport interaction
  // and drawing; the parent supplies already-sampled `series` and resamples
  // when `view` changes.
  import {
    type View,
    type DrawSeries,
    xToPx,
    yToPx,
    pxToX,
    pxToY,
    xRange,
    yRange,
    panned,
    zoomedAt,
    niceStep,
    ticks,
    tickDecimals,
  } from "../graph/viewport";

  interface Props {
    view: View;
    series?: DrawSeries[];
    /** Console/compact mode uses a shorter graph. */
    height?: number;
  }

  let { view = $bindable(), series = [], height = 0 }: Props = $props();

  let host: HTMLDivElement | undefined = $state();
  let canvas: HTMLCanvasElement | undefined = $state();
  let width = $state(0);
  let measuredH = $state(0);
  let themeTick = $state(0);
  let cursor = $state<{ x: number; y: number } | null>(null);

  const H = $derived(height > 0 ? height : measuredH || 460);

  // --- sizing + theme --------------------------------------------------------
  $effect(() => {
    const el = host;
    if (!el) return;
    const ro = new ResizeObserver((entries) => {
      const r = entries[0].contentRect;
      width = Math.max(0, Math.floor(r.width));
      measuredH = Math.max(0, Math.floor(r.height));
    });
    ro.observe(el);
    return () => ro.disconnect();
  });

  $effect(() => {
    const bump = () => (themeTick += 1);
    const mo = new MutationObserver(bump);
    mo.observe(document.documentElement, { attributes: true, attributeFilter: ["data-theme"] });
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

  // --- redraw on any relevant change ----------------------------------------
  $effect(() => {
    void themeTick;
    void series;
    void cursor;
    const c = canvas;
    const w = width;
    const h = H;
    const v = view;
    if (!c || w < 20 || h < 20) return;
    draw(c, w, h, v);
  });

  function fmtTick(val: number, step: number): string {
    if (val === 0) return "0";
    const abs = Math.abs(val);
    if (abs >= 1e5 || abs < 1e-4) return val.toExponential(0).replace("e+", "e");
    return String(parseFloat(val.toFixed(tickDecimals(step))));
  }

  function draw(c: HTMLCanvasElement, w: number, h: number, v: View) {
    const dpr = window.devicePixelRatio || 1;
    if (c.width !== Math.round(w * dpr) || c.height !== Math.round(h * dpr)) {
      c.width = Math.round(w * dpr);
      c.height = Math.round(h * dpr);
      c.style.width = w + "px";
      c.style.height = h + "px";
    }
    const ctx = c.getContext("2d");
    if (!ctx) return;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

    const bg = cssColor(c, "--bg-panel", "#fff");
    const gridMinor = cssColor(c, "--border", "#e2e5e9");
    const gridMajor = cssColor(c, "--fg-muted", "#5c6470");
    const axis = cssColor(c, "--fg", "#1a1d21");
    const label = cssColor(c, "--fg-muted", "#5c6470");

    ctx.fillStyle = bg;
    ctx.fillRect(0, 0, w, h);

    const [xlo, xhi] = xRange(v, w);
    const [ylo, yhi] = yRange(v, h);
    const majX = niceStep(xhi - xlo, Math.max(2, w / 90));
    const majY = niceStep(yhi - ylo, Math.max(2, h / 90));

    // Minor gridlines (major / 5).
    ctx.lineWidth = 1;
    ctx.strokeStyle = gridMinor;
    ctx.globalAlpha = 0.5;
    drawGrid(ctx, v, w, h, xlo, xhi, ylo, yhi, majX / 5, majY / 5);
    // Major gridlines.
    ctx.globalAlpha = 0.9;
    drawGrid(ctx, v, w, h, xlo, xhi, ylo, yhi, majX, majY);
    ctx.globalAlpha = 1;

    // Axes: the x-axis (y=0) and y-axis (x=0), clamped to the edge when the
    // origin is off-screen so labels stay attached to the axis line.
    const axisY = Math.max(0, Math.min(h, yToPx(0, v, h)));
    const axisX = Math.max(0, Math.min(w, xToPx(0, v, w)));
    ctx.strokeStyle = axis;
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.moveTo(0, Math.round(axisY) + 0.5);
    ctx.lineTo(w, Math.round(axisY) + 0.5);
    ctx.moveTo(Math.round(axisX) + 0.5, 0);
    ctx.lineTo(Math.round(axisX) + 0.5, h);
    ctx.stroke();

    // Tick labels along the axes.
    ctx.fillStyle = label;
    ctx.font =
      "11px " + (cssColor(c, "--font-sans", "") || "system-ui, sans-serif");
    const labelBelow = axisY < h - 22; // room under the x-axis?
    ctx.textAlign = "center";
    ctx.textBaseline = labelBelow ? "top" : "bottom";
    for (const x of ticks(xlo, xhi, majX)) {
      if (Math.abs(x) < majX / 2) continue; // skip 0 (drawn once at origin)
      const px = xToPx(x, v, w);
      ctx.fillText(fmtTick(x, majX), px, axisY + (labelBelow ? 5 : -5));
    }
    const labelLeft = axisX > 34;
    ctx.textAlign = labelLeft ? "right" : "left";
    ctx.textBaseline = "middle";
    for (const y of ticks(ylo, yhi, majY)) {
      if (Math.abs(y) < majY / 2) continue;
      const py = yToPx(y, v, h);
      ctx.fillText(fmtTick(y, majY), axisX + (labelLeft ? -6 : 6), py);
    }
    // Origin label.
    if (axisX > 12 && axisY < h - 12 && axisX < w && axisY > 0) {
      ctx.textAlign = "right";
      ctx.textBaseline = "top";
      ctx.fillText("0", axisX - 4, axisY + 4);
    }

    // Series polylines, clipped to the panel.
    ctx.save();
    ctx.beginPath();
    ctx.rect(0, 0, w, h);
    ctx.clip();
    ctx.lineJoin = "round";
    ctx.lineCap = "round";
    ctx.lineWidth = 2.2;
    for (const s of series) {
      if (!s.visible || s.xs.length === 0) continue;
      ctx.strokeStyle = s.color;
      ctx.beginPath();
      let pen = false;
      for (let i = 0; i < s.xs.length; i++) {
        const y = s.ys[i];
        if (y === null || !Number.isFinite(y)) {
          pen = false;
          continue;
        }
        const px = xToPx(s.xs[i], v, w);
        const py = yToPx(y, v, h);
        // Break the line across near-vertical jumps (asymptotes).
        if (pen && Math.abs(py) < 1e7) ctx.lineTo(px, py);
        else {
          ctx.moveTo(px, py);
          pen = true;
        }
      }
      ctx.stroke();
    }
    ctx.restore();
  }

  function drawGrid(
    ctx: CanvasRenderingContext2D,
    v: View,
    w: number,
    h: number,
    xlo: number,
    xhi: number,
    ylo: number,
    yhi: number,
    stepX: number,
    stepY: number,
  ) {
    ctx.beginPath();
    for (const x of ticks(xlo, xhi, stepX)) {
      const px = Math.round(xToPx(x, v, w)) + 0.5;
      ctx.moveTo(px, 0);
      ctx.lineTo(px, h);
    }
    for (const y of ticks(ylo, yhi, stepY)) {
      const py = Math.round(yToPx(y, v, h)) + 0.5;
      ctx.moveTo(0, py);
      ctx.lineTo(w, py);
    }
    ctx.stroke();
  }

  // --- pointer interaction ---------------------------------------------------
  let dragging = false;
  let lastX = 0;
  let lastY = 0;
  const pointers = new Map<number, { x: number; y: number }>();
  let pinchDist = 0;

  function localXY(e: PointerEvent): { x: number; y: number } {
    const r = canvas!.getBoundingClientRect();
    return { x: e.clientX - r.left, y: e.clientY - r.top };
  }

  function onPointerDown(e: PointerEvent): void {
    canvas!.setPointerCapture(e.pointerId);
    const p = localXY(e);
    pointers.set(e.pointerId, p);
    if (pointers.size === 1) {
      dragging = true;
      lastX = p.x;
      lastY = p.y;
    } else if (pointers.size === 2) {
      const [a, b] = [...pointers.values()];
      pinchDist = Math.hypot(a.x - b.x, a.y - b.y);
    }
  }

  function onPointerMove(e: PointerEvent): void {
    const p = localXY(e);
    if (pointers.has(e.pointerId)) pointers.set(e.pointerId, p);
    cursor = { x: p.x, y: p.y };

    if (pointers.size >= 2) {
      const [a, b] = [...pointers.values()];
      const d = Math.hypot(a.x - b.x, a.y - b.y);
      if (pinchDist > 0 && d > 0) {
        const mid = { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 };
        view = zoomedAt(view, d / pinchDist, mid.x, mid.y, width, H);
      }
      pinchDist = d;
      return;
    }
    if (!dragging) return;
    view = panned(view, p.x - lastX, p.y - lastY);
    lastX = p.x;
    lastY = p.y;
  }

  function onPointerUp(e: PointerEvent): void {
    pointers.delete(e.pointerId);
    if (pointers.size < 2) pinchDist = 0;
    if (pointers.size === 0) dragging = false;
    if (canvas?.hasPointerCapture(e.pointerId)) canvas.releasePointerCapture(e.pointerId);
  }

  function onWheel(e: WheelEvent): void {
    e.preventDefault();
    const r = canvas!.getBoundingClientRect();
    const factor = Math.exp(-e.deltaY * 0.0015);
    view = zoomedAt(view, factor, e.clientX - r.left, e.clientY - r.top, width, H);
  }

  function zoomButton(factor: number): void {
    view = zoomedAt(view, factor, width / 2, H / 2, width, H);
  }
  function home(): void {
    view = { cx: 0, cy: 0, scale: 40 };
  }

  const readout = $derived.by(() => {
    if (!cursor || dragging) return null;
    return { x: pxToX(cursor.x, view, width), y: pxToY(cursor.y, view, H) };
  });
</script>

<div class="graph" bind:this={host} style:--gh={height > 0 ? `${height}px` : null}>
  <canvas
    bind:this={canvas}
    aria-label="Interactive graph — drag to pan, scroll to zoom"
    onpointerdown={onPointerDown}
    onpointermove={onPointerMove}
    onpointerup={onPointerUp}
    onpointercancel={onPointerUp}
    onpointerleave={() => (cursor = null)}
    onwheel={onWheel}
    ondblclick={() => zoomButton(1.6)}
  ></canvas>

  <div class="zoom" role="group" aria-label="Zoom">
    <button onclick={() => zoomButton(1.4)} aria-label="Zoom in" title="Zoom in">+</button>
    <button onclick={() => zoomButton(1 / 1.4)} aria-label="Zoom out" title="Zoom out">−</button>
    <button class="home" onclick={home} aria-label="Reset view" title="Reset view">
      <svg viewBox="0 0 16 16" width="13" height="13" aria-hidden="true" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M2.5 8 8 3l5.5 5M4 7v5.5h8V7" /></svg>
    </button>
  </div>

  {#if readout}
    <div class="readout" aria-hidden="true">
      ({readout.x.toPrecision(4).replace(/\.?0+$/, "")}, {readout.y
        .toPrecision(4)
        .replace(/\.?0+$/, "")})
    </div>
  {/if}
</div>

<style>
  .graph {
    position: relative;
    width: 100%;
    height: var(--gh, 100%);
    min-height: 20rem;
    border: 1px solid var(--border);
    border-radius: var(--radius);
    overflow: hidden;
    background: var(--bg-panel);
    line-height: 0;
    touch-action: none;
  }
  canvas {
    display: block;
    width: 100%;
    height: 100%;
    cursor: grab;
  }
  canvas:active {
    cursor: grabbing;
  }
  .zoom {
    position: absolute;
    top: 0.6rem;
    right: 0.6rem;
    display: flex;
    flex-direction: column;
    gap: 1px;
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) / 1.5);
    overflow: hidden;
    background: var(--border);
    box-shadow: 0 1px 6px rgb(0 0 0 / 12%);
  }
  .zoom button {
    width: 2rem;
    height: 2rem;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    font: inherit;
    font-size: 1.15rem;
    color: var(--fg-muted);
    background: var(--bg-panel);
    border: none;
    cursor: pointer;
  }
  .zoom button:hover {
    color: var(--accent);
    background: color-mix(in srgb, var(--accent) 8%, var(--bg-panel));
  }
  .zoom .home svg {
    display: block;
  }
  .readout {
    position: absolute;
    left: 0.6rem;
    bottom: 0.6rem;
    padding: 0.15rem 0.55rem;
    font-family: var(--font-mono);
    font-size: 0.75rem;
    line-height: 1.4;
    color: var(--fg-muted);
    background: color-mix(in srgb, var(--bg-panel) 82%, transparent);
    border: 1px solid var(--border);
    border-radius: 999px;
    pointer-events: none;
    white-space: nowrap;
  }
</style>
