<script lang="ts">
  // Interactive coordinate plane (Desmos-style): pan by drag, zoom by wheel /
  // pinch / buttons, dynamic axes + gridlines with axis-attached labels, and
  // multi-series rendering. Purely a view: it owns the viewport interaction
  // and drawing; the parent supplies already-sampled `series` and resamples
  // when `view` changes.
  import { untrack } from "svelte";
  import {
    type View,
    type DrawSeries,
    type PointHandle,
    xToPx,
    yToPx,
    pxToX,
    pxToY,
    xRange,
    yRange,
    panned,
    zoomedAt,
    viewForKey,
    isXMonotonic,
    interpolateAtX,
    niceStep,
    ticks,
    tickDecimals,
  } from "../graph/viewport";

  interface Props {
    view: View;
    series?: DrawSeries[];
    /** Draggable point handles, keyed by points-series id (aligned with xs/ys). */
    handles?: Record<string, PointHandle[]>;
    /** Per-row label pixel offsets (from dragging), keyed by String(rowId). */
    labelOffsets?: Record<string, { dx: number; dy: number }>;
    /** Console/compact mode uses a shorter graph. */
    height?: number;
    onPointDragStart?: () => void;
    onPointDrag?: (rowId: number, coordIndex: number, wx: number, wy: number) => void;
    onPointCommit?: (rowId: number, coordIndex: number, wx: number, wy: number) => void;
    onPointDragCancel?: () => void;
    /** Commit a dragged label's new pixel offset. */
    onLabelMove?: (rowId: number, dx: number, dy: number) => void;
  }

  let {
    view = $bindable(),
    series = [],
    handles = {},
    labelOffsets = {},
    height = 0,
    onPointDragStart,
    onPointDrag,
    onPointCommit,
    onPointDragCancel,
    onLabelMove,
  }: Props = $props();

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
    void dragging; // redraw the trace when a drag ends
    void pointDrag; // and while dragging a point
    void ghost;
    void labelDrag; // redraw the moving label live
    void labelOffsets; // and when a committed offset changes
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

    // Drawables, clipped to the panel: regions (under) → lines → points.
    ctx.save();
    ctx.beginPath();
    ctx.rect(0, 0, w, h);
    ctx.clip();

    for (const s of series) {
      if (s.visible && s.kind === "region" && s.region) drawRegion(ctx, v, w, h, s);
    }

    // Definite-integral area shading (drawn under the curves, above the grid).
    for (const s of series) {
      if (s.visible && s.kind === "area") drawArea(ctx, v, w, h, s, bg, label);
    }

    // Slope/direction field: thin, muted segments drawn under the curves.
    ctx.lineJoin = "round";
    ctx.lineCap = "round";
    for (const s of series) {
      if (!s.visible || s.kind !== "field" || s.xs.length === 0) continue;
      ctx.strokeStyle = s.color;
      ctx.globalAlpha = 0.42;
      ctx.lineWidth = 1.2;
      ctx.beginPath();
      let pen = false;
      for (let i = 0; i < s.xs.length; i++) {
        const x = s.xs[i];
        const y = s.ys[i];
        if (x === null || y === null || !Number.isFinite(x) || !Number.isFinite(y)) {
          pen = false;
          continue;
        }
        const px = xToPx(x, v, w);
        const py = yToPx(y, v, h);
        if (pen) ctx.lineTo(px, py);
        else {
          ctx.moveTo(px, py);
          pen = true;
        }
      }
      ctx.stroke();
      ctx.globalAlpha = 1;
    }

    for (const s of series) {
      if (!s.visible || s.kind !== "line" || s.xs.length === 0) continue;
      ctx.strokeStyle = s.color;
      ctx.lineWidth = s.width ?? 2.2;
      // An explicit dash pattern (per-row style) wins; else the legacy `dash`
      // flag (asymptotes) draws a fixed dash; else solid.
      ctx.setLineDash(s.dashArr ?? (s.dash ? [6, 5] : []));
      ctx.lineCap = s.dashArr && s.dashArr[0] <= 2 ? "round" : "butt"; // dotted → round caps
      ctx.beginPath();
      let pen = false;
      for (let i = 0; i < s.xs.length; i++) {
        const y = s.ys[i];
        const x = s.xs[i];
        if (x === null || y === null || !Number.isFinite(x) || !Number.isFinite(y)) {
          pen = false;
          continue;
        }
        const px = xToPx(x, v, w);
        const py = yToPx(y, v, h);
        // Bound both pixels: an x=f(y) asymptote blows up px, not py, so a
        // py-only guard would draw a spurious near-horizontal spike.
        if (pen && Math.abs(px) < 1e7 && Math.abs(py) < 1e7) ctx.lineTo(px, py);
        else {
          ctx.moveTo(px, py);
          pen = true;
        }
      }
      ctx.stroke();
    }
    ctx.setLineDash([]); // don't let an asymptote's dash leak into later strokes
    ctx.lineCap = "butt";

    let ghostDrawn = false;
    for (const s of series) {
      if (!s.visible || s.kind !== "points") continue;
      ctx.fillStyle = s.color;
      const r = s.width ? 4 * (s.width / 2.2) : 4; // scale marker radius with weight
      for (let i = 0; i < s.xs.length; i++) {
        const isGhost = !!ghost && ghost.sid === s.id && handles[s.id]?.[i]?.coordIndex === ghost.ci;
        let x = s.xs[i];
        let y = s.ys[i];
        if (isGhost && ghost) {
          x = ghost.x; // draw at the live/committed position, not the stale sample
          y = ghost.y;
          ghostDrawn = true;
        }
        if (x === null || y === null || !Number.isFinite(x) || !Number.isFinite(y)) continue;
        drawPointDot(ctx, xToPx(x, v, w), yToPx(y, v, h), s.color, isGhost, r);
      }
    }
    // The ghost's source point may not be in `series` yet (e.g. just committed);
    // draw it standalone so it never blinks to a stale position.
    if (ghost && !ghostDrawn) {
      const s = series.find((ss) => ss.id === ghost!.sid);
      drawPointDot(ctx, xToPx(ghost.x, v, w), yToPx(ghost.y, v, h), s?.color ?? "#2563eb", true);
    }

    // Points of interest (exact zeros / y-intercept from the CAS): hollow
    // rings, always visible; hovering one reveals its exact coordinate label.
    for (const s of series) {
      if (!s.visible || s.kind !== "poi") continue;
      for (let i = 0; i < s.xs.length; i++) {
        const x = s.xs[i];
        const y = s.ys[i];
        if (x === null || y === null || !Number.isFinite(x) || !Number.isFinite(y)) continue;
        const px = xToPx(x, v, w);
        const py = yToPx(y, v, h);
        ctx.beginPath();
        ctx.arc(px, py, 4, 0, 2 * Math.PI);
        ctx.fillStyle = bg;
        ctx.fill();
        ctx.lineWidth = 2;
        ctx.strokeStyle = s.color;
        ctx.stroke();
      }
    }

    // Row labels ("tags"): user-set text drawn at each point (point rows) or at
    // the last on-screen sample of a curve (function rows), shifted by the row's
    // drag offset. Each drawn rect is recorded so the label can be grabbed.
    labelHits = [];
    for (const s of series) {
      if (!s.visible || !s.tag) continue;
      const rowId = Number(s.id);
      // The active drag shows its live offset; otherwise use the stored one.
      const off =
        labelDrag && labelDrag.rowId === rowId
          ? { dx: labelDrag.curDx, dy: labelDrag.curDy }
          : labelOffsets[s.id] ?? { dx: 0, dy: 0 };
      const ok = (i: number) => {
        const x = s.xs[i];
        const y = s.ys[i];
        return x !== null && y !== null && Number.isFinite(x) && Number.isFinite(y);
      };
      if (s.kind === "points") {
        for (let i = 0; i < s.xs.length; i++) {
          if (!ok(i)) continue;
          drawTag(ctx, xToPx(s.xs[i] as number, v, w) + 8 + off.dx, yToPx(s.ys[i] as number, v, h) - 9 + off.dy, s.tag, s.color, bg, rowId);
        }
      } else if (s.kind === "line") {
        let ai = -1;
        for (let i = 0; i < s.xs.length; i++) {
          if (!ok(i)) continue;
          const px = xToPx(s.xs[i] as number, v, w);
          const py = yToPx(s.ys[i] as number, v, h);
          if (px >= 0 && px <= w && py >= 0 && py <= h) ai = i;
        }
        if (ai >= 0) drawTag(ctx, xToPx(s.xs[ai] as number, v, w) + 6 + off.dx, yToPx(s.ys[ai] as number, v, h) - 9 + off.dy, s.tag, s.color, bg, rowId);
      }
    }

    drawTrace(ctx, v, w, h, label, bg);
    ctx.restore();
  }

  // A small pill-backed text label in the series color, clamped on-screen. The
  // drawn rect is recorded (with its rowId) so the label can be grabbed + moved.
  function drawTag(
    ctx: CanvasRenderingContext2D,
    px: number,
    py: number,
    text: string,
    color: string,
    bg: string,
    rowId: number,
  ): void {
    ctx.font = "12px " + (cssColor(canvas!, "--font-sans", "") || "system-ui, sans-serif");
    ctx.textAlign = "left";
    ctx.textBaseline = "middle";
    const tw = ctx.measureText(text).width;
    const w = canvas ? canvas.clientWidth : 1e4;
    const x = Math.max(2, Math.min(px, w - tw - 10));
    const y = Math.max(10, py);
    ctx.fillStyle = bg;
    ctx.globalAlpha = 0.85;
    roundRect(ctx, x - 3, y - 9, tw + 8, 18, 5);
    ctx.fill();
    ctx.globalAlpha = 1;
    ctx.fillStyle = color;
    ctx.fillText(text, x + 1, y);
    labelHits.push({ rowId, x0: x - 3, y0: y - 9, x1: x - 3 + tw + 8, y1: y - 9 + 18 });
  }

  // The rowId of the topmost label whose rect contains p, or null.
  function hitLabel(p: { x: number; y: number }): number | null {
    for (let i = labelHits.length - 1; i >= 0; i--) {
      const r = labelHits[i];
      if (p.x >= r.x0 && p.x <= r.x1 && p.y >= r.y0 && p.y <= r.y1) return r.rowId;
    }
    return null;
  }

  function drawPointDot(
    ctx: CanvasRenderingContext2D,
    px: number,
    py: number,
    color: string,
    emphasized: boolean,
    radius = 4,
  ): void {
    ctx.fillStyle = color;
    ctx.beginPath();
    ctx.arc(px, py, emphasized ? radius + 2 : radius, 0, 2 * Math.PI);
    ctx.fill();
    if (emphasized) {
      ctx.beginPath();
      ctx.arc(px, py, 10, 0, 2 * Math.PI);
      ctx.strokeStyle = color;
      ctx.globalAlpha = 0.35;
      ctx.lineWidth = 2;
      ctx.stroke();
      ctx.globalAlpha = 1;
    }
  }

  // Shade the signed area between a sampled boundary (`y = f(x)` over `[a, b]`)
  // and the x-axis: fill each contiguous sub-band down to y = 0 (so the band
  // flips below the axis wherever f < 0), stroke the boundary on top, and draw
  // the exact ∫ value from the CAS centred over the region.
  function drawArea(
    ctx: CanvasRenderingContext2D,
    v: View,
    w: number,
    h: number,
    s: DrawSeries,
    bg: string,
    labelColor: string,
  ): void {
    const axisPy = yToPx(0, v, h);
    const finite = (i: number) =>
      s.xs[i] !== null &&
      s.ys[i] !== null &&
      Number.isFinite(s.xs[i] as number) &&
      Number.isFinite(s.ys[i] as number);

    ctx.fillStyle = s.color;
    ctx.globalAlpha = 0.2;
    ctx.beginPath();
    let pen = false;
    let bandLastPx = 0;
    for (let i = 0; i < s.xs.length; i++) {
      if (!finite(i)) {
        if (pen) {
          ctx.lineTo(bandLastPx, axisPy);
          ctx.closePath();
          pen = false;
        }
        continue;
      }
      const px = xToPx(s.xs[i] as number, v, w);
      const py = yToPx(s.ys[i] as number, v, h);
      if (!pen) {
        ctx.moveTo(px, axisPy);
        ctx.lineTo(px, py);
        pen = true;
      } else {
        ctx.lineTo(px, py);
      }
      bandLastPx = px;
    }
    if (pen) {
      ctx.lineTo(bandLastPx, axisPy);
      ctx.closePath();
    }
    ctx.fill();
    ctx.globalAlpha = 1;

    // Boundary stroke.
    ctx.strokeStyle = s.color;
    ctx.lineWidth = 2;
    ctx.beginPath();
    pen = false;
    for (let i = 0; i < s.xs.length; i++) {
      if (!finite(i)) {
        pen = false;
        continue;
      }
      const px = xToPx(s.xs[i] as number, v, w);
      const py = yToPx(s.ys[i] as number, v, h);
      if (pen) ctx.lineTo(px, py);
      else {
        ctx.moveTo(px, py);
        pen = true;
      }
    }
    ctx.stroke();

    // Value label centred over the band, midway between the axis and the curve.
    if (!s.label) return;
    let anchor = -1;
    const target = (s.xs.length - 1) / 2;
    for (let i = 0; i < s.xs.length; i++) {
      if (finite(i) && (anchor < 0 || Math.abs(i - target) < Math.abs(anchor - target))) anchor = i;
    }
    if (anchor < 0) return;
    const ax = xToPx(s.xs[anchor] as number, v, w);
    const ay = (axisPy + yToPx(s.ys[anchor] as number, v, h)) / 2;
    ctx.font = "12px " + (cssColor(canvas!, "--font-mono", "") || "monospace");
    const tw = ctx.measureText(s.label).width;
    ctx.fillStyle = bg;
    roundRect(ctx, ax - tw / 2 - 6, ay - 10, tw + 12, 20, 5);
    ctx.fill();
    ctx.fillStyle = labelColor;
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    ctx.fillText(s.label, ax, ay);
  }

  function drawRegion(
    ctx: CanvasRenderingContext2D,
    v: View,
    w: number,
    h: number,
    s: DrawSeries,
  ): void {
    const rg = s.region!;
    // Grid nodes are spaced across [x0,x1] inclusive (/(n-1)), matching
    // sampleGrid + marchingSquares, so the fill aligns with its own contour.
    const cw = (rg.x1 - rg.x0) / (rg.nx - 1);
    const ch = (rg.y1 - rg.y0) / (rg.ny - 1);
    ctx.fillStyle = s.color;
    ctx.globalAlpha = 0.18;
    for (let j = 0; j < rg.ny; j++) {
      for (let i = 0; i < rg.nx; i++) {
        if (!rg.mask[j * rg.nx + i]) continue;
        // Center the fill cell on its sample node (node ± half a cell).
        const cx = rg.x0 + i * cw;
        const cy = rg.y0 + j * ch;
        const px = xToPx(cx - cw / 2, v, w);
        const py = yToPx(cy + ch / 2, v, h);
        ctx.fillRect(px, py, Math.abs(cw * v.scale) + 1, Math.abs(ch * v.scale) + 1);
      }
    }
    ctx.globalAlpha = 1;
  }

  /** Hover trace: follow a `y=f(x)` curve continuously at the cursor's x, snap
   *  to points-of-interest markers (which carry an exact CAS label), and fall
   *  back to the nearest sampled vertex for non-monotonic curves (x=f(y),
   *  parametric, polar). */
  function drawTrace(
    ctx: CanvasRenderingContext2D,
    v: View,
    w: number,
    h: number,
    label: string,
    bg: string,
  ): void {
    if (!cursor || dragging || pointDrag || labelDrag) return;
    type Hit = { px: number; py: number; x: number; y: number; color: string; label?: string };

    // 1) Points of interest snap to the nearest marker (exact coordinate label).
    let poi: Hit | null = null;
    let poiD = 24 * 24;
    for (const s of series) {
      if (!s.visible || s.kind !== "poi") continue;
      for (let i = 0; i < s.xs.length; i++) {
        const x = s.xs[i];
        const y = s.ys[i];
        if (x === null || y === null || !Number.isFinite(x) || !Number.isFinite(y)) continue;
        const px = xToPx(x, v, w);
        const py = yToPx(y, v, h);
        const d = (px - cursor.x) ** 2 + (py - cursor.y) ** 2;
        if (d < poiD) {
          poiD = d;
          poi = { px, py, x, y, color: s.color, label: s.labels?.[i] ?? undefined };
        }
      }
    }

    // 2) Continuous trace: the y=f(x) curve nearest the cursor at its x.
    let curve: Hit | null = null;
    if (!poi) {
      const wx = pxToX(cursor.x, v, w);
      let bestDy = 30; // px — how close vertically the cursor must be to attach
      for (const s of series) {
        if (!s.visible || s.kind !== "line" || !isXMonotonic(s.xs)) continue;
        const wy = interpolateAtX(s.xs, s.ys, wx);
        if (wy === null || !Number.isFinite(wy)) continue;
        const py = yToPx(wy, v, h);
        const dy = Math.abs(py - cursor.y);
        if (dy < bestDy) {
          bestDy = dy;
          curve = { px: cursor.x, py, x: wx, y: wy, color: s.color };
        }
      }
    }

    // 3) Fallback: nearest sampled vertex on any remaining curve.
    let vertex: Hit | null = null;
    if (!poi && !curve) {
      let vD = 24 * 24;
      for (const s of series) {
        if (!s.visible || s.kind !== "line") continue;
        for (let i = 0; i < s.xs.length; i++) {
          const x = s.xs[i];
          const y = s.ys[i];
          if (x === null || y === null || !Number.isFinite(x) || !Number.isFinite(y)) continue;
          const px = xToPx(x, v, w);
          const py = yToPx(y, v, h);
          const d = (px - cursor.x) ** 2 + (py - cursor.y) ** 2;
          if (d < vD) {
            vD = d;
            vertex = { px, py, x, y, color: s.color };
          }
        }
      }
    }

    const best = poi ?? curve ?? vertex;
    if (!best) return;
    ctx.beginPath();
    ctx.arc(best.px, best.py, 4.5, 0, 2 * Math.PI);
    ctx.fillStyle = best.color;
    ctx.fill();
    ctx.lineWidth = 2;
    ctx.strokeStyle = bg;
    ctx.stroke();
    const txt = best.label ?? `(${fmtCoord(best.x)}, ${fmtCoord(best.y)})`;
    ctx.font = "12px " + (cssColor(canvas!, "--font-mono", "") || "monospace");
    const tw = ctx.measureText(txt).width;
    let lx = best.px + 10;
    if (lx + tw + 10 > w) lx = best.px - tw - 18;
    const ly = best.py - 14;
    ctx.fillStyle = color_mix(bg);
    roundRect(ctx, lx - 5, ly - 12, tw + 12, 20, 5);
    ctx.fill();
    ctx.fillStyle = label;
    ctx.textAlign = "left";
    ctx.textBaseline = "middle";
    ctx.fillText(txt, lx + 1, ly - 1);
  }

  function fmtCoord(v: number): string {
    if (v === 0) return "0";
    return String(parseFloat(v.toPrecision(4)));
  }
  function color_mix(bg: string): string {
    return bg; // solid panel bg behind the label
  }
  function roundRect(ctx: CanvasRenderingContext2D, x: number, y: number, w: number, h: number, r: number): void {
    ctx.beginPath();
    ctx.moveTo(x + r, y);
    ctx.arcTo(x + w, y, x + w, y + h, r);
    ctx.arcTo(x + w, y + h, x, y + h, r);
    ctx.arcTo(x, y + h, x, y, r);
    ctx.arcTo(x, y, x + w, y, r);
    ctx.closePath();
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
  let dragging = $state(false);
  let lastX = 0;
  let lastY = 0;
  const pointers = new Map<number, { x: number; y: number }>();
  let pinchDist = 0;

  // Draggable-point state: the grabbed handle and a live ghost position drawn
  // decoupled from the (debounced) `series` so the dot tracks the cursor.
  const GRAB = 11; // px hit radius
  // Keyed by the stable coordIndex (ci), not the volatile series xs-index, so a
  // mid-drag resample of a multi-point row can't desync the ghost/emphasis.
  let pointDrag = $state<null | { sid: string; ci: number; h: PointHandle; pointerId: number }>(null);
  let ghost = $state<null | { sid: string; ci: number; x: number; y: number }>(null);
  let grabMoved = false;
  let hoverPoint = $state(false); // cursor hint: hovering a draggable point

  // Draggable row labels ("tags"): each drawn label's on-screen rect (populated
  // during the tag pass, most-recent last), and the active drag — a live pixel
  // offset shown immediately and committed to the store once on release.
  let labelHits: { rowId: number; x0: number; y0: number; x1: number; y1: number }[] = [];
  let labelDrag = $state<null | {
    rowId: number;
    startDx: number;
    startDy: number;
    curDx: number;
    curDy: number;
    startX: number;
    startY: number;
    pointerId: number;
  }>(null);
  let hoverLabel = $state(false); // cursor hint: hovering a draggable label

  const cursorStyle = $derived(
    pointDrag || labelDrag || dragging ? "grabbing" : hoverPoint || hoverLabel ? "grab" : "crosshair",
  );

  function localXY(e: PointerEvent): { x: number; y: number } {
    const r = canvas!.getBoundingClientRect();
    return { x: e.clientX - r.left, y: e.clientY - r.top };
  }
  function grabbable(h: PointHandle): boolean {
    return h.x.kind !== "expr" || h.y.kind !== "expr";
  }
  // Locate a point by its stable coordIndex within a series (handles align with
  // xs/ys by position, and each carries its coordIndex).
  function seriesXY(sid: string, ci: number): { x: number; y: number } | null {
    const s = series.find((ss) => ss.id === sid);
    if (!s) return null;
    const k = handles[sid]?.findIndex((hh) => hh && hh.coordIndex === ci) ?? -1;
    if (k < 0) return null;
    const x = s.xs[k];
    const y = s.ys[k];
    return x == null || y == null ? null : { x, y };
  }
  function hitPoint(p: { x: number; y: number }): { sid: string; i: number; h: PointHandle } | null {
    let best: { sid: string; i: number; h: PointHandle } | null = null;
    let bestD = GRAB * GRAB;
    for (const s of series) {
      if (!s.visible || s.kind !== "points") continue;
      const hs = handles[s.id];
      if (!hs) continue;
      for (let i = 0; i < s.xs.length; i++) {
        const h = hs[i];
        if (!h || !grabbable(h)) continue;
        const x = s.xs[i];
        const y = s.ys[i];
        if (x == null || y == null) continue;
        const dx = xToPx(x, view, width) - p.x;
        const dy = yToPx(y, view, H) - p.y;
        const d = dx * dx + dy * dy;
        if (d <= bestD) {
          bestD = d;
          best = { sid: s.id, i, h };
        }
      }
    }
    return best;
  }
  // Commit or cancel an in-progress point drag; the ghost is retained until the
  // next `series` lands (anti-snap-back), cleared by the effect below.
  function finishPointDrag(cancel: boolean): void {
    if (!pointDrag) return;
    const d = pointDrag;
    pointDrag = null;
    if (!grabMoved) {
      ghost = null;
      return;
    }
    if (cancel) {
      ghost = null;
      onPointDragCancel?.();
    } else if (ghost) {
      onPointCommit?.(d.h.rowId, d.h.coordIndex, ghost.x, ghost.y);
    }
  }
  // Clear the retained ghost only when a fresh `series` actually lands (the
  // committed recompute) — NOT when pointDrag flips to null on release, or a
  // literal-point commit (no live series write) would snap back for ~90ms.
  // pointDrag is read untracked so the release transition doesn't fire this.
  $effect(() => {
    void series;
    if (!untrack(() => pointDrag)) ghost = null;
  });

  function onPointerDown(e: PointerEvent): void {
    canvas!.setPointerCapture(e.pointerId);
    const p = localXY(e);
    if (pointDrag || labelDrag) return; // ignore extra fingers mid-drag
    pointers.set(e.pointerId, p);
    if (pointers.size === 1) {
      const hit = hitPoint(p);
      if (hit) {
        const ci = hit.h.coordIndex;
        const base = seriesXY(hit.sid, ci);
        if (base) {
          pointDrag = { sid: hit.sid, ci, h: hit.h, pointerId: e.pointerId };
          ghost = { sid: hit.sid, ci, ...base };
          grabMoved = false;
          onPointDragStart?.();
          return; // do NOT arm pan
        }
      }
      // A row label under the cursor starts a label drag (grabs its offset).
      const labelRow = hitLabel(p);
      if (labelRow !== null) {
        const off = labelOffsets[String(labelRow)] ?? { dx: 0, dy: 0 };
        labelDrag = {
          rowId: labelRow,
          startDx: off.dx,
          startDy: off.dy,
          curDx: off.dx,
          curDy: off.dy,
          startX: p.x,
          startY: p.y,
          pointerId: e.pointerId,
        };
        return; // do NOT arm pan
      }
      dragging = true;
      lastX = p.x;
      lastY = p.y;
    } else if (pointers.size === 2) {
      const [a, b] = [...pointers.values()];
      pinchDist = Math.hypot(a.x - b.x, a.y - b.y);
    }
  }

  function onPointerMove(e: PointerEvent): void {
    // While dragging a point or label, ignore every other pointer's moves.
    if (pointDrag && e.pointerId !== pointDrag.pointerId) return;
    if (labelDrag && e.pointerId !== labelDrag.pointerId) return;
    const p = localXY(e);
    if (pointers.has(e.pointerId)) pointers.set(e.pointerId, p);
    cursor = { x: p.x, y: p.y };

    if (labelDrag) {
      labelDrag = {
        ...labelDrag,
        curDx: labelDrag.startDx + (p.x - labelDrag.startX),
        curDy: labelDrag.startDy + (p.y - labelDrag.startY),
      };
      return;
    }

    if (pointDrag) {
      const h = pointDrag.h;
      const base = seriesXY(pointDrag.sid, pointDrag.ci);
      // A free axis follows the cursor; a locked axis holds the series value.
      let wx = h.x.kind !== "expr" ? pxToX(p.x, view, width) : base?.x ?? pxToX(p.x, view, width);
      let wy = h.y.kind !== "expr" ? pxToY(p.y, view, H) : base?.y ?? pxToY(p.y, view, H);
      // Same variable on both axes → one degree of freedom: project onto y = x.
      if (h.x.kind === "var" && h.y.kind === "var" && h.x.name === h.y.name) {
        wx = wy = (wx + wy) / 2;
      }
      grabMoved = true;
      ghost = { sid: pointDrag.sid, ci: pointDrag.ci, x: wx, y: wy };
      onPointDrag?.(h.rowId, h.coordIndex, wx, wy);
      return;
    }

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
    if (!dragging) {
      hoverPoint = !!hitPoint(p); // grab-cursor hint when over a draggable point
      hoverLabel = !hoverPoint && hitLabel(p) !== null; // or over a draggable label
      return;
    }
    view = panned(view, p.x - lastX, p.y - lastY);
    lastX = p.x;
    lastY = p.y;
  }

  function onPointerUp(e: PointerEvent): void {
    pointers.delete(e.pointerId);
    // Only the pointer that started the drag can finish it — a stray second
    // finger lifting must not commit/cancel the gesture.
    if (pointDrag && pointDrag.pointerId === e.pointerId) finishPointDrag(e.type === "pointercancel");
    if (labelDrag && labelDrag.pointerId === e.pointerId) {
      // Commit the label's final offset once (a cancel keeps the live value).
      onLabelMove?.(labelDrag.rowId, labelDrag.curDx, labelDrag.curDy);
      labelDrag = null;
    }
    if (pointers.size < 2) pinchDist = 0;
    if (pointers.size === 1) {
      // Lifting one finger of a pinch: reseat the pan anchor to the remaining
      // pointer so the next single-finger move doesn't jump by the finger gap.
      const [p] = [...pointers.values()];
      lastX = p.x;
      lastY = p.y;
    }
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

  // Keyboard navigation (Desmos-style) when the graph paper is focused: arrows
  // pan, +/- zoom about the center, 0/Home reset. We only preventDefault for
  // keys we actually handle, so Tab still moves focus out of the graph and
  // browser shortcuts (Ctrl/Cmd/Alt combos) are left alone.
  function onKeyDown(e: KeyboardEvent): void {
    if (e.ctrlKey || e.metaKey || e.altKey) return;
    const next = viewForKey(view, e.key, width, H, { shift: e.shiftKey });
    if (!next) return; // leave unrecognised keys (Tab, etc.) to the browser
    view = next;
    e.preventDefault();
  }

  /** Rendered graph as a PNG blob (for download). Exposed to the parent. */
  export function exportBlob(): Promise<Blob | null> {
    return new Promise((resolve) => {
      if (!canvas) return resolve(null);
      canvas.toBlob((b) => resolve(b), "image/png");
    });
  }

  const readout = $derived.by(() => {
    if (!cursor || dragging || pointDrag || labelDrag) return null;
    return { x: pxToX(cursor.x, view, width), y: pxToY(cursor.y, view, H) };
  });
</script>

<div class="graph" bind:this={host} style:--gh={height > 0 ? `${height}px` : null}>
  <canvas
    bind:this={canvas}
    tabindex="0"
    style:cursor={cursorStyle}
    aria-label="Interactive graph — drag or arrow keys to pan, scroll or +/- to zoom, 0 to reset, drag points to move them"
    onpointerdown={onPointerDown}
    onpointermove={onPointerMove}
    onpointerup={onPointerUp}
    onpointercancel={onPointerUp}
    onpointerleave={() => {
      cursor = null;
      hoverPoint = false;
    }}
    onwheel={onWheel}
    onkeydown={onKeyDown}
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
      ({fmtCoord(readout.x)}, {fmtCoord(readout.y)})
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
  canvas:focus {
    outline: none;
  }
  canvas:focus-visible {
    outline: 2px solid var(--accent, #2563eb);
    outline-offset: -2px;
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
