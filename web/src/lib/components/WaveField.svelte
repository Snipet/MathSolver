<script lang="ts">
  // Interactive 2-D wave field: a real-time FDTD simulation (WaveSim) painted
  // to a canvas, with energy introduced by mouse/touch gestures — click to
  // pluck, drag to launch a directional wavefront. Used both inline as a
  // console "wave" cell (compact) and as the full Workbench "Wave" tab.
  import { untrack } from "svelte";
  import { WaveSim, type Boundary } from "../wave/sim";

  interface Props {
    /** Console cells pass compact=true for a smaller field + condensed controls. */
    compact?: boolean;
    /** Initial grid columns (rows follow the display aspect ratio). */
    columns?: number;
    speed?: number;
    damping?: number;
    boundary?: Boundary;
  }

  let {
    compact = false,
    columns = 180,
    speed = 0.5,
    damping = 0.06,
    boundary = "fixed",
  }: Props = $props();

  function clamp01(v: number): number {
    return v < 0 ? 0 : v > 1 ? 1 : v;
  }
  function clampCols(v: number): number {
    return Math.max(48, Math.min(320, Math.round(v)));
  }

  // --- controls (reactive) ---------------------------------------------------
  // The props are one-time seeds for these editable controls; untrack makes
  // that intent explicit (the component owns the values after mount).
  let running = $state(true);
  let speedV = $state(untrack(() => clamp01(speed)));
  let dampingV = $state(untrack(() => clamp01(damping)));
  let boundaryV = $state<Boundary>(untrack(() => boundary));
  let robinV = $state(0.5); // Robin edge stiffness k (∂u/∂n + k·u = 0)
  let cols = $state(untrack(() => clampCols(columns)));
  let brush = $state(untrack(() => (compact ? 3 : 4))); // brush radius in cells
  let strength = $state(1);
  let colormap = $state<"coolwarm" | "fire" | "violet">("coolwarm");
  let energy = $state(0);
  let touched = $state(false);

  const HEIGHT = untrack(() => (compact ? 260 : 460));
  const STEPS_PER_FRAME = 2; // temporal oversampling for smoother propagation

  // --- sizing ----------------------------------------------------------------
  let host: HTMLDivElement | undefined = $state();
  let canvas: HTMLCanvasElement | undefined = $state();
  let width = $state(0);
  let visible = $state(true);

  $effect(() => {
    const el = host;
    if (!el) return;
    const ro = new ResizeObserver((entries) => {
      width = Math.max(0, Math.floor(entries[0].contentRect.width));
    });
    ro.observe(el);
    const io = new IntersectionObserver(
      (entries) => (visible = entries[0].isIntersecting),
      { threshold: 0 },
    );
    io.observe(el);
    return () => {
      ro.disconnect();
      io.disconnect();
    };
  });

  // Grid dimensions: cols wide, rows to match the display aspect ratio.
  const gridDims = $derived.by(() => {
    const w = width || 640;
    const aspect = HEIGHT / w;
    const nx = cols;
    const ny = Math.max(24, Math.round(nx * aspect));
    return { nx, ny };
  });

  // --- simulation ------------------------------------------------------------
  // Rebuilt only when the integer grid changes (not on every pixel resize).
  // The effect must depend ONLY on gridDims: reading `sim` (to resample) and
  // the control seeds, and writing `sim`, are untracked — otherwise the write
  // re-triggers the effect (an infinite reactive loop).
  let sim = $state<WaveSim | null>(null);
  $effect(() => {
    const { nx, ny } = gridDims;
    untrack(() => {
      const next = new WaveSim(nx, ny, {
        speed: speedV,
        damping: dampingV,
        boundary: boundaryV,
        robin: robinV,
      });
      // Preserve the wave when only the resolution changed: resample the old
      // field into the new grid so a running simulation isn't wiped.
      if (sim) resample(sim, next);
      sim = next;
    });
  });

  // Push control changes into the live sim without rebuilding it.
  $effect(() => {
    const s = sim;
    if (!s) return;
    s.speed = speedV;
    s.damping = dampingV;
    s.setBoundary(boundaryV);
    s.robin = robinV;
  });

  function resample(from: WaveSim, to: WaveSim): void {
    for (let j = 0; j < to.ny; j++) {
      const fj = Math.min(from.ny - 1, Math.round((j / (to.ny - 1)) * (from.ny - 1)));
      for (let i = 0; i < to.nx; i++) {
        const fi = Math.min(from.nx - 1, Math.round((i / (to.nx - 1)) * (from.nx - 1)));
        const k = to.idx(i, j);
        const fk = from.idx(fi, fj);
        to.cur[k] = from.cur[fk];
        to.prev[k] = from.prev[fk];
      }
    }
  }

  // --- color lookup tables ---------------------------------------------------
  // 256-entry diverging maps over t ∈ [-1, 1]; index = round((t+1)/2·255).
  function buildLut(
    stops: [number, [number, number, number]][],
  ): Uint8ClampedArray {
    const lut = new Uint8ClampedArray(256 * 3);
    for (let m = 0; m < 256; m++) {
      const t = m / 255;
      let a = stops[0];
      let b = stops[stops.length - 1];
      for (let s = 0; s < stops.length - 1; s++) {
        if (t >= stops[s][0] && t <= stops[s + 1][0]) {
          a = stops[s];
          b = stops[s + 1];
          break;
        }
      }
      const span = b[0] - a[0] || 1;
      const f = (t - a[0]) / span;
      lut[m * 3] = a[1][0] + (b[1][0] - a[1][0]) * f;
      lut[m * 3 + 1] = a[1][1] + (b[1][1] - a[1][1]) * f;
      lut[m * 3 + 2] = a[1][2] + (b[1][2] - a[1][2]) * f;
    }
    return lut;
  }

  // Diverging palettes: rest (0) maps to the panel background so the membrane
  // blends into the UI in either theme, and only the wavefronts glow.
  type RGB = [number, number, number];
  const PALETTES: Record<string, { neg: RGB; pos: RGB }> = {
    coolwarm: { neg: [59, 110, 220], pos: [225, 70, 55] },
    fire: { neg: [30, 170, 190], pos: [245, 150, 40] },
    violet: { neg: [40, 205, 225], pos: [215, 70, 200] },
  };

  /** Read a CSS color (hex or rgb()) into an RGB triple. */
  function readRgb(el: Element, name: string, fb: RGB): RGB {
    const v = getComputedStyle(el).getPropertyValue(name).trim();
    const hex = /^#([0-9a-f]{3}|[0-9a-f]{6})$/i.exec(v);
    if (hex) {
      let h = hex[1];
      if (h.length === 3) h = h[0] + h[0] + h[1] + h[1] + h[2] + h[2];
      return [
        parseInt(h.slice(0, 2), 16),
        parseInt(h.slice(2, 4), 16),
        parseInt(h.slice(4, 6), 16),
      ];
    }
    const rgb = /rgba?\(([^)]+)\)/i.exec(v);
    if (rgb) {
      const p = rgb[1].split(/[,\s/]+/).map(Number);
      if (p.length >= 3) return [p[0], p[1], p[2]];
    }
    return fb;
  }

  let lut: Uint8ClampedArray = new Uint8ClampedArray(256 * 3);
  let lutKey = "";
  function ensureLut(name: string, bg: RGB): void {
    const key = `${name}|${bg.join(",")}`;
    if (key === lutKey) return;
    lutKey = key;
    const p = PALETTES[name] ?? PALETTES.coolwarm;
    lut = buildLut([
      [0, p.neg],
      [0.5, bg],
      [1, p.pos],
    ]);
  }

  // --- render buffer ---------------------------------------------------------
  let field: HTMLCanvasElement | undefined; // offscreen nx×ny buffer
  let fieldCtx: CanvasRenderingContext2D | null = null;
  let imgData: ImageData | null = null;
  let scale = 0.05; // smoothed amplitude normalizer (avoids flicker)

  function paint(): void {
    const s = sim;
    const disp = canvas;
    if (!s || !disp) return;

    // (Re)allocate the offscreen field buffer when the grid changes.
    if (!field || field.width !== s.nx || field.height !== s.ny) {
      field = document.createElement("canvas");
      field.width = s.nx;
      field.height = s.ny;
      fieldCtx = field.getContext("2d");
      imgData = fieldCtx ? fieldCtx.createImageData(s.nx, s.ny) : null;
    }
    if (!fieldCtx || !imgData) return;

    // Ease the color scale toward the current peak so faint ripples stay
    // visible while a big pluck doesn't wash everything out.
    const peak = s.maxAbs();
    const target = Math.max(peak, 0.02);
    scale += (target - scale) * 0.08;
    const inv = scale > 1e-6 ? 1 / scale : 0;

    // Theme-aware LUT: rest → panel background, so the field blends into the
    // page in light and dark; rebuilt only when the palette or bg changes.
    ensureLut(colormap, readRgb(disp, "--bg-panel", [24, 27, 33]));
    const cur = s.cur;
    const px = imgData.data;
    for (let k = 0; k < cur.length; k++) {
      let t = cur[k] * inv; // ~[-1, 1]
      t = t < -1 ? -1 : t > 1 ? 1 : t;
      // Signed-gamma boost: emphasize small displacements so ripples read
      // against the near-background rest state.
      const ts = t < 0 ? -((-t) ** 0.7) : t ** 0.7;
      const m = ((ts + 1) * 0.5 * 255) | 0;
      const o = k * 4;
      const l = m * 3;
      px[o] = lut[l];
      px[o + 1] = lut[l + 1];
      px[o + 2] = lut[l + 2];
      px[o + 3] = 255;
    }
    fieldCtx.putImageData(imgData, 0, 0);

    // Blit the low-res field up to the display canvas (smoothed).
    const dpr = window.devicePixelRatio || 1;
    const cssW = width || disp.clientWidth || s.nx;
    if (disp.width !== Math.round(cssW * dpr) || disp.height !== Math.round(HEIGHT * dpr)) {
      disp.width = Math.round(cssW * dpr);
      disp.height = Math.round(HEIGHT * dpr);
      disp.style.width = cssW + "px";
      disp.style.height = HEIGHT + "px";
    }
    const ctx = disp.getContext("2d");
    if (!ctx) return;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.imageSmoothingEnabled = true;
    ctx.imageSmoothingQuality = "high";
    ctx.drawImage(field, 0, 0, s.nx, s.ny, 0, 0, cssW, HEIGHT);
  }

  // --- animation loop --------------------------------------------------------
  // One loop for the component's lifetime; restarts only when the canvas
  // remounts. running/visible/steps are read live inside the callback, so
  // toggling them never re-runs this effect (no rAF leak / double loop).
  $effect(() => {
    const disp = canvas;
    if (!disp) return;
    let raf = 0;
    let frame = 0;
    const loop = () => {
      raf = requestAnimationFrame(loop);
      const s = sim;
      if (!s || !visible) return;
      if (running) s.step(STEPS_PER_FRAME);
      paint();
      if ((frame++ & 7) === 0) energy = s.energy();
    };
    raf = requestAnimationFrame(loop);
    return () => cancelAnimationFrame(raf);
  });

  // --- pointer gestures ------------------------------------------------------
  let dragging = false;
  let lastX = 0;
  let lastY = 0;

  function toGrid(e: PointerEvent): { gx: number; gy: number } {
    const s = sim!;
    const rect = canvas!.getBoundingClientRect();
    const fx = (e.clientX - rect.left) / rect.width;
    const fy = (e.clientY - rect.top) / rect.height;
    return {
      gx: Math.max(0, Math.min(s.nx - 1, fx * s.nx)),
      gy: Math.max(0, Math.min(s.ny - 1, fy * s.ny)),
    };
  }

  function onPointerDown(e: PointerEvent): void {
    if (!sim) return;
    touched = true;
    canvas!.setPointerCapture(e.pointerId);
    dragging = true;
    const { gx, gy } = toGrid(e);
    lastX = gx;
    lastY = gy;
    sim.poke(gx, gy, brush, 1.1 * strength);
  }

  function onPointerMove(e: PointerEvent): void {
    if (!dragging || !sim) return;
    const { gx, gy } = toGrid(e);
    const dx = gx - lastX;
    const dy = gy - lastY;
    const dist = Math.hypot(dx, dy);
    if (dist < 0.5) return; // ignore jitter
    // A moving source: deposit a small pluck scaled by drag speed, plus a
    // velocity dipole aligned with the drag so energy launches that way.
    const spd = Math.min(dist, 12);
    sim.poke(gx, gy, brush, 0.18 * strength * (spd / 6));
    sim.impartVelocity(gx, gy, brush, 0.5 * strength * (spd / 6), dx, dy);
    lastX = gx;
    lastY = gy;
  }

  function onPointerUp(e: PointerEvent): void {
    dragging = false;
    if (canvas?.hasPointerCapture(e.pointerId)) canvas.releasePointerCapture(e.pointerId);
  }

  // --- actions ---------------------------------------------------------------
  function reset(): void {
    sim?.reset();
    scale = 0.05;
    energy = 0;
  }
  function togglePlay(): void {
    running = !running;
  }
  const BOUNDARIES: { id: Boundary; label: string; title: string }[] = [
    { id: "fixed", label: "Fixed", title: "Clamped edge (drum) — reflects with inversion" },
    { id: "free", label: "Free", title: "Free edge — reflects without inversion" },
    { id: "robin", label: "Robin", title: "Impedance edge ∂u/∂n + k·u = 0 — springy, tunable between free and fixed" },
    { id: "absorbing", label: "Open", title: "Absorbing edge — waves leave the window" },
  ];
