<script lang="ts">
  import type { FieldResult } from "../engine/types";
  import type { Ok } from "../outcome";

  let {
    fx,
    fy,
    result,
  }: { fx: string; fy: string; result: Ok<FieldResult> } = $props();

  const SIZE = 360;
  const PAD = 28;

  // Longest arrow among finite samples, used to scale every arrow so the
  // longest just spans one grid cell (a standard normalized quiver).
  const geom = $derived.by(() => {
    const { x, y, u, v, n } = result;
    const xs = x.filter((f) => Number.isFinite(f));
    const ys = y.filter((f) => Number.isFinite(f));
    const xmin = Math.min(...xs);
    const xmax = Math.max(...xs);
    const ymin = Math.min(...ys);
    const ymax = Math.max(...ys);
    let maxMag = 0;
    for (let i = 0; i < u.length; i++) {
      const uu = u[i];
      const vv = v[i];
      if (uu !== null && vv !== null && Number.isFinite(uu) && Number.isFinite(vv)) {
        maxMag = Math.max(maxMag, Math.hypot(uu, vv));
      }
    }
    const cell = (SIZE - 2 * PAD) / Math.max(1, n - 1);
    const scale = maxMag > 0 ? (cell * 0.92) / maxMag : 0;
    const sx = (xv: number) =>
      PAD + ((xv - xmin) / (xmax - xmin || 1)) * (SIZE - 2 * PAD);
    const sy = (yv: number) =>
      SIZE - PAD - ((yv - ymin) / (ymax - ymin || 1)) * (SIZE - 2 * PAD);
    return { xmin, xmax, ymin, ymax, maxMag, scale, sx, sy };
  });

  const arrows = $derived.by(() => {
    const { x, y, u, v } = result;
    const { scale, sx, sy, maxMag } = geom;
    const out: {
      x1: number;
      y1: number;
      x2: number;
      y2: number;
      mag: number;
    }[] = [];
    for (let i = 0; i < x.length; i++) {
      const uu = u[i];
      const vv = v[i];
      if (uu === null || vv === null || !Number.isFinite(uu) || !Number.isFinite(vv))
        continue;
      const px = sx(x[i]);
      const py = sy(y[i]);
      // The chart y-axis points up, so flip the v component for screen space.
      out.push({
        x1: px,
        y1: py,
        x2: px + uu * scale,
        y2: py - vv * scale,
        mag: Math.hypot(uu, vv) / (maxMag || 1),
      });
    }
    return out;
  });

  // Blue (low) to red (high) magnitude ramp.
  function color(t: number): string {
    const h = (1 - t) * 220;
    return `hsl(${h}, 70%, 50%)`;
  }
</script>

<figure class="field">
  <figcaption>F = ({fx}, {fy})</figcaption>
  <svg viewBox="0 0 {SIZE} {SIZE}" role="img" aria-label="vector field">
    <defs>
      <marker
        id="vf-arrow"
        viewBox="0 0 10 10"
        refX="8"
        refY="5"
        markerWidth="6"
        markerHeight="6"
        orient="auto-start-reverse"
      >
        <path d="M 0 0 L 10 5 L 0 10 z" fill="context-stroke" />
      </marker>
    </defs>
    {#each arrows as a (a.x1 + "," + a.y1)}
      <line
        x1={a.x1}
        y1={a.y1}
        x2={a.x2}
        y2={a.y2}
        stroke={color(a.mag)}
        stroke-width="1.6"
        marker-end="url(#vf-arrow)"
      />
    {/each}
  </svg>
</figure>

<style>
  .field {
    margin: 0;
  }
  figcaption {
    font-size: 0.85rem;
    color: var(--text-dim);
    margin-bottom: 0.4rem;
    font-family: var(--mono, monospace);
  }
  svg {
    width: 100%;
    max-width: 360px;
    height: auto;
    background: var(--bg-inset, transparent);
    border: 1px solid var(--border);
    border-radius: var(--radius);
  }
</style>
