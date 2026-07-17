// Svelte action rendering a LaTeX string into the node via KaTeX.
import katex from "katex";
import type { Action } from "svelte/action";

export interface KatexParams {
  latex: string;
  display?: boolean;
}

export const renderKatex: Action<HTMLElement, KatexParams> = (node, params) => {
  const render = (p: KatexParams) => {
    katex.render(p.latex, node, {
      throwOnError: false,
      displayMode: p.display ?? false,
    });
  };
  render(params);
  return {
    update: render,
    destroy() {
      node.textContent = "";
    },
  };
};
