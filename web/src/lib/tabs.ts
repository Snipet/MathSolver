// Operation tab definitions: labels, input placeholders, example chips.

export type TabId =
  | "simplify"
  | "expand"
  | "factor"
  | "solve"
  | "derivative"
  | "integral"
  | "evaluate"
  | "plot"
  | "wave";

export interface TabDef {
  id: TabId;
  label: string;
  placeholder: string;
  examples: string[];
}

export const TABS: TabDef[] = [
  {
    id: "simplify",
    label: "Simplify",
    placeholder: "Enter an expression, e.g. 2x + 3x",
    examples: ["2x + 3x", "sin(pi/6) + cos(0)", "sin(30°)", "|3 - π|"],
  },
  {
    id: "expand",
    label: "Expand",
    placeholder: "Enter an expression to expand, e.g. (x+1)^3",
    examples: ["(x+1)^3"],
  },
  {
    id: "factor",
    label: "Factor",
    placeholder: "Enter a polynomial, e.g. x^2 - 5x + 6",
    examples: ["x^2 - 5x + 6"],
  },
  {
    id: "solve",
    label: "Solve",
    placeholder: "Equation like x^2 = 4, or a system: x + y = 3; x - y = 1",
    examples: ["x^2 = 4", "sin(x) = 1/2", "cos(x) = x", "x + y = 3; x - y = 1"],
  },
  {
    id: "derivative",
    label: "Derivative",
    placeholder: "Expression to differentiate, e.g. sin(x^2)",
    examples: ["sin(x^2)", "x^x"],
  },
  {
    id: "integral",
    label: "Integral",
    placeholder: "Expression to integrate, e.g. x*sin(x)",
    examples: ["x*sin(x)", "1/(x^2-1)", "e^(x^2)"],
  },
  {
    id: "evaluate",
    label: "Evaluate",
    placeholder: "Expression to evaluate numerically, e.g. pi^2/6",
    examples: ["pi^2/6", "2e-3 + 1e2"],
  },
  {
    // A full-canvas interactive tool: it has no expression input, so App.svelte
    // special-cases it (like nothing else) — placeholder/examples stay empty.
    id: "wave",
    label: "Wave",
    placeholder: "",
    examples: [],
  },
];
