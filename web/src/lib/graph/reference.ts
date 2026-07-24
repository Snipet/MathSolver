// Grouped quick-reference for the grapher's expression list, shown in the
// sidebar. Data-only so it stays scannable and testable; GraphReference.svelte
// renders it as collapsible groups of code chips + one-line descriptions.

export interface GraphRefEntry {
  /** A snippet to insert as a new row when clicked (omit for a plain note). */
  code?: string;
  /** One-line explanation. */
  desc: string;
}

export interface GraphRefGroup {
  title: string;
  entries: GraphRefEntry[];
}

export const GRAPH_REFERENCE: GraphRefGroup[] = [
  {
    title: "Functions & values",
    entries: [
      { code: "f(x) = x^2", desc: "Define a function" },
      { code: "y = f(x) + 1", desc: "Use it in another row" },
      { code: "f'(x)", desc: "Its derivative" },
      { code: "f(3)", desc: "A value at a point" },
      { code: "g(f(x))", desc: "Compose functions" },
      { code: "f = x^2", desc: "A reusable named value" },
      { code: "diff(f)", desc: "Plot the derivative" },
      { code: "integral(f)", desc: "Plot the antiderivative" },
    ],
  },
  {
    title: "Sliders & animation",
    entries: [
      { desc: "An undefined variable (like a) becomes a slider — here and in the app." },
      { desc: "Hit ▶ on a slider to animate it back and forth." },
      { desc: "Set a step to snap the slider — e.g. step 1 counts terms one by one." },
      { code: "sum(x^k, k, 0, n)", desc: "Slider n animates the number of terms" },
    ],
  },
  {
    title: "Sums, products & piecewise",
    entries: [
      { code: "sum(x^k, k, 0, 5)", desc: "Plot a partial sum" },
      { code: "product(k, k, 1, n)", desc: "A running product" },
      { code: "{x < 0: -x, x}", desc: "Split a curve into cases (piecewise)" },
    ],
  },
  {
    title: "Area & domain",
    entries: [
      { code: "integral(f, a, b)", desc: "Shade the signed area — the exact ∫ is labelled" },
      { code: "{0 <= t <= 6pi}", desc: "Restrict a curve's domain (trailing clause)" },
    ],
  },
  {
    title: "Points, styling & labels",
    entries: [
      { code: "(a, b)", desc: "Drag the point to move it — its variables move everywhere" },
      { desc: "Click a row's colored dot to restyle: color, line style, weight." },
      { desc: "Add a label in the style popover, then drag the label to reposition it." },
      { desc: "Labels → set a graph title and name the x- and y-axes; they draw on the plot." },
    ],
  },
  {
    title: "Lists",
    entries: [
      { code: "L = [1, 2, 3]", desc: "A list; or a range [1...10]" },
      { code: "(L, L^2)", desc: "Plot as points — a scatter (scalars broadcast, lists zip)" },
      { code: "[k^2 for k = L]", desc: "Build a list with a comprehension" },
      { code: "mean(L)", desc: "Reduce a list — also total, min, max, median, stdev" },
      { code: "L[3]", desc: "Index (1-based); slice with L[2...4]" },
      { code: "y = [1, 2, 3]", desc: "Plot a list as lines (x = [ … ] for vertical)" },
      { code: "sort(L)", desc: "Transform — also unique, reverse, join(A, B)" },
    ],
  },
];
