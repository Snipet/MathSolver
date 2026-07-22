// Marching squares: extract the g(x,y) = level iso-contour from a scalar grid
// (row-major, y outer) sampled over [x0,x1]×[y0,y1], as flat polyline segments
// with null separators (ready for a "line" DrawSeries). Pure + unit-tested.

export interface Contour {
  xs: (number | null)[];
  ys: (number | null)[];
}

export function marchingSquares(
  g: (number | null)[],
  nx: number,
  ny: number,
  x0: number,
  x1: number,
  y0: number,
  y1: number,
  level = 0,
): Contour {
  const xs: (number | null)[] = [];
  const ys: (number | null)[] = [];
  const dx = (x1 - x0) / (nx - 1);
  const dy = (y1 - y0) / (ny - 1);

  const val = (i: number, j: number): number => {
    const v = g[j * nx + i];
    return v === null || !Number.isFinite(v) ? NaN : v - level;
  };
  const wx = (i: number) => x0 + i * dx;
  const wy = (j: number) => y0 + j * dy;

  for (let j = 0; j < ny - 1; j++) {
    for (let i = 0; i < nx - 1; i++) {
      const v00 = val(i, j);
      const v10 = val(i + 1, j);
      const v11 = val(i + 1, j + 1);
      const v01 = val(i, j + 1);
      if (Number.isNaN(v00) || Number.isNaN(v10) || Number.isNaN(v11) || Number.isNaN(v01)) continue;

      // Interpolated crossing points on each edge that changes sign.
      const bottom = (): [number, number] => [wx(i) + (v00 / (v00 - v10)) * dx, wy(j)];
      const right = (): [number, number] => [wx(i + 1), wy(j) + (v10 / (v10 - v11)) * dy];
      const top = (): [number, number] => [wx(i) + (v01 / (v01 - v11)) * dx, wy(j + 1)];
      const left = (): [number, number] => [wx(i), wy(j) + (v00 / (v00 - v01)) * dy];

      const cross: [number, number][] = [];
      if (v00 > 0 !== v10 > 0) cross.push(bottom());
      if (v10 > 0 !== v11 > 0) cross.push(right());
      if (v01 > 0 !== v11 > 0) cross.push(top());
      if (v00 > 0 !== v01 > 0) cross.push(left());

      const seg = (a: [number, number], b: [number, number]) => {
        xs.push(a[0], b[0], null);
        ys.push(a[1], b[1], null);
      };
      if (cross.length === 2) {
        seg(cross[0], cross[1]);
      } else if (cross.length === 4) {
        // Saddle — cross = [bottom, right, top, left]. Disambiguate by the
        // center average so the branches connect consistently.
        const center = (v00 + v10 + v11 + v01) / 4;
        if (center > 0) {
          seg(cross[0], cross[1]);
          seg(cross[2], cross[3]);
        } else {
          seg(cross[0], cross[3]);
          seg(cross[1], cross[2]);
        }
      }
    }
  }
  return { xs, ys };
}

/** Sign mask of an inequality over the grid, for region shading. */
export function inequalityMask(
  g: (number | null)[],
  op: "<" | ">" | "<=" | ">=",
): Uint8Array {
  const mask = new Uint8Array(g.length);
  for (let k = 0; k < g.length; k++) {
    const v = g[k];
    if (v === null || !Number.isFinite(v)) continue;
    const inside =
      op === "<" ? v < 0 : op === ">" ? v > 0 : op === "<=" ? v <= 0 : v >= 0;
    if (inside) mask[k] = 1;
  }
  return mask;
}