</script>

<div class="wave" class:compact bind:this={host}>
  <div class="controls" role="group" aria-label="Wave field controls">
    <button class="ctl-btn primary" onclick={togglePlay} aria-pressed={running}>
      {running ? "❚❚ Pause" : "► Play"}
    </button>
    <button class="ctl-btn" onclick={reset}>Reset</button>

    <label class="ctl">
      <span>Speed</span>
      <input type="range" min="0.05" max="1" step="0.01" bind:value={speedV} />
    </label>
    <label class="ctl">
      <span>Damping</span>
      <input type="range" min="0" max="0.6" step="0.01" bind:value={dampingV} />
    </label>

    <div class="seg" role="group" aria-label="Boundary">
      {#each BOUNDARIES as b (b.id)}
        <button
          class="seg-btn"
          class:active={boundaryV === b.id}
          title={b.title}
          onclick={() => (boundaryV = b.id)}
        >
          {b.label}
        </button>
      {/each}
    </div>

    {#if boundaryV === "robin"}
      <label class="ctl" title="Edge stiffness k: 0 ≈ free, large ≈ fixed">
        <span>Stiffness</span>
        <input type="range" min="0" max="8" step="0.1" bind:value={robinV} />
      </label>
    {/if}

    {#if !compact}
      <label class="ctl">
        <span>Brush</span>
        <input type="range" min="1" max="12" step="1" bind:value={brush} />
      </label>
      <label class="ctl">
        <span>Strength</span>
        <input type="range" min="0.2" max="2.5" step="0.1" bind:value={strength} />
      </label>
      <label class="ctl">
        <span>Detail</span>
        <input type="range" min="64" max="300" step="4" bind:value={cols} />
      </label>
      <label class="ctl select">
        <span>Color</span>
        <select bind:value={colormap}>
          <option value="coolwarm">coolwarm</option>
          <option value="fire">fire</option>
          <option value="violet">violet</option>
        </select>
      </label>
    {/if}

    <span class="energy" title="Total field energy (kinetic + potential, arb. units)">
      E {Math.max(0, Math.round(energy * 1000))}
    </span>
  </div>

  <div class="stage">
    <canvas
      bind:this={canvas}
      aria-label="Interactive 2-D wave field — drag to add energy"
      onpointerdown={onPointerDown}
      onpointermove={onPointerMove}
      onpointerup={onPointerUp}
      onpointercancel={onPointerUp}
    ></canvas>
    {#if !touched}
      <p class="hint">Click to pluck · drag to launch a wavefront</p>
    {/if}
  </div>
</div>

<style>
  .wave {
    display: flex;
    flex-direction: column;
    gap: 0.55rem;
    min-width: 0;
  }
  .controls {
    display: flex;
    align-items: center;
    gap: 0.55rem 0.75rem;
    flex-wrap: wrap;
  }
  .ctl-btn {
    font: inherit;
    font-size: 0.82rem;
    font-weight: 600;
    color: var(--fg-muted);
    background: var(--bg-panel);
    border: 1px solid var(--border);
    border-radius: 999px;
    padding: 0.25rem 0.8rem;
    cursor: pointer;
    white-space: nowrap;
  }
  .ctl-btn:hover {
    color: var(--accent);
    border-color: var(--accent);
  }
  .ctl-btn.primary {
    color: var(--accent-fg, #fff);
    background: var(--accent);
    border-color: var(--accent);
  }
  .ctl {
    display: inline-flex;
    align-items: center;
    gap: 0.4rem;
    font-size: 0.78rem;
    color: var(--fg-muted);
  }
  .ctl input[type="range"] {
    width: 6.5rem;
    accent-color: var(--accent);
  }
  .ctl.select select {
    font: inherit;
    font-size: 0.78rem;
    color: var(--fg);
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) / 2);
    padding: 0.1rem 0.35rem;
  }
  .seg {
    display: inline-flex;
    border: 1px solid var(--border);
    border-radius: 999px;
    overflow: hidden;
  }
  .seg-btn {
    font: inherit;
    font-size: 0.76rem;
    color: var(--fg-muted);
    background: transparent;
    border: none;
    padding: 0.22rem 0.6rem;
    cursor: pointer;
  }
  .seg-btn:hover {
    color: var(--accent);
  }
  .seg-btn.active {
    color: var(--accent-fg, #fff);
    background: var(--accent);
  }
  .energy {
    font-family: var(--font-mono);
    font-size: 0.74rem;
    color: var(--fg-muted);
    margin-left: auto;
    white-space: nowrap;
  }
  .stage {
    position: relative;
    border: 1px solid var(--border);
    border-radius: var(--radius);
    overflow: hidden;
    background: var(--bg-panel);
    line-height: 0;
  }
  canvas {
    display: block;
    width: 100%;
    max-width: 100%;
    touch-action: none;
    cursor: crosshair;
  }
  .hint {
    position: absolute;
    left: 50%;
    bottom: 0.7rem;
    transform: translateX(-50%);
    margin: 0;
    padding: 0.2rem 0.7rem;
    font-size: 0.78rem;
    color: var(--fg-muted);
    background: color-mix(in srgb, var(--bg-panel) 78%, transparent);
    border: 1px solid var(--border);
    border-radius: 999px;
    pointer-events: none;
    white-space: nowrap;
  }
</style>
