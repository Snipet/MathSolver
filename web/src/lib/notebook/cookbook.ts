// Console cookbook: curated, worked recipes that show how to get the most out
// of the console's commands — combinations, workflows, and gotchas — as
// opposed to the flat per-command reference (reference.ts). Every step is a
// real console line; clicking one drops it into the prompt (via consoleUi).
//
// The content is grouped into sections; each recipe carries a short "why" and
// one or more ordered steps. Authored and adversarially verified against the
// built CLI and the plugin sources. DO NOT hand-edit large swaths — this data
// was generated; small corrections are fine.

export interface CookbookStep {
  /** One console line, exactly as typed (no quotes). */
  code: string;
  /** Optional short note on what this line demonstrates. */
  note?: string;
}

export interface CookbookRecipe {
  title: string;
  /** What it demonstrates, when to reach for it, any gotcha. */
  why: string;
  steps: CookbookStep[];
}

export interface CookbookSection {
  id: string;
  title: string;
  blurb: string;
  recipes: CookbookRecipe[];
}

// Populated from the authoring workflow output. Sections run fundamentals →
// advanced so a reader can work straight down.
export const COOKBOOK: CookbookSection[] = [
  {
    "id": "getting-started",
    "title": "Getting started with the Console",
    "blurb": "The Console is a line-by-line REPL: type one line, press Enter, get a numbered In/Out cell. Before you learn any particular kind of math, learn the console itself — the two ways a line is read, the live \"as parsed\" preview and its error caret, Tab completion, the argument hints and \"try:\" chips, the symbol palette, session variables, and where to find every command. These mechanics make everything else faster.",
    "recipes": [
      {
        "title": "Two ways in: a bare line vs. a command",
        "why": "The console does something sensible with a bare line: a bare expression is simplified, and a bare equation in one variable is solved. A leading verb instead runs one specific operation on comma-separated arguments — never quoted. Systems of equations use the solve verb with semicolons between the equations, then the variables. Gotcha: a bare equation must name a single variable; give it two or more and the console will not guess — it tells you to write solve <eq>, <var>.",
        "steps": [
          {
            "code": "2x + 3x",
            "note": "bare expression is simplified to 5*x (implicit multiplication is fine)"
          },
          {
            "code": "(x+1)(x-2)",
            "note": "still just simplified — adjacency means multiply, giving (x - 2)*(x + 1)"
          },
          {
            "code": "x^2 = 4",
            "note": "a bare equation in one variable is solved -> x = -2, x = 2"
          },
          {
            "code": "factor x^2 - 5x + 6",
            "note": "a verb + comma-args runs one named operation -> (x - 3)*(x - 2)"
          },
          {
            "code": "solve x + y = 3; x - y = 1, x, y",
            "note": "semicolons build a system; solve takes the equations then the variables -> x = 2, y = 1"
          }
        ]
      },
      {
        "title": "Watch the \"as parsed\" preview before you run",
        "why": "As you type, a debounced preview above the prompt typesets exactly what the line will compute — d/dx(...) for diff, an integral sign for integrate, a sum sign for sum — with a small note naming the operation and its options. It is the quickest way to confirm the parser read your precedence and implicit multiplication the way you meant, before you commit. Gotcha: plugin calls and session words (help, dsp.*) show no math preview — their one-line usage hint stands in instead.",
        "steps": [
          {
            "code": "diff sin(x^2), x",
            "note": "preview renders d/dx( sin(x^2) ) before you press Enter"
          },
          {
            "code": "integrate e^(-x^2), x, 0, 1",
            "note": "preview shows the definite integral from 0 to 1 dx"
          },
          {
            "code": "series sin(x), x, 0, 5",
            "note": "same math, but the note reads \"Taylor about 0, order 5\""
          },
          {
            "code": "solve x^2 = 4, x",
            "note": "preview shows x^2 = 4 with the note \"solve for x\""
          }
        ]
      },
      {
        "title": "Catch typos with the error caret",
        "why": "When a line cannot parse, the preview turns red and draws a caret under the exact fragment the engine choked on, so you fix it in place instead of reading an error after you run. The usual triggers are an unbalanced parenthesis or an operator with nothing after it. Treat a red preview as a spell-checker for math.",
        "steps": [
          {
            "code": "(x+1",
            "note": "red preview: \"missing ')'\", caret under the open paren"
          },
          {
            "code": "factor x^2 - 5x +",
            "note": "\"unexpected end of input\", caret at the trailing +"
          },
          {
            "code": "(x+1)(x-2)",
            "note": "balance the paren and the preview flips back to typeset math"
          }
        ]
      },
      {
        "title": "Tab-complete command names",
        "why": "Type the first letters of any command and a popup lists every verb that starts with them; Tab (or a click) accepts the highlighted one and adds a trailing space, up/down arrows move the selection, Esc dismisses, and Enter simply runs the line as typed. It is how you find a verb without lifting your hands from the keyboard. Gotcha: the popup only shows while you are still typing the first word — once you type a space it gives way to the argument hints.",
        "steps": [
          {
            "code": "int",
            "note": "popup offers integrate; Tab accepts and the prompt becomes \"integrate \""
          },
          {
            "code": "s",
            "note": "a short prefix lists many — simplify, solve, series, sum, subs, seq, stirling — use up/down to pick"
          },
          {
            "code": "integrate e^(-x^2), x, 0, 1",
            "note": "finish the arguments after the completed verb and press Enter"
          }
        ]
      },
      {
        "title": "Let the argument hints spell out the syntax",
        "why": "Once a command word is complete (you have typed a space), a usage synopsis appears and a ghost hint shows the next argument slot inline at the caret, advancing after each comma. You never have to memorize argument order — diff wants <expr>[, <var>]; integrate wants <expr>[, <var>[, <lo>, <hi>]] for a definite integral. Reach for this any time you are unsure what comes next.",
        "steps": [
          {
            "code": "diff ",
            "note": "trailing space: usage line shows diff <expr>[, <var>] and the ghost hints <expr>"
          },
          {
            "code": "diff x^3, ",
            "note": "after the comma the ghost hint advances to <var>"
          },
          {
            "code": "integrate x^2, x, 0, 1",
            "note": "the hints walk you through the var, then lo and hi -> 1/3"
          }
        ]
      },
      {
        "title": "Follow the \"try:\" chips on a bare expression",
        "why": "Type a bare expression with no leading verb and a quiet \"try:\" row appears offering the operations that fit its shape — factor, solve = 0, expand, diff, integrate for a polynomial; a series chip for transcendental input; a decimal eval for a pure number. Click a chip to run it, or press Tab to fill in the first one. It turns \"what can I even do with this?\" into a single keystroke. Gotcha: the chips hide while the completion popup is open, since you are already naming a verb there.",
        "steps": [
          {
            "code": "x^2 - 5x + 6",
            "note": "polynomial -> chips: factor, solve = 0, expand, diff, integrate"
          },
          {
            "code": "sin(x)",
            "note": "transcendental drops factor/expand and adds series -> solve = 0, diff, integrate, series"
          },
          {
            "code": "sqrt(2) + 1",
            "note": "a pure number (Enter keeps it exact as sqrt(2) + 1); the eval chip gives the decimal 2.414..."
          }
        ]
      },
      {
        "title": "Insert symbols from the palette",
        "why": "The palette below the prompt drops parser-ready Unicode at the caret — pi, a square-root sign, absolute-value bars, a squared exponent, the times and divide signs, the degree sign for degrees, and the := assignment operator — and some wrap the current selection (select x, click the bars, get |x|). It is handy on a keyboard without easy symbol access, and the parser treats each symbol exactly like its ASCII spelling. Clicking a chip never steals focus from the prompt.",
        "steps": [
          {
            "code": "√(16) + π",
            "note": "the root sign and pi insert directly; simplifies to pi + 4"
          },
          {
            "code": "sin(30°)",
            "note": "the degree sign means degrees -> 1/2"
          },
          {
            "code": "3 × 4 ÷ 2",
            "note": "times and divide are just * and / -> 6"
          }
        ]
      },
      {
        "title": "Name things with := and reuse them",
        "why": "name := value stores a session binding that applies to every later line — not earlier ones, so order matters — letting you name a constant, or a whole equation, once and reference it downstream. vars lists what is bound, unset drops one, and clear drops them all. This lazy, top-to-bottom substitution is the backbone of any multi-step console session.",
        "steps": [
          {
            "code": "a := 2",
            "note": "bind a to 2; the preview notes \"assignment\""
          },
          {
            "code": "a*x^2 + 1",
            "note": "a later line sees a = 2 -> 2*x^2 + 1"
          },
          {
            "code": "vars",
            "note": "list the current bindings -> a := 2"
          },
          {
            "code": "unset a",
            "note": "remove just a — clear would remove every binding"
          }
        ]
      },
      {
        "title": "History recall, In/Out cells, and clearing",
        "why": "Every run becomes a numbered In/Out cell you can scroll back through and re-run or edit from its controls. At the prompt, up and down arrows walk your input history like a shell, so you can pull back a long line, tweak one number, and run it again instead of retyping. Gotcha: Ctrl+L (or the Clear button) wipes the transcript — that is different from the clear command, which clears variables, not the screen.",
        "steps": [
          {
            "code": "integrate x*sin(x), x",
            "note": "runs and becomes cell In[1]/Out[1] -> -x*cos(x) + sin(x) + C"
          },
          {
            "code": "sum k^2, k, 1, n",
            "note": "another cell -> n^3/3 + n^2/2 + n/6; press up twice to step back to either line and edit it"
          },
          {
            "code": "integrate x*cos(x), x",
            "note": "recall the first line with up, change sin to cos, Enter to re-run -> x*sin(x) + cos(x) + C"
          }
        ]
      },
      {
        "title": "Find any command: help, the Commands panel, plugins",
        "why": "help prints the whole console grammar as one cell; the Commands panel (the sidebar on desktop, the collapsible \"Commands\" section on mobile) lists every verb with a runnable example you can click to insert; and plugins lists the extra namespaced commands compiled in, which you call as <namespace>.<command>. Start here whenever you do not yet know the verb for what you want. Plugin commands take comma-separated args just like built-ins, but their leading token has a dot.",
        "steps": [
          {
            "code": "help",
            "note": "the full grammar, printed as one info cell"
          },
          {
            "code": "plugins",
            "note": "lists each compiled-in plugin and its commands' usage"
          },
          {
            "code": "dsp.butter lowpass, 4, 1000, 48000",
            "note": "a namespaced plugin call: ns.command then comma-args (matches the dsp plugin's own example)"
          }
        ]
      }
    ]
  },
  {
    "id": "algebra",
    "title": "Algebra & rational expressions",
    "blurb": "Reshape polynomials and rational expressions from the console: tidy them up, multiply out, factor, group by a variable, and combine or split fractions. The headline in this release is the combine to reduce pair — together puts a sum of fractions over one denominator, and the new cancel divides out the GCD — plus subs for symbolic substitution and latex to typeset any result. A rule of thumb worth learning early: simplify tidies and collects like terms but never multiplies products out or cancels a rational's GCD — reach for expand and cancel for those.",
    "recipes": [
      {
        "title": "Everyday cleanup: simplify and expand",
        "why": "simplify collects like terms and applies identities (it even knows sin^2+cos^2=1), but it deliberately leaves products unmultiplied. expand is the opposite move: it multiplies everything out into a flat polynomial. Reach for simplify to tidy, expand to open products up. Gotcha: simplify will NOT expand (x+1)^3 for you and will NOT cancel a rational — those are separate verbs.",
        "steps": [
          {
            "code": "simplify 2x + 3x - x",
            "note": "like terms collapse: 4*x"
          },
          {
            "code": "simplify sin(x)^2 + cos(x)^2",
            "note": "a trig identity fires: 1"
          },
          {
            "code": "expand (x+1)^3",
            "note": "multiplied out: x^3 + 3*x^2 + 3*x + 1"
          },
          {
            "code": "expand (a+b)^2 + (a-b)^2",
            "note": "cross terms cancel: 2*a^2 + 2*b^2"
          }
        ]
      },
      {
        "title": "Factor polynomials",
        "why": "factor pulls out the greatest common factor and splits quadratics into linear pieces, including difference-of-squares and perfect-square forms. It is strongest on a single variable: GCF extraction plus quadratic factoring. Gotcha: it will not fully split an arbitrary cubic or a two-variable difference of squares (both come back unchanged), so treat it as a quadratic/GCF tool, not a universal factorizer.",
        "steps": [
          {
            "code": "factor x^2 - 5x + 6",
            "note": "(x - 3)*(x - 2)"
          },
          {
            "code": "factor x^2 - 9",
            "note": "difference of squares: (x - 3)*(x + 3)"
          },
          {
            "code": "factor 6x^2 - 18x",
            "note": "pulls the GCF: 6*x*(x - 3)"
          },
          {
            "code": "factor x^2 + 6x + 9",
            "note": "perfect square: (x + 3)^2"
          }
        ]
      },
      {
        "title": "Reduce a rational to lowest terms with cancel (new)",
        "why": "cancel divides out the shared factors of a fraction's numerator and denominator — the reduction that simplify pointedly refuses to do. Use it whenever a ratio looks reducible. You can pin the variable with a trailing comma-argument when the expression has more than one symbol.",
        "steps": [
          {
            "code": "simplify (x^2 - 1)/(x - 1)",
            "note": "unchanged — simplify won't touch a rational's GCD"
          },
          {
            "code": "cancel (x^2 - 1)/(x - 1)",
            "note": "the GCD divides out: x + 1"
          },
          {
            "code": "cancel (x^2 - 1)/(x - 1), x",
            "note": "same result, variable pinned to x"
          }
        ]
      },
      {
        "title": "Combine fractions over one denominator with together (new)",
        "why": "together rewrites a sum or difference of fractions as a single ratio over the common denominator — the natural inverse of apart. It also quietly reduces trivial results, so a sum that collapses to a constant comes back as one. This is step one of the combine-to-reduce workflow below.",
        "steps": [
          {
            "code": "together 1/x + 1/y",
            "note": "(x + y)/(x*y)"
          },
          {
            "code": "together 1/(x-1) + 1/(x+1)",
            "note": "2*x/((x - 1)*(x + 1))"
          },
          {
            "code": "together x/(x+1) + 1/(x+1)",
            "note": "auto-reduces all the way to 1"
          }
        ]
      },
      {
        "title": "The combine to reduce pipeline: together then cancel",
        "why": "This is the headline pattern for messy rational expressions. cancel only reduces something that is already a single ratio — hand it a raw sum of fractions and it has nothing to divide, so it comes back unchanged. Combine first with together, then feed the resulting single fraction to cancel. Since the console has no last-answer variable, copy together's output into the cancel line.",
        "steps": [
          {
            "code": "cancel x/(x^2-1) + 1/(x^2-1)",
            "note": "unchanged — cancel won't combine a sum on its own"
          },
          {
            "code": "together x/(x^2-1) + 1/(x^2-1)",
            "note": "one denominator: (x + 1)/(x^2 - 1)"
          },
          {
            "code": "cancel (x + 1)/(x^2 - 1)",
            "note": "now it reduces: 1/(x - 1)"
          }
        ]
      },
      {
        "title": "Split a ratio into partial fractions with apart",
        "why": "apart decomposes a rational expression into a sum of simple fractions — the inverse of together, and exactly what you want before integrating or reading off residues. It handles repeated roots too, producing the 1/x and 1/x^2 tower. Pin the variable with a trailing comma-argument when there is more than one symbol.",
        "steps": [
          {
            "code": "apart (3x+2)/((x+1)(x+2))",
            "note": "-1/(x + 1) + 4/(x + 2)"
          },
          {
            "code": "apart (3x+2)/((x+1)(x+2)), x",
            "note": "same decomposition, variable pinned"
          },
          {
            "code": "apart 1/(x^2 (x+1)), x",
            "note": "repeated root: 1/(x + 1) - 1/x + 1/x^2"
          }
        ]
      },
      {
        "title": "Group a polynomial by powers of a variable with collect",
        "why": "collect regroups a polynomial by powers of the variable you name, pulling the coefficient of each power into its own bracket — ideal for reading off coefficients or treating one symbol as the main variable. Gotcha: collect regroups what is already there; it does not multiply products out. If your expression still has unexpanded products, run expand first, then collect.",
        "steps": [
          {
            "code": "collect x*y + 2x + y*x^2, x",
            "note": "grouped by x: y*x^2 + x*(y + 2)"
          },
          {
            "code": "collect a*x^2 + b*x^2 + c*x + x, x",
            "note": "coefficients gathered: x^2*(a + b) + x*(c + 1)"
          },
          {
            "code": "expand (x + a)*(x + b)",
            "note": "expand first: x^2 + a*b + a*x + b*x"
          },
          {
            "code": "collect x^2 + a*x + b*x + a*b, x",
            "note": "then collect: x^2 + a*b + x*(a + b)"
          }
        ]
      },
      {
        "title": "Substitute values: subs (symbolic) vs eval (numeric)",
        "why": "subs replaces symbols and keeps the result symbolic — plug in an expression and it stays an expression. eval instead crunches to a number once every symbol has a value. Use subs to specialize a formula, eval to get an actual value. Both take comma-separated name=value pairs.",
        "steps": [
          {
            "code": "subs a*x + b, a=2, b=5",
            "note": "constants substituted, still symbolic: 2*x + 5"
          },
          {
            "code": "subs x^2 + y, x=t+1",
            "note": "substitute a whole expression: (t + 1)^2 + y"
          },
          {
            "code": "eval x^2 + y, x=3, y=0.5",
            "note": "everything numeric: 9.5"
          }
        ]
      },
      {
        "title": "Typeset any result as LaTeX",
        "why": "latex renders an expression as LaTeX source you can paste into a paper or notes — fractions, roots, and powers come out formatted. Gotcha: latex prints the expression exactly as written and does not resolve := bindings, so give it the literal expression (typically the output of a previous verb), not a variable name.",
        "steps": [
          {
            "code": "latex sqrt(x)/2",
            "note": "\\frac{\\sqrt{x}}{2}"
          },
          {
            "code": "latex (x+1)/(x-1)",
            "note": "\\frac{x + 1}{x - 1}"
          },
          {
            "code": "latex -1/(x+1) + 4/(x+2)",
            "note": "typeset an apart result: -\\frac{1}{x + 1} + \\frac{4}{x + 2}"
          }
        ]
      },
      {
        "title": "Full workflow: name it, combine, reduce, typeset",
        "why": "Chains the whole story with a session variable. := binds a name lazily — it does nothing until a later line uses it — and computing verbs like together and cancel resolve the name to its expression. Note the one exception: latex does not expand := bindings (feed it the name and it prints the name back), so the final step passes the reduced fraction literally. This is the end-to-end move from a raw expression to publication-ready LaTeX.",
        "steps": [
          {
            "code": "D := x/(x^2-1) + 1/(x^2-1)",
            "note": "bind D; nothing computes yet (lazy)"
          },
          {
            "code": "together D",
            "note": "the verb resolves D and combines: (x + 1)/(x^2 - 1)"
          },
          {
            "code": "cancel (x + 1)/(x^2 - 1)",
            "note": "reduce the combined fraction: 1/(x - 1)"
          },
          {
            "code": "latex 1/(x - 1)",
            "note": "typeset the final result: \\frac{1}{x - 1}"
          }
        ]
      }
    ]
  },
  {
    "id": "calculus",
    "title": "Calculus",
    "blurb": "Differentiate, integrate, and take limits at the console. Every verb infers the variable when the input has a single symbol and names the technique it used, so you see not just the answer but how it was reached — and where a real closed form does not exist, MathSolver says so instead of faking one.",
    "recipes": [
      {
        "title": "Differentiate: inferred variable, chain, product, quotient",
        "why": "diff is the workhorse of this section. When the input has exactly one free symbol you can drop the variable and let the console infer it; with two or more you must name it. These lines exercise the chain, product, and quotient rules so you can recognise the shape of each output.",
        "steps": [
          {
            "code": "diff x^3 + 2x",
            "note": "one free symbol, so the variable is inferred -> 3*x^2 + 2"
          },
          {
            "code": "diff sin(x^2), x",
            "note": "chain rule -> 2*x*cos(x^2)"
          },
          {
            "code": "diff x^2 * sin(x), x",
            "note": "product rule -> x^2*cos(x) + 2*x*sin(x)"
          },
          {
            "code": "diff x/(x^2+1), x",
            "note": "quotient rule -> -2*x^2/(x^2 + 1)^2 + 1/(x^2 + 1)"
          },
          {
            "code": "diff e^(sin(x)), x",
            "note": "chain through the exponential -> e^(sin(x))*cos(x)"
          }
        ]
      },
      {
        "title": "Partial derivatives of a multivariable expression",
        "why": "Naming the variable turns diff into a partial derivative: it differentiates with respect to that symbol and treats every other as a constant. With more than one free symbol the console refuses to guess — a deliberate guard, since the answer depends entirely on which variable you mean.",
        "steps": [
          {
            "code": "diff x^2*y + y^3, x",
            "note": "partial wrt x, y held constant -> 2*x*y"
          },
          {
            "code": "diff x^2*y + y^3, y",
            "note": "partial wrt y -> x^2 + 3*y^2"
          },
          {
            "code": "diff x*y",
            "note": "gotcha: two symbols (x, y) and no variable -> usage error asking you to name it"
          }
        ]
      },
      {
        "title": "Higher-order derivatives by re-running diff",
        "why": "There is no second-derivative flag; you take the derivative, then differentiate the result. Feeding an output line back into the same verb is the general pattern for iterating any operation, and here it walks you up the derivative ladder one order at a time.",
        "steps": [
          {
            "code": "diff x^4 - 3x^2, x",
            "note": "first derivative -> 4*x^3 - 6*x"
          },
          {
            "code": "diff 4x^3 - 6x, x",
            "note": "differentiate that again -> the second derivative, 12*x^2 - 6"
          },
          {
            "code": "diff 12x^2 - 6, x",
            "note": "third derivative -> 24*x"
          }
        ]
      },
      {
        "title": "Indefinite integrals and the method that solved them",
        "why": "integrate returns an antiderivative plus the constant of integration and prints a 'method:' line naming the techniques it composed (table, power rule, u-substitution, integration by parts, linearity). Like diff, the variable is inferred when only one symbol is present. Watch for ln(abs(x)) — the absolute value is the honest antiderivative of 1/x.",
        "steps": [
          {
            "code": "integrate 3*t^2",
            "note": "one symbol, inferred -> t^3 + C (method: linearity + power rule)"
          },
          {
            "code": "integrate 1/x, x",
            "note": "note the absolute value -> ln(abs(x)) + C (method: table)"
          },
          {
            "code": "integrate 2x/(x^2+1), x",
            "note": "u-substitution -> ln(x^2 + 1) + C (method: linearity + u-substitution + table)"
          },
          {
            "code": "integrate x*e^x, x",
            "note": "integration by parts -> x*e^x - e^x + C"
          },
          {
            "code": "integrate 1/cos(x)^2, x",
            "note": "table lookup -> tan(x) + C"
          }
        ]
      },
      {
        "title": "Definite integrals: add the bounds",
        "why": "Append the lower and upper limits as two more comma-separated arguments and integrate reports value = ... via the Fundamental Theorem (method: FTC). Bounds are parsed as expressions, so pi and e work directly. Results stay exact when possible — including special functions such as the error function erf.",
        "steps": [
          {
            "code": "integrate x^2, x, 0, 1",
            "note": "exact -> value = 1/3"
          },
          {
            "code": "integrate sin(x), x, 0, pi",
            "note": "the bound pi is parsed as an expression -> value = 2"
          },
          {
            "code": "integrate e^(-x^2), x, 0, 1",
            "note": "exact via the error function -> value = sqrt(pi)*erf(1)/2"
          },
          {
            "code": "integrate 1/x, x, 1, e",
            "note": "e is the constant, not a symbol -> value = 1"
          }
        ]
      },
      {
        "title": "Honest about the unsolvable",
        "why": "MathSolver never fabricates an antiderivative. When no rule applies, an indefinite integral answers 'unable to integrate' and still exits cleanly with status 0 — that IS the answer. Ask for the same integrand over a definite interval and it falls back to adaptive numerical quadrature (adaptive Simpson), so a bounded question always yields a number.",
        "steps": [
          {
            "code": "integrate e^(x^2), x",
            "note": "no elementary antiderivative exists -> unable to integrate"
          },
          {
            "code": "integrate e^(x^2), x, 0, 1",
            "note": "same integrand, now bounded -> value ~ 1.4626517 (method: numeric, adaptive Simpson)"
          },
          {
            "code": "integrate sin(x)/x, x, 1, 2",
            "note": "the sine integral, also numeric -> value ~ 0.6593299"
          }
        ]
      },
      {
        "title": "Verify an integral by differentiating it",
        "why": "The surest check on an antiderivative is to differentiate it back to the integrand. Integrate, drop the + C, feed the result into diff, and you should recover exactly what you started with. This two-step round trip catches sign flips and coefficient slips instantly.",
        "steps": [
          {
            "code": "integrate x*cos(x), x",
            "note": "integration by parts -> x*sin(x) + cos(x) + C"
          },
          {
            "code": "diff x*sin(x) + cos(x), x",
            "note": "differentiate the antiderivative (leave off + C) -> x*cos(x), the original integrand. Checks out."
          }
        ]
      },
      {
        "title": "Bind an expression once, then differentiate and integrate it",
        "why": "A := binding stores an expression and substitutes it into every LATER line, so you can name a function once and run several calculus verbs on it without retyping. Bindings are lazy and order-sensitive: define f before the lines that use it. Use vars to review and unset to clear.",
        "steps": [
          {
            "code": "f := x^3*sin(x)",
            "note": "bind the function to the name f"
          },
          {
            "code": "vars",
            "note": "review active bindings -> f := x^3*sin(x)"
          },
          {
            "code": "diff f, x",
            "note": "f expands in place -> x^3*cos(x) + 3*x^2*sin(x)"
          },
          {
            "code": "integrate f, x",
            "note": "same f, integrated by parts -> -x^3*cos(x) + 3*(x^2*sin(x) - 2*(-x*cos(x) + sin(x))) + C"
          },
          {
            "code": "unset f",
            "note": "clear the binding; a following vars now reports 'no variables defined'"
          }
        ]
      },
      {
        "title": "One-sided limits and limits at infinity",
        "why": "A fourth argument, left or right, takes a single-sided limit — essential where the two sides disagree, as with 1/x at 0. Use inf, oo, or -inf / -oo as the point to probe end behavior. The console reports the method it used (L'Hopital, rational degree analysis, or the x = 1/u numeric extrapolation) so you can gauge how the answer was reached.",
        "steps": [
          {
            "code": "limit 1/x, x, 0, right",
            "note": "approach 0 from the right -> limit = +inf"
          },
          {
            "code": "limit 1/x, x, 0, left",
            "note": "from the left -> limit = -inf"
          },
          {
            "code": "limit (2x+1)/(x-3), x, oo",
            "note": "end behavior of a rational function -> limit = 2 (rational degree analysis)"
          },
          {
            "code": "limit e^(-x), x, oo",
            "note": "decays to zero -> limit ~ 0"
          },
          {
            "code": "limit e^x, x, -oo",
            "note": "point at negative infinity -> limit ~ 0"
          }
        ]
      },
      {
        "title": "Two-sided limits, L'Hopital, and the DNE verdict",
        "why": "With no direction the console computes both one-sided limits and reports a value only when they agree. Classic 0/0 forms are resolved by L'Hopital automatically, iterating internally when one pass is not enough. A genuine mismatch returns 'the limit does not exist' with the offending left and right values as warnings.",
        "steps": [
          {
            "code": "limit sin(x)/x, x, 0",
            "note": "the textbook 0/0 -> limit = 1 (l'hopital)"
          },
          {
            "code": "limit (1-cos(x))/x^2, x, 0",
            "note": "needs two L'Hopital passes -> limit = 1/2"
          },
          {
            "code": "limit (e^x-1)/x, x, 0",
            "note": "another 0/0 -> limit = 1"
          },
          {
            "code": "limit abs(x)/x, x, 0",
            "note": "sides disagree (-1 vs 1) -> the limit does not exist"
          }
        ]
      },
      {
        "title": "mlimit: two-variable limits by path sampling",
        "why": "A limit in the plane exists only if EVERY path to the point gives the same value. mlimit samples ten paths — eight rays (the axes and diagonals) plus the two parabolas that catch the classic x = y^2 counterexamples. If they all agree it reports the value; if any two disagree it returns DNE naming the witnessing paths. Sampling is evidence, not proof, and the console warns so. Note: the path parameter is t, so rename any t inside your expression or mlimit errors.",
        "steps": [
          {
            "code": "mlimit x^2*y/(x^2+y^2), x, 0, y, 0",
            "note": "every path agrees -> limit = 0"
          },
          {
            "code": "mlimit x*y/(x^2+y^2), x, 0, y, 0",
            "note": "x-axis gives 0 but diagonal y=x gives 0.5 -> the limit does not exist"
          },
          {
            "code": "mlimit x^2/(x^2+y^2), x, 0, y, 0",
            "note": "x-axis gives 1, y-axis gives 0 -> the limit does not exist"
          }
        ]
      }
    ]
  },
  {
    "id": "equations",
    "title": "Equations & Systems",
    "blurb": "Everything the console does with an equals sign: solving a single equation for a variable and solving a set of linear equations at once. The engine tries exact methods first (quadratic formula, rational roots, algebraic isolation), reports complex roots and domain conditions honestly, and falls back to a bounded numeric search only when no closed form exists. Every solution is verified before it is printed, and the method used is always reported on a `method:` line, followed by any `warning:` lines.",
    "recipes": [
      {
        "title": "Solve a bare equation — and name the variable when it matters",
        "why": "The console's core reflex: type any equation and it is solved automatically (a bare expression, by contrast, is only simplified). When several symbols are present the engine cannot guess which to isolate, so use the `solve` command with a trailing variable. Real roots come back sorted ascending, and the `method:` line tells you how they were found.",
        "steps": [
          {
            "code": "x^2 = 4",
            "note": "bare equation — solved on sight, prints x = -2 then x = 2"
          },
          {
            "code": "solve x^2 = 4, x",
            "note": "same result, variable stated explicitly"
          },
          {
            "code": "solve 2x + 3y = 12, y",
            "note": "two symbols: name y to get y = -2*x/3 + 4 (method: linear)"
          }
        ]
      },
      {
        "title": "Find the zeros of an expression: set it = 0",
        "why": "`solve` always needs an equals sign — `solve x^2 - 5x + 6, x` errors with 'expected = in equation', so to find where an expression vanishes you write `expr = 0`. This is the everyday way to get a polynomial's roots. Pair it with `factor` to see why the roots are what they are.",
        "steps": [
          {
            "code": "solve x^2 - 5x + 6 = 0, x",
            "note": "roots x = 2 and x = 3 (verified, sorted)"
          },
          {
            "code": "factor x^2 - 5x + 6",
            "note": "prints (x - 3)*(x - 2) — the factors expose the same roots"
          }
        ]
      },
      {
        "title": "Exact quadratics: surds and repeated roots",
        "why": "For a quadratic the engine applies the quadratic formula symbolically and keeps roots exact, pulling perfect squares out of the radical (sqrt(8) becomes 2*sqrt(2)). A zero discriminant yields a single (double) root, reported once rather than duplicated. Reach for this whenever you want radicals, not decimals.",
        "steps": [
          {
            "code": "solve x^2 - 2 = 0, x",
            "note": "x = -sqrt(2), x = sqrt(2)"
          },
          {
            "code": "solve x^2 = 8, x",
            "note": "radical auto-simplified to x = -2*sqrt(2), x = 2*sqrt(2)"
          },
          {
            "code": "solve x^2 - 4x + 4 = 0, x",
            "note": "double root: prints x = 2 just once"
          }
        ]
      },
      {
        "title": "Complex roots of a quadratic",
        "why": "A negative discriminant has no real answer, so the console prints 'no real solutions; complex roots:' and gives the exact conjugate pair using i. These complex candidates are checked symbolically (substituting a root back must collapse the residual to exactly zero via the i^2 rules), not numerically. Use it when you actually want the complex pair rather than just 'no real solutions'.",
        "steps": [
          {
            "code": "solve x^2 + 1 = 0, x",
            "note": "x = -i and x = i"
          },
          {
            "code": "solve x^2 + 2x + 5 = 0, x",
            "note": "conjugate pair x = -2*i - 1 and x = 2*i - 1"
          },
          {
            "code": "solve 2x^2 + 2x + 5 = 0, x",
            "note": "fractional parts: x = -3*i/2 - 1/2 and x = 3*i/2 - 1/2"
          }
        ]
      },
      {
        "title": "The symbolic quadratic formula, with domain conditions",
        "why": "Give the coefficients as symbols and solve returns the literal quadratic formula. Crucially it also emits the validity conditions it cannot assume: the roots are real only when the discriminant is non-negative, and only meaningful when the leading coefficient is non-zero. Read the warnings — they are part of the answer.",
        "steps": [
          {
            "code": "solve a*x^2 + b*x + c = 0, x",
            "note": "prints x = (-b ∓ sqrt(b^2 - 4*a*c))/(2*a) plus warnings 'solutions are real only when b^2 - 4*a*c >= 0' and 'roots are valid only when a != 0'"
          }
        ]
      },
      {
        "title": "Trigonometric equations and periodicity",
        "why": "Trig equations are solved by inverse-function isolation, which returns the principal solution plus a parenthesized note describing the full repeating family (period 2*pi for sin/cos, pi for tan). A target outside a function's range is correctly reported as having no real solution. The gotcha: only the principal value is printed as a root — read the note for the general form.",
        "steps": [
          {
            "code": "solve sin(x) = 1/2, x",
            "note": "x = pi/6, note: general x = pi/6 + 2*pi*n or x = pi - pi/6 + 2*pi*n"
          },
          {
            "code": "solve cos(x) = 0, x",
            "note": "x = pi/2, note: general x = ±pi/2 + 2*pi*n"
          },
          {
            "code": "solve tan(x) = 1, x",
            "note": "x = pi/4, note: general x = pi/4 + pi*n (period pi)"
          },
          {
            "code": "solve sin(x) = 2, x",
            "note": "out of range -> 'no real solutions'"
          }
        ]
      },
      {
        "title": "Isolating invertible functions (ln, sqrt, powers, abs)",
        "why": "When the unknown appears exactly once wrapped in an invertible function, the engine peels it off algebraically for an exact answer — logs, roots, exponents in the base or the exponent, and absolute values. Absolute value splits into its two branches automatically. This path (method: isolation) runs before any numeric fallback, so you keep symbolic results like e^2.",
        "steps": [
          {
            "code": "solve ln(x) = 2, x",
            "note": "x = e^2"
          },
          {
            "code": "solve sqrt(x) = 3, x",
            "note": "x = 9"
          },
          {
            "code": "solve 2^x = 8, x",
            "note": "unknown in the exponent: x = 3 via ln(c)/ln(a)"
          },
          {
            "code": "solve |x - 3| = 5, x",
            "note": "both branches: x = -2 and x = 8"
          }
        ]
      },
      {
        "title": "Higher-degree polynomials",
        "why": "Beyond quadratics, solve peels rational roots (rational-root theorem) and finishes a leftover quadratic exactly, and it recognizes 'biquadratic' shapes by substituting y = x^k. When a leftover factor has non-real roots it reports the real ones and warns you to solve that factor alone — a clean two-step way to surface the complex roots of a cubic.",
        "steps": [
          {
            "code": "solve x^3 - 6x^2 + 11x - 6 = 0, x",
            "note": "three rational roots x = 1, 2, 3 (method: rational roots + quadratic)"
          },
          {
            "code": "solve x^4 - 5x^2 + 4 = 0, x",
            "note": "substitution x^2: roots -2, -1, 1, 2"
          },
          {
            "code": "solve x^3 - x^2 + x - 1 = 0, x",
            "note": "real root x = 1, with 'the remaining quadratic factor has two non-real roots' warning"
          },
          {
            "code": "solve x^2 + 1 = 0, x",
            "note": "solve that leftover factor alone to see x = -i and x = i"
          }
        ]
      },
      {
        "title": "The numeric fallback for transcendental equations",
        "why": "When no exact method applies (transcendental equations, irreducible high-degree factors), solve switches to a bracketed numeric search and prints roots with '≈' (method: numeric (Newton/bisection)). The console scans a fixed window of [-100, 100] and always prints a warning that roots outside that interval are not reported. It returns every root it brackets in that window, sorted ascending.",
        "steps": [
          {
            "code": "solve cos(x) = x, x",
            "note": "single root x ≈ 0.739085133215161, searched over [-100, 100]"
          },
          {
            "code": "solve x^3 + x - 5 = 0, x",
            "note": "irreducible cubic -> numeric single real root x ≈ 1.51598022769282"
          },
          {
            "code": "solve sin(x) = x/2, x",
            "note": "three roots x ≈ -1.89549426703398, x ≈ 0, x ≈ 1.89549426703398"
          }
        ]
      },
      {
        "title": "Linear systems with semicolons",
        "why": "Separate equations with semicolons and list every unknown after the equation block to solve a linear system by Gaussian elimination over exact arithmetic. The three outcomes are all handled: a unique exact solution, an inconsistent system ('no solution (inconsistent system)'), and an underdetermined one where the extra unknowns are reported as `free:` and the pivots are written in terms of them.",
        "steps": [
          {
            "code": "solve x + y = 3; x - y = 1, x, y",
            "note": "unique solution x = 2, y = 1 (method: gaussian elimination)"
          },
          {
            "code": "solve x + y + z = 6; 2x - y + z = 3; x + 2y - z = 2, x, y, z",
            "note": "3x3 system -> x = 1, y = 2, z = 3"
          },
          {
            "code": "solve x + y = 1; x + y = 2, x, y",
            "note": "contradiction -> 'no solution (inconsistent system)'"
          },
          {
            "code": "solve x + y + z = 1; x - y = 0, x, y, z",
            "note": "underdetermined: x = -z/2 + 1/2, y = -z/2 + 1/2, free: z"
          }
        ]
      },
      {
        "title": "eval, subs, and verifying a solution",
        "why": "Two different substitution tools: `eval` plugs in numeric literals and returns a decimal, while `subs` replaces symbols with expressions and simplifies symbolically. The gotcha is that `eval` accepts numbers ONLY — `x=pi/6` is rejected with 'not a number', so use `subs` for symbolic values. Together they let you close the loop: solve an equation, then eval the residual at a root to confirm it lands on ~0.",
        "steps": [
          {
            "code": "eval x^2 + y, x=3, y=0.5",
            "note": "pure numeric evaluation -> 9.5"
          },
          {
            "code": "subs a*x + 3, a=2",
            "note": "symbolic substitution -> 2*x + 3"
          },
          {
            "code": "subs sin(x), x=pi/6",
            "note": "subs handles pi/6 and folds it to 1/2 (eval would reject pi/6)"
          },
          {
            "code": "solve cos(x) = x, x",
            "note": "gives the numeric root x ≈ 0.739085133215161"
          },
          {
            "code": "eval cos(x) - x, x=0.739085133215161",
            "note": "residual ≈ -5.55e-16 confirms it is a genuine root"
          }
        ]
      },
      {
        "title": "Session variables in solving (a multi-step workflow)",
        "why": "`:=` binds a name that applies lazily to every LATER line, so you can parameterize an equation and reuse it. Solving for a symbol that also has a binding ignores the binding (with a warning that names 'unset') so you still get the general solution. A name can even hold a whole equation — spell it with an underscore, like E_1 — and then stand in as one row of a system.",
        "steps": [
          {
            "code": "k := 7",
            "note": "bind a parameter; echoes k := 7"
          },
          {
            "code": "solve x^2 = k, x",
            "note": "k resolves to 7 -> x = -sqrt(7), x = sqrt(7)"
          },
          {
            "code": "E_1 := x + y = 3",
            "note": "an equation-valued binding (underscore form required; E1 errors as 'E1 reads as E*1')"
          },
          {
            "code": "solve E_1; x - y = 1, x, y",
            "note": "E_1 stands as the first system row -> x = 2, y = 1"
          },
          {
            "code": "vars",
            "note": "lists active bindings in definition order: k := 7, E_1 := x + y = 3"
          }
        ]
      }
    ]
  },
  {
    "id": "series-discrete",
    "title": "Series & Discrete Math",
    "blurb": "Taylor and asymptotic expansions, exact closed forms for sums and products, linear recurrences solved to closed form, Stirling asymptotics, and pattern recognition from raw data. Every result here is computed exactly (rational arithmetic, characteristic roots, undetermined coefficients) — the console is a symbolic engine, not a table of special values, so it either returns an honest closed form or tells you it cannot.",
    "recipes": [
      {
        "title": "Taylor series about zero",
        "why": "The workhorse: series expands a function about a center to a chosen order. Written bare (series cos(x)) it defaults to center 0 and order 6; add a variable and order to control it. Reach for it for local polynomial approximations and generating-function coefficients. Gotcha: the order is the highest power kept and zero coefficients are dropped — so an odd function like sin only shows odd powers and the top printed power can sit below the order you asked for (max order is 20).",
        "steps": [
          {
            "code": "series sin(x), x, 0, 5",
            "note": "odd powers only: x - x^3/6 + x^5/120"
          },
          {
            "code": "series cos(x)",
            "note": "bare form uses default center 0, order 6"
          },
          {
            "code": "series e^x, x, 0, 4",
            "note": "1 + x + x^2/2 + x^3/6 + x^4/24"
          },
          {
            "code": "series 1/(1+x^2), x, 0, 8",
            "note": "alternating even powers, the geometric pattern"
          }
        ]
      },
      {
        "title": "Expand about a nonzero center",
        "why": "Pass a center as the third argument to expand about x = c; the answer is printed in powers of (x - c). Use it near a working point, or where a function has no expansion at 0 (ln, sqrt). The center must be a constant — it may not depend on the series variable, and the engine errors if it does. Notice constants like e ride along in the coefficients.",
        "steps": [
          {
            "code": "series ln(x), x, 1, 4",
            "note": "powers of (x - 1); ln has no series at 0"
          },
          {
            "code": "series sqrt(x), x, 4, 3",
            "note": "expand a square-root about a perfect square"
          },
          {
            "code": "series e^x, x, 1, 3",
            "note": "each coefficient carries the constant e"
          }
        ]
      },
      {
        "title": "Asymptotic series at infinity",
        "why": "Use inf (or oo) as the center to expand in powers of 1/x as x grows. For a rational function this cleanly splits the exact polynomial part (the asymptote) from a 1/x tail — the fastest way to read off an oblique asymptote and large-x behavior. Gotcha: only rational functions (plus a few direct 1/x substitutions) are supported; algebraic forms like sqrt(x^2+x) error out instead of expanding.",
        "steps": [
          {
            "code": "series x/(x+1), x, inf, 4",
            "note": "1 - 1/x + 1/x^2 - 1/x^3 + 1/x^4"
          },
          {
            "code": "series (x^2+1)/(x-1), x, inf, 4",
            "note": "polynomial part x + 1, then a 2/x tail"
          },
          {
            "code": "series (2x^2 - x + 1)/(x+3), x, inf, 3",
            "note": "oblique asymptote 2x - 7 plus corrections"
          }
        ]
      },
      {
        "title": "Faulhaber: closed forms for power sums",
        "why": "sum term, var, lo, hi returns an exact closed form in the (symbolic) upper bound. Polynomial summands are fit exactly — this is Faulhaber's formula, derived rather than looked up. Several terms in the summand are folded into a single closed form. A fully numeric range is added up directly and exactly.",
        "steps": [
          {
            "code": "sum k, k, 1, n",
            "note": "n^2/2 + n/2"
          },
          {
            "code": "sum k^2, k, 1, n",
            "note": "n^3/3 + n^2/2 + n/6"
          },
          {
            "code": "sum k^3, k, 1, n",
            "note": "n^4/4 + n^3/2 + n^2/4 (a perfect square)"
          },
          {
            "code": "sum k^2 + 2k + 1, k, 1, n",
            "note": "multiple terms folded into one closed form"
          },
          {
            "code": "sum k, k, 1, 100",
            "note": "numeric bounds sum directly to 5050"
          }
        ]
      },
      {
        "title": "Geometric sums, finite and infinite",
        "why": "Geometric and mixed polynomial-times-geometric summands get closed forms too. Set hi to inf for an infinite series: with a numeric ratio of magnitude below 1 it converges to an exact value, with a symbolic ratio you get the closed form plus a 'valid for |x| < 1' warning. Series that diverge are reported as diverging (with the reason), never silently.",
        "steps": [
          {
            "code": "sum 2^k, k, 0, n",
            "note": "finite geometric: 2*2^n - 1"
          },
          {
            "code": "sum k*2^k, k, 0, n",
            "note": "polynomial x geometric, still closed-form"
          },
          {
            "code": "sum (1/3)^k, k, 0, inf",
            "note": "converges to 3/2"
          },
          {
            "code": "sum x^k, k, 0, inf",
            "note": "1/(1 - x), flagged valid for |x| < 1"
          },
          {
            "code": "sum 3^k, k, 0, inf",
            "note": "reported divergent: |ratio| >= 1"
          }
        ]
      },
      {
        "title": "Harmonic sums and telescoping series",
        "why": "Sums of 1/k become honest harmonic numbers harmonic(n), and an offset lower bound just shifts by a constant. Rational summands whose partial fractions telescope get an exact infinite sum. Gotcha: a non-telescoping p-series like the Basel sum sum 1/k^2 returns 'unable to find a closed form' — the engine is exact, so it declines rather than quoting pi^2/6.",
        "steps": [
          {
            "code": "sum 1/k, k, 1, n",
            "note": "harmonic(n)"
          },
          {
            "code": "sum 1/k, k, 3, n",
            "note": "harmonic(n) - 3/2 (dropped first two terms)"
          },
          {
            "code": "eval harmonic(n), n=10",
            "note": "read H(10) as a number, ~2.9289682"
          },
          {
            "code": "sum 1/(k*(k+1)), k, 1, inf",
            "note": "telescopes exactly to 1"
          },
          {
            "code": "sum 1/(k*(k+2)), k, 1, inf",
            "note": "telescopes to 3/4"
          }
        ]
      },
      {
        "title": "Products: factorials and geometric",
        "why": "product multiplies a term over the range. A numeric range gives an exact integer — product k over 1..n is n!. A symbolic upper bound works for constant and geometric factors, producing r^(triangular-number) style closed forms. Gotcha: a genuine factorial of a symbolic bound (a polynomial factor with a symbolic hi) has no closed form yet and is declined.",
        "steps": [
          {
            "code": "product k, k, 1, 5",
            "note": "5! = 120 by direct product"
          },
          {
            "code": "product 2^k, k, 1, n",
            "note": "2^(n*(n+1)/2)"
          },
          {
            "code": "product x, k, 1, n",
            "note": "constant-in-k factor gives x^n"
          },
          {
            "code": "product 1/2, k, 1, n",
            "note": "(1/2)^n"
          }
        ]
      },
      {
        "title": "Recurrences to closed form with rsolve",
        "why": "rsolve solves linear constant-coefficient recurrences by characteristic roots, with initial conditions listed after commas. Distinct roots, repeated roots, and polynomial or geometric forcing (via undetermined coefficients) are all handled — Fibonacci comes back as Binet's formula. Gotchas: the recurrence must use a(n)-relative indices (a(n+2), never a(3)); complex characteristic roots are not supported yet and error out; and any missing initial condition defaults to 0 with a warning.",
        "steps": [
          {
            "code": "rsolve a(n+2) = a(n+1) + a(n), a(0)=0, a(1)=1",
            "note": "Fibonacci to Binet's formula (golden-ratio powers)"
          },
          {
            "code": "rsolve a(n+1) = 2 a(n), a(0)=1",
            "note": "pure geometric: 2^n"
          },
          {
            "code": "rsolve a(n+2) = 3 a(n+1) - 2 a(n), a(0)=0, a(1)=1",
            "note": "distinct roots: 2^n - 1"
          },
          {
            "code": "rsolve a(n+2) = 4 a(n+1) - 4 a(n), a(0)=1, a(1)=4",
            "note": "repeated root: n*2^n + 2^n"
          },
          {
            "code": "rsolve a(n+1) = 2 a(n) + 3^n, a(0)=0",
            "note": "geometric forcing: 3^n - 2^n"
          },
          {
            "code": "rsolve a(n+1) = a(n) + n, a(0)=0",
            "note": "polynomial forcing: n^2/2 - n/2"
          }
        ]
      },
      {
        "title": "Recognize a sequence with seq",
        "why": "seq takes four or more exact terms (indexed from n = 0) and searches for the rule behind them: a geometric ratio, an arithmetic or polynomial law via finite differences, or a linear recurrence up to order 3. It prints the pattern, a closed form when one exists, and the next three terms — ideal for reverse-engineering a formula from data. Gotcha: it needs at least 4 terms, and genuinely patternless input (e.g. the primes) is reported as unrecognized.",
        "steps": [
          {
            "code": "seq 2, 5, 8, 11, 14, 17",
            "note": "arithmetic, common difference 3"
          },
          {
            "code": "seq 3, 6, 12, 24, 48, 96",
            "note": "geometric, ratio 2, closed form 3*2^n"
          },
          {
            "code": "seq 1, 3, 6, 10, 15, 21",
            "note": "triangular numbers, degree-2 polynomial"
          },
          {
            "code": "seq 0, 1, 1, 2, 3, 5, 8",
            "note": "Fibonacci: recurrence plus Binet closed form"
          },
          {
            "code": "seq 0, 1, 2, 5, 12, 29, 70",
            "note": "Pell numbers: a(n+2) = 2a(n+1) + a(n)"
          }
        ]
      },
      {
        "title": "Stirling asymptotics for ln Gamma and factorials",
        "why": "stirling var, terms prints the Stirling series for ln Gamma(var) with exact Bernoulli-number coefficients, followed by accuracy checks against the true value at var = 5, 10, 20. Since ln n! = ln Gamma(n+1), it is your tool for large-factorial estimates. Gotcha: it is an asymptotic (ultimately divergent) series — for a fixed var more terms help only up to a point; accuracy improves most by increasing var.",
        "steps": [
          {
            "code": "stirling x, 3",
            "note": "leading terms plus three 1/x^k corrections, with error notes"
          },
          {
            "code": "stirling n, 2",
            "note": "same in the variable n, two corrections"
          },
          {
            "code": "stirling x, 0",
            "note": "bare leading form (x - 1/2)*ln(x) - x + ln(2*pi)/2"
          }
        ]
      },
      {
        "title": "Workflow: bind a summand, reuse it, read off a number",
        "why": "A multi-step chain. Bind an expression once with := and feed it to several verbs — order matters, since a binding only affects later lines. Key gotcha: := binds expressions, not command output, so you cannot capture a 'sum = ...' line into a variable. Instead, reuse the summand across finite and infinite sums, then paste a returned closed form into eval to get a concrete value.",
        "steps": [
          {
            "code": "g := (1/2)^k",
            "note": "bind the summand once"
          },
          {
            "code": "sum g, k, 0, n",
            "note": "finite closed form: -(1/2)^n + 2"
          },
          {
            "code": "sum g, k, 0, inf",
            "note": "same summand, infinite sum = 2"
          },
          {
            "code": "p := k^2 + k",
            "note": "a fresh polynomial summand"
          },
          {
            "code": "sum p, k, 1, n",
            "note": "closed form n^3/3 + n^2 + 2*n/3"
          },
          {
            "code": "eval n^3/3 + n^2 + 2*n/3, n=20",
            "note": "paste that closed form to evaluate at n = 20 -> 3080"
          }
        ]
      }
    ]
  },
  {
    "id": "odes-transforms",
    "title": "ODEs & Transforms",
    "blurb": "Solve differential equations and move between the time and frequency domains. `dsolve` handles linear constant-coefficient IVPs (any order), plus first-order separable, linear, and Bernoulli equations and linear systems; `laplace`/`ilaplace` are the transform pair that `dsolve` uses under the hood. Every answer is exact, and each `dsolve` result also prints the partial-fraction Y(s) and the method it used, so you can watch the Laplace machinery work.",
    "recipes": [
      {
        "title": "Solve a second-order linear IVP",
        "why": "The flagship use of dsolve: a constant-coefficient equation with initial conditions, solved exactly by the Laplace method (transform, partial-fraction, invert). Type the equation and its conditions as one comma-separated line — the first segment is the ODE, the rest are y(0)=..., y'(0)=.... Note the Y(s) line: it shows the transform of the solution, and its partial-fraction terms are where the answer's terms come from.",
        "steps": [
          {
            "code": "dsolve y'' + 3y' + 2y = e^(-t), y(0)=1, y'(0)=0",
            "note": "y(t) = t*e^(-t) + e^(-t); Y(s) = 1/(s + 1) + 1/(s + 1)^2"
          },
          {
            "code": "dsolve y'' + 2y' + 5y = 0, y(0)=1, y'(0)=0",
            "note": "underdamped: y(t) = e^(-t)*(cos(2*t) + sin(2*t)/2)"
          }
        ]
      },
      {
        "title": "Read the response type off a homogeneous equation",
        "why": "For a homogeneous second-order equation (forcing = 0) the character of the roots dictates the solution shape: distinct real roots give exponentials, a repeated root gives the t*e^(rt) term, and complex roots give oscillation (undamped) or damped oscillation. Gotcha: the left side must collect y and its derivatives and the right side must be free of y, so write `y'' + 4y = 0`, never `y'' = -4y`.",
        "steps": [
          {
            "code": "dsolve y'' - y = 0, y(0)=2, y'(0)=0",
            "note": "distinct real roots ±1: y(t) = e^t + e^(-t)"
          },
          {
            "code": "dsolve y'' + 2y' + y = 0, y(0)=1, y'(0)=1",
            "note": "repeated root -1: y(t) = 2*t*e^(-t) + e^(-t), the secular t*e^(-t) appears"
          },
          {
            "code": "dsolve y'' + 4y = 0, y(0)=1, y'(0)=0",
            "note": "pure imaginary roots ±2i: simple harmonic motion, y(t) = cos(2*t)"
          }
        ]
      },
      {
        "title": "Force an oscillator into resonance",
        "why": "Drive an undamped oscillator at its natural frequency and the solution grows without bound — the secular t-term. dsolve captures this exactly because Y(s) picks up a repeated quadratic factor (s^2 + 1)^2, whose inverse transform carries the t. Compare a cosine drive with a sine drive (a 90-degree phase shift): the secular term flips between t*sin and t*cos.",
        "steps": [
          {
            "code": "dsolve y'' + y = cos(t), y(0)=0, y'(0)=0",
            "note": "resonance: y(t) = t*sin(t)/2, amplitude grows with t; Y(s) = s/(s^2 + 1)^2"
          },
          {
            "code": "dsolve y'' + y = sin(t), y(0)=0, y'(0)=0",
            "note": "sine drive: y(t) = (-t*cos(t) + sin(t))/2; Y(s) = 1/(s^2 + 1)^2"
          }
        ]
      },
      {
        "title": "Let omitted initial conditions default to zero",
        "why": "You do not have to supply every condition. Any y^(k)(0) you leave out is assumed zero and dsolve prints a warning naming it, so you get a concrete function rather than a general solution with free constants. Handy for probing a forced equation's zero-state response. Gotcha: a fully homogeneous equation with all conditions omitted has Y(s) = 0, which dsolve refuses to invert (a constant transform is an impulse), so supply forcing or at least one nonzero condition.",
        "steps": [
          {
            "code": "dsolve y'' + 2y' + y = 0, y(0)=1",
            "note": "y'(0) omitted -> 'warning: assuming y'(0) = 0', still a concrete y(t)"
          },
          {
            "code": "dsolve y'' + 4y = 1",
            "note": "both conditions omitted -> zero-state response y(t) = -cos(2*t)/4 + 1/4, two warnings"
          }
        ]
      },
      {
        "title": "First-order linear: integrating factor",
        "why": "Equations spelled `y' = f(t, y)` route to first-order methods instead of the Laplace path, so variable coefficients are allowed here (unlike the higher-order case). A right side that is linear in y — including a t-dependent coefficient — is solved by the integrating factor e^(∫p). Leave off the initial condition and dsolve returns the general solution with a free constant C.",
        "steps": [
          {
            "code": "dsolve y' = -2t*y, y(0)=1",
            "note": "variable coefficient: y(t) = e^(-t^2)"
          },
          {
            "code": "dsolve y' = -y + 1, y(0)=0",
            "note": "with forcing: y(t) = e^(-t)*(e^t - 1), i.e. 1 - e^(-t)"
          },
          {
            "code": "dsolve y' = -y + 1",
            "note": "no IC -> general solution y(t) = e^(-t)*(C + e^t), C free"
          }
        ]
      },
      {
        "title": "Separable equations, explicit or implicit",
        "why": "When every factor of f(t, y) depends on t alone or y alone, dsolve separates variables, integrates both sides, and tries to solve the relation for y. With an initial condition it picks the branch through (0, y0); when the relation cannot be inverted it reports an honest implicit solution instead of guessing. Gotcha: a bare `y' = 3*y` is linear (degree 1 in y), so it takes the integrating-factor path — separable is for genuine products/quotients like t/y.",
        "steps": [
          {
            "code": "dsolve y' = t/y, y(0)=2",
            "note": "explicit branch through (0,2): y(t) = sqrt(-2*(-t^2/2 - 2))  [= sqrt(t^2 + 4)]"
          },
          {
            "code": "dsolve y' = t/y",
            "note": "no IC: keeps C and warns it selected one of several branches"
          }
        ]
      },
      {
        "title": "Bernoulli equations",
        "why": "A right side of the form y' = p·y + q·y^m (a single power m ≥ 2 alongside the linear term, no constant) is nonlinear, but the substitution v = y^(1-m) turns it linear and dsolve does this automatically. Classic case: the logistic equation. Gotcha: y(0) = 0 is singular for the substitution and is rejected; the printed form can look tangled but is algebraically the expected curve.",
        "steps": [
          {
            "code": "dsolve y' = y - y^2, y(0)=1/2",
            "note": "logistic: prints (e^t + 1)^(-1)/e^(-t), i.e. the S-curve 1/(1 + e^(-t))"
          },
          {
            "code": "dsolve y' = -y + t*y^2, y(0)=1",
            "note": "Bernoulli with a t-dependent coefficient on y^2"
          }
        ]
      },
      {
        "title": "Systems of first-order linear ODEs",
        "why": "Separate first-order equations with semicolons and dsolve solves the coupled system in one exact linear solve over rational functions of s. Every equation must be `<name>' = ...`, linear in the unknowns with numeric coefficients; the unknowns may not be named t or s. Initial conditions are shared across the whole line, and any omitted one defaults to zero with a warning.",
        "steps": [
          {
            "code": "dsolve x' = -y; y' = x, x(0)=1, y(0)=0",
            "note": "rotation: x(t) = cos(t), y(t) = sin(t)"
          },
          {
            "code": "dsolve x' = -2x + y; y' = x - 2y, x(0)=1, y(0)=0",
            "note": "coupled decay: x = e^(-3t)/2 + e^(-t)/2, y = -e^(-3t)/2 + e^(-t)/2"
          }
        ]
      },
      {
        "title": "Build the Laplace transform table",
        "why": "`laplace` maps a time function f(t) to F(s) by composing three rules: the base table (1, sin, cos, sinh, cosh), the s-shift theorem L{e^(at)·g} = G(s-a), and frequency differentiation L{t^n·g} = (-1)^n d^n/ds^n G. The time variable defaults to t; pass a second argument to use another name (but never s, which the result is in — that is rejected). Inputs with no rule error honestly rather than returning something wrong.",
        "steps": [
          {
            "code": "laplace sin(2t)",
            "note": "base table: 2/(s^2 + 4)"
          },
          {
            "code": "laplace e^(-t) sin(2t)",
            "note": "s-shift: 2/((s + 1)^2 + 4)"
          },
          {
            "code": "laplace t^2 e^(-3t)",
            "note": "frequency differentiation: 2/(s + 3)^3"
          },
          {
            "code": "laplace cos(a*x), x",
            "note": "explicit time variable x: s/(a^2 + s^2)"
          }
        ]
      },
      {
        "title": "Invert F(s) — and always apart first",
        "why": "`ilaplace` matches each term of a sum against a partial-fraction pattern: c/(s-a)^n inverts to a polynomial·exponential, and an irreducible quadratic completes the square into damped sin/cos. The catch: it expects the input already split into strictly-proper partial fractions. Feed it a single fraction with a product denominator and it silently matches only the first factor and returns a WRONG answer — run `apart` first. A constant-in-s term is an impulse δ(t) and errors rather than lying.",
        "steps": [
          {
            "code": "ilaplace 1/(s^2 + 2s + 5)",
            "note": "completes the square: e^(-t)*sin(2*t)/2"
          },
          {
            "code": "ilaplace (2s + 3)/(s^2 + 4)",
            "note": "linear numerator over a quadratic: 3*sin(2*t)/2 + 2*cos(2*t)"
          },
          {
            "code": "ilaplace 1/((s+1)(s+2))",
            "note": "GOTCHA: returns e^(-t) — wrong, the (s+2) factor was dropped"
          },
          {
            "code": "apart 1/((s+1)(s+2)), s",
            "note": "split first: 1/(s + 1) - 1/(s + 2)"
          },
          {
            "code": "ilaplace 1/(s + 1) - 1/(s + 2)",
            "note": "now correct: e^(-t) - e^(-2*t)"
          }
        ]
      },
      {
        "title": "Reproduce dsolve by hand: the Laplace-method workflow",
        "why": "This is exactly what dsolve does internally for a linear IVP — transform the forcing, form Y(s), partial-fraction it, and invert — done step by step so you can see each stage. It also shows the two console idioms for chaining: a := variable holds an expression (not a command's output) for a later line, and you paste one line's result into the next. Great for teaching, or for handling a Y(s) you assembled yourself.",
        "steps": [
          {
            "code": "laplace t",
            "note": "transform the forcing of y'' + y = t: F(s) = 1/s^2"
          },
          {
            "code": "apart 1/(s^2*(s^2+1)), s",
            "note": "Y(s) = F/(s^2+1); split it: -1/(s^2 + 1) + 1/s^2"
          },
          {
            "code": "ilaplace 1/s^2 - 1/(s^2 + 1)",
            "note": "invert term by term: t - sin(t)"
          },
          {
            "code": "dsolve y'' + y = t, y(0)=0, y'(0)=0",
            "note": "confirm: dsolve gives the same y(t) = t - sin(t)"
          },
          {
            "code": "Y := s/(s^2 + 1)",
            "note": "bind a hand-built transform (here Y for y''+y=0, y(0)=1, y'(0)=0)"
          },
          {
            "code": "ilaplace Y",
            "note": "the := variable feeds the next line: cos(t)"
          }
        ]
      }
    ]
  },
  {
    "id": "complex",
    "title": "Complex Numbers (ℂ)",
    "blurb": "The Console treats i as a first-class constant, not a variable. That single fact powers exact Gaussian-rational arithmetic in simplify, principal-branch numeric evaluation in eval (where Euler's formula falls out cleanly), the complex functions conj/Re/Im/abs/arg, and exact complex roots from solve. These recipes work up from powers of i to a full workflow that binds a complex number to a session variable and probes its modulus, argument, and conjugate.",
    "recipes": [
      {
        "title": "Powers of the imaginary unit",
        "why": "i is a built-in constant (like pi or e), so integer powers cycle mod 4 and fold to a single number. Reach for simplify to reduce any power of i to -1, 1, i, or -i. Even an absurd exponent is handled by square-and-multiply, so i^1000000 returns instantly instead of spinning.",
        "steps": [
          {
            "code": "simplify i^2",
            "note": "-1 — the defining relation"
          },
          {
            "code": "simplify i^3",
            "note": "-i"
          },
          {
            "code": "simplify i^4",
            "note": "1 — the cycle closes"
          },
          {
            "code": "simplify i^1000000",
            "note": "1, computed at once (no slowdown)"
          }
        ]
      },
      {
        "title": "Exact Gaussian-rational arithmetic",
        "why": "simplify collapses any expression built only from numbers and i to the canonical a + b*i form — exactly, never as floating point. That includes rationalizing a complex denominator (multiply by the conjugate over the modulus squared). Only nodes containing i fold, so ordinary real arithmetic and symbolic complex expressions like x + i are left untouched.",
        "steps": [
          {
            "code": "simplify (2+3i)+(1-i)",
            "note": "2*i + 3"
          },
          {
            "code": "simplify (2+3i)-(4-i)",
            "note": "4*i - 2"
          },
          {
            "code": "simplify (2+3i)*(1-i)",
            "note": "i + 5"
          },
          {
            "code": "simplify 1/(1+i)",
            "note": "-i/2 + 1/2 — denominator rationalized"
          },
          {
            "code": "simplify (3+i)/(1-i)",
            "note": "2*i + 1"
          },
          {
            "code": "simplify 1/(3+4i)",
            "note": "-4*i/25 + 3/25"
          }
        ]
      },
      {
        "title": "Integer powers of a complex number",
        "why": "Raising a + b*i to an integer power folds through the same canonical form using square-and-multiply, so both small and huge exponents stay exact and fast. simplify and expand agree on the normal form. The conjugate product (1+i)(1-i) is a quick way to see that z times its conjugate is the real |z|^2.",
        "steps": [
          {
            "code": "simplify (1+i)^2",
            "note": "2*i"
          },
          {
            "code": "simplify (1+i)^8",
            "note": "16"
          },
          {
            "code": "simplify (1+i)^10",
            "note": "32*i"
          },
          {
            "code": "expand (1+i)^2",
            "note": "2*i — expand agrees with simplify"
          },
          {
            "code": "simplify (1+i)*(1-i)",
            "note": "2 — z times conj(z) is real (= |z|^2)"
          }
        ]
      },
      {
        "title": "Numeric evaluation and Euler's formula",
        "why": "eval routes to the complex evaluator whenever the expression contains i, printing a + b*i with near-integer results snapped clean — this is where e^(i*pi) comes out as a clean -1. Use it to get a numeric value rather than a symbolic form. Gotcha: an expression with no i stays on the real path, so eval sqrt(-1) still reports the real-domain error; write the i explicitly to work over ℂ.",
        "steps": [
          {
            "code": "eval (2+3i)*(1-i)",
            "note": "5 + i"
          },
          {
            "code": "eval e^(i*pi)",
            "note": "-1"
          },
          {
            "code": "eval e^(i*pi/2)",
            "note": "i"
          },
          {
            "code": "eval e^(i*pi)+1",
            "note": "0 — Euler's identity"
          },
          {
            "code": "eval e^(i*pi/4)",
            "note": "0.7071... + 0.7071...*i"
          },
          {
            "code": "eval sqrt(-1)",
            "note": "error by design — no i present, so it stays real"
          }
        ]
      },
      {
        "title": "The complex functions: conj, Re, Im, abs",
        "why": "conj, Re, Im, and abs are first-class. On a numeric complex argument simplify folds them exactly — and abs returns an exact radical, not a decimal. A symbolic argument such as conj(x) is deliberately left unevaluated, because a bare symbol is not assumed to be real. Gotcha: simplify does NOT fold arg (it returns arg(i + 1) unevaluated) — reach for eval to get a numeric argument.",
        "steps": [
          {
            "code": "simplify conj(2+3i)",
            "note": "-3*i + 2"
          },
          {
            "code": "simplify Re(2+3i)",
            "note": "2"
          },
          {
            "code": "simplify Im(2+3i)",
            "note": "3"
          },
          {
            "code": "simplify abs(3+4i)",
            "note": "5"
          },
          {
            "code": "simplify abs(2+3i)",
            "note": "sqrt(13) — exact modulus"
          },
          {
            "code": "simplify conj(x)",
            "note": "conj(x) — symbolic argument untouched"
          },
          {
            "code": "eval arg(1+i)",
            "note": "0.7853... (= pi/4); arg needs eval, not simplify"
          }
        ]
      },
      {
        "title": "Modulus, argument, and polar form (De Moivre)",
        "why": "abs and arg give the polar coordinates (r, theta) of a complex number, and r*e^(i*theta) rebuilds the rectangular form. Use eval for the numeric modulus and argument, then to confirm De Moivre's theorem and locate roots of unity on the unit circle. Gotcha: rebuilt rectangular values carry floating-point dust (you get 0.9999...*i, not an exact 1*i), so read them as approximate.",
        "steps": [
          {
            "code": "eval abs(1+i)",
            "note": "1.4142... — the modulus r = sqrt(2)"
          },
          {
            "code": "eval arg(1+i)",
            "note": "0.7853... — the argument theta = pi/4"
          },
          {
            "code": "eval sqrt(2)*e^(i*pi/4)",
            "note": "1 + 0.9999...*i — polar back to (approximately) 1 + i"
          },
          {
            "code": "eval (cos(pi/3)+i*sin(pi/3))^3",
            "note": "-1 — De Moivre: (e^(i*pi/3))^3 = e^(i*pi)"
          },
          {
            "code": "eval e^(i*2*pi/3)",
            "note": "-0.5 + 0.866...*i — a primitive cube root of unity"
          }
        ]
      },
      {
        "title": "Transcendental functions over ℂ",
        "why": "The elementary, hyperbolic, and inverse functions all evaluate on the principal branch through their complex overloads, so you can check identities that connect trig and hyperbolic functions across ℂ. Handy for verifying cos(i) = cosh(1), sin(i) = i*sinh(1), and ln(i) = i*pi/2. As a bonus, i^i is a real number.",
        "steps": [
          {
            "code": "eval cos(i)",
            "note": "1.5430... (= cosh 1)"
          },
          {
            "code": "eval sin(i)",
            "note": "1.1752...*i (= i*sinh 1)"
          },
          {
            "code": "eval tan(i)",
            "note": "0.7615...*i (= i*tanh 1)"
          },
          {
            "code": "eval ln(i)",
            "note": "1.5707...*i (= i*pi/2, principal branch)"
          },
          {
            "code": "eval i^i",
            "note": "0.2078... — real (= e^(-pi/2))"
          }
        ]
      },
      {
        "title": "Complex roots from solve",
        "why": "When a real quadratic has a negative discriminant, solve reports the exact conjugate pair of complex roots — with radicals kept intact — instead of giving up. Reach for it on any quadratic with no real solutions. Gotcha: exact complex roots are currently a quadratic feature; higher-degree polynomials report only their real roots, so solve x^4 + 1 = 0 comes back with 'no real solutions'.",
        "steps": [
          {
            "code": "solve x^2 = -1, x",
            "note": "no real solutions; complex roots x = -i and x = i"
          },
          {
            "code": "solve x^2 + 4 = 0, x",
            "note": "x = -2*i and x = 2*i"
          },
          {
            "code": "solve x^2 - 2x + 5 = 0, x",
            "note": "x = -2*i + 1 and x = 2*i + 1 (i.e. 1 ∓ 2i)"
          },
          {
            "code": "solve x^2 + x + 1 = 0, x",
            "note": "x = -i*sqrt(3)/2 - 1/2 and x = i*sqrt(3)/2 - 1/2 — radicals preserved"
          },
          {
            "code": "solve x^4 + 1 = 0, x",
            "note": "no real solutions — quartic falls back to real roots only"
          }
        ]
      },
      {
        "title": "Bind a complex variable and probe it (multi-step)",
        "why": "Assign a complex number to a session variable with := once, then explore it over later lines — simplify and eval substitute the bound value first, so a single z can carry a full a+b*i. This is the cleanest way to read off |z|^2, modulus, argument, and conjugate of one number. Gotcha: eval's inline bindings (z=1+i) only accept plain numbers and reject a complex value ('1+i' is not a number) — bind with := instead, or use subs to inject a complex value into an expression.",
        "steps": [
          {
            "code": "z := 2 + 3i",
            "note": "bind once; stored canonically as 3*i + 2"
          },
          {
            "code": "simplify z*conj(z)",
            "note": "13 — z*conj(z) = |z|^2, a real number"
          },
          {
            "code": "eval abs(z)",
            "note": "3.6055... (= sqrt(13))"
          },
          {
            "code": "eval arg(z)",
            "note": "0.9827... — the argument of z"
          },
          {
            "code": "simplify conj(z)",
            "note": "-3*i + 2"
          },
          {
            "code": "subs (2+3i)*w, w=1-i",
            "note": "i + 5 — subs injects a complex value inline"
          }
        ]
      }
    ]
  },
  {
    "id": "variables-notebooks",
    "title": "Session Variables & Notebooks",
    "blurb": "The := environment turns the console into a workspace: bind numbers, expressions, and whole equations to names, chain them together, and let later lines resolve them lazily. Notebooks capture a session's command lines as a replayable script that you can save, reload, and re-run top-to-bottom in its own isolated scope.",
    "recipes": [
      {
        "title": "Bind a value and reuse it",
        "why": "`name := value` stores a binding that is substituted into every LATER line automatically, so you set a constant once and refer to it by name everywhere after. Assignments echo the binding; computing verbs see the resolved input. This is the foundation for everything else in this section.",
        "steps": [
          {
            "code": "a := 2",
            "note": "binds a; applies to later lines only"
          },
          {
            "code": "x^2 + a",
            "note": "resolves to x^2 + 2, then simplifies"
          },
          {
            "code": "diff x^2 + a, x",
            "note": "a is a constant here, so d/dx = 2*x"
          },
          {
            "code": "vars",
            "note": "lists every binding in definition order"
          }
        ]
      },
      {
        "title": "Lazy binding — definitions apply only to later lines",
        "why": "Bindings resolve at use time and only affect lines typed AFTER the definition, so order matters in a recipe. A name used before it is bound simply stays a free symbol. Reach for this mental model whenever a value 'didn't take' — you probably defined it below where you used it.",
        "steps": [
          {
            "code": "x + c",
            "note": "c is undefined here, so it stays a free symbol: c + x"
          },
          {
            "code": "c := 5",
            "note": "now c is bound"
          },
          {
            "code": "x + c",
            "note": "the same line now resolves to x + 5"
          }
        ]
      },
      {
        "title": "Naming rules — a target must be ONE symbol",
        "why": "An assignment target must lex as exactly one symbol: a single letter (a, A, r), a Greek name (alpha), or a subscripted name (v_0, E_1). A bare multi-letter word is never one symbol — most error outright as an 'unknown name' (area, xyz), while some short runs silently read as a product (ab = a*b) — and a letter glued to digits reads as a product too (E1 = E*1); all are rejected with a hint. The web console conveniently braces a multi-letter subscript for you, so x_max is stored as x_{max}.",
        "steps": [
          {
            "code": "alpha := 3",
            "note": "Greek name — valid"
          },
          {
            "code": "v_0 := 5",
            "note": "subscripted — valid"
          },
          {
            "code": "area := 3",
            "note": "REJECTED — 'unknown name': not one symbol; the hint says write the product a*r*e*a explicitly"
          },
          {
            "code": "A := 3",
            "note": "the fix — a single letter is one symbol"
          },
          {
            "code": "E1 := 10",
            "note": "REJECTED — reads as E*1; the error suggests E_1"
          },
          {
            "code": "x_max := 100",
            "note": "multi-letter subscript — the console stores it as x_{max}"
          }
        ]
      },
      {
        "title": "Redefinition — last definition wins",
        "why": "Assigning a name that already exists replaces its binding in place; there is no separate 'update' step. Because resolution is lazy, anything downstream that references the name picks up the new value on its next evaluation. Note the one restriction: a name cannot be defined in terms of itself (k := k + 1 is rejected as a self-cycle) — assign a fresh value instead.",
        "steps": [
          {
            "code": "k := 2"
          },
          {
            "code": "k*x",
            "note": "2*x"
          },
          {
            "code": "k := 10",
            "note": "replaces the old binding"
          },
          {
            "code": "k*x",
            "note": "now 10*x"
          }
        ]
      },
      {
        "title": "Derived variables — chains resolve parents-first",
        "why": "A binding's value may reference other bindings, forming a dependency chain that resolution walks parents-first. Because the chain is stored lazily (not pre-computed), redefining an upstream variable ripples through to everything built on it. Circular definitions (a := b together with b := a) are caught and rejected the moment you create the cycle.",
        "steps": [
          {
            "code": "r := 3"
          },
          {
            "code": "A := pi*r^2",
            "note": "A depends on r, stored as pi*r^2"
          },
          {
            "code": "A",
            "note": "resolves through r to 9*pi"
          },
          {
            "code": "r := 5",
            "note": "change the upstream variable"
          },
          {
            "code": "A",
            "note": "A follows automatically: 25*pi"
          }
        ]
      },
      {
        "title": "Equations as values",
        "why": "A binding's value can be a whole equation, not just an expression — E_1 := x + y = 3 stores the equation under a name. An equation name expands only where an equation belongs: as a complete ;-segment on the solve path, so solve E_1; E_2, x, y rebuilds and solves the system. It is not a value you can compute with — folding an equation name into a larger expression (solve E_1 + 2, x, or building it into another binding's value) is a deliberate error: 'E_1 names an equation and cannot be used inside an expression'.",
        "steps": [
          {
            "code": "E_1 := x + y = 3",
            "note": "value is an equation, not an expression"
          },
          {
            "code": "E_2 := x - y = 1"
          },
          {
            "code": "solve E_1; E_2, x, y",
            "note": "names expand to the stored equations: x = 2, y = 1"
          }
        ]
      },
      {
        "title": "Inspect and tidy the environment",
        "why": "vars lists your bindings in definition order, unset removes one by name, and clear wipes them all. Use vars to audit what a line will resolve against, and unset/clear to reset before starting a new problem so stale bindings don't silently rewrite your input. unset takes the name as you'd type it (unset x_max works for the stored x_{max}).",
        "steps": [
          {
            "code": "vars",
            "note": "review current bindings"
          },
          {
            "code": "unset k",
            "note": "remove a single binding by name"
          },
          {
            "code": "clear",
            "note": "remove every binding; vars then reports 'no variables set'"
          }
        ]
      },
      {
        "title": "A parametric problem, end to end",
        "why": "The payoff of the := environment: build a symbolic object once, then attack it with several verbs. Keeping the coefficients in variables makes the whole family of polynomials one edit away, and a named expression carries them lazily into every downstream verb. Note that to solve you must name the variable to solve FOR (solve p = 0, x), since p itself is a bound name.",
        "steps": [
          {
            "code": "a := 1"
          },
          {
            "code": "b := -5"
          },
          {
            "code": "c := 6"
          },
          {
            "code": "p := a*x^2 + b*x + c",
            "note": "named polynomial over the coefficients"
          },
          {
            "code": "p",
            "note": "resolves to x^2 - 5*x + 6"
          },
          {
            "code": "factor p",
            "note": "(x - 3)*(x - 2)"
          },
          {
            "code": "solve p = 0, x",
            "note": "solve for x (not the name p): x = 2, x = 3"
          },
          {
            "code": "diff p, x",
            "note": "2*x - 5"
          },
          {
            "code": "b := -7",
            "note": "retune one coefficient..."
          },
          {
            "code": "p",
            "note": "...and p follows: x^2 - 7*x + 6"
          }
        ]
      },
      {
        "title": "Save a session as a notebook",
        "why": "save <name> captures the session's command LINES (not their results) as a named, replayable notebook; notebooks lists what you've stored, and open <name> loads a notebook's lines back into the console without running them, ready to edit. Names are 1-40 characters of letters, digits, '_' or '-'. The document verbs themselves (save/open/run/notebooks) are never written into a notebook.",
        "steps": [
          {
            "code": "save quadratic-demo",
            "note": "stores the current command lines"
          },
          {
            "code": "notebooks",
            "note": "lists saved notebooks and their command counts"
          },
          {
            "code": "open quadratic-demo",
            "note": "reloads the lines into the console, unevaluated"
          }
        ]
      },
      {
        "title": "Re-run a notebook in a fresh, isolated scope",
        "why": "run <name> executes a saved notebook top-to-bottom in a throwaway scope: its := bindings live only for that run and never touch your session variables, so a notebook behaves like a script or function rather than a snapshot of state. Reach for it to reproduce a result cleanly, or to run a parametric template repeatedly without polluting your workspace. After the run, vars shows your session environment exactly as it was.",
        "steps": [
          {
            "code": "clear",
            "note": "empty the session environment first, to prove isolation"
          },
          {
            "code": "run quadratic-demo",
            "note": "runs every line in a fresh scope; session vars untouched"
          },
          {
            "code": "vars",
            "note": "still 'no variables set' — the run's a, b, c, p never leaked out"
          }
        ]
      }
    ]
  },
  {
    "id": "plotting-vector",
    "title": "Plotting & Vector Calculus",
    "blurb": "Turn expressions into pictures with plot and vecfield, and work the differential operators of vector calculus — grad, div, curl, laplacian, jacobian, hessian. Recipes build from a single curve to quiver fields, and show how to chain a symbolic result into a plot and how to sanity-check your algebra against the classic identities (curl of a gradient = 0, div of a curl = 0).",
    "recipes": [
      {
        "title": "Plot a curve over a range",
        "why": "plot samples an expression and charts it. With no bounds it uses -10..10 with 400 sample points; add two comma-separated numbers to set your own window as lo, hi. The expression must have at most one free variable (it becomes the x-axis) — feed it an equation and plot refuses, telling you to use solve instead.",
        "steps": [
          {
            "code": "plot sin(x)/x",
            "note": "default window -10..10, 400 samples"
          },
          {
            "code": "plot sin(x)/x, -20, 20",
            "note": "custom bounds lo, hi; the removable dip at x=0 renders cleanly"
          },
          {
            "code": "plot x^3 - 3x, -3, 3",
            "note": "a cubic with its two turning points in view"
          }
        ]
      },
      {
        "title": "Plot functions with poles (1/x, tan)",
        "why": "Rational and trig functions with vertical asymptotes still plot: near a pole the sampled value explodes and the chart auto-scales to it, so branches look like near-vertical spikes. The 400-point grid rarely lands on an irrational pole (like ±pi/2), so you get the two diverging branches instead of a gap. Narrow the window to study one branch, and pick bounds that don't put a pole on an exact sample point.",
        "steps": [
          {
            "code": "plot 1/x, -5, 5",
            "note": "asymptote at x=0; left and right branches shoot opposite ways"
          },
          {
            "code": "plot 1/(x^2 - 1), -2.5, 2.5",
            "note": "two poles, at x = -1 and x = 1"
          },
          {
            "code": "plot tan(x), -3, 3",
            "note": "poles at ±pi/2 appear as tall spikes"
          },
          {
            "code": "plot tan(x), -1.4, 1.4",
            "note": "stay inside one branch to see the true S-shape"
          }
        ]
      },
      {
        "title": "Use constants in plot bounds",
        "why": "Bounds don't have to be plain numbers — each is evaluated first, so constant expressions like pi and 2pi work and you can frame a period exactly instead of eyeballing 6.28. Handy for trig, where the natural window is a multiple of pi. The only rule is that lo evaluates strictly below hi.",
        "steps": [
          {
            "code": "plot sin(x), 0, 2pi",
            "note": "one full period, framed exactly (2pi = 6.2832)"
          },
          {
            "code": "plot cos(x)^2, 0, pi",
            "note": "bounds evaluate the constant pi before sampling"
          }
        ]
      },
      {
        "title": "Store an expression with := and plot it",
        "why": "An assignment binds a reusable expression that applies to LATER lines only (it's lazy, so order matters). Naming a curve once lets you plot it and reuse it without retyping — plot substitutes every session binding before sampling. A numeric := also lets you pin a second symbol so plot is left with a single free variable.",
        "steps": [
          {
            "code": "f := sin(x)/x",
            "note": "bind f for the lines below"
          },
          {
            "code": "plot f, -20, 20",
            "note": "plot substitutes f before sampling"
          },
          {
            "code": "a := 2",
            "note": "pin a parameter (must come before it is used)"
          },
          {
            "code": "plot a*sin(x), -10, 10",
            "note": "a resolves to 2, so only x is free — plot is happy"
          }
        ]
      },
      {
        "title": "Chain a computation into a plot",
        "why": "plot won't run diff for you, but you can compute a result symbolically and paste the printed expression straight into plot. Plotting a function beside its derivative makes the relationship visible: the derivative crosses zero exactly where the original has a turning point. This copy-the-result-forward pattern works for any verb (integrate, series, ilaplace, …).",
        "steps": [
          {
            "code": "diff sin(x^2), x",
            "note": "returns 2*x*cos(x^2)"
          },
          {
            "code": "plot sin(x^2), -5, 5",
            "note": "the original wiggling function"
          },
          {
            "code": "plot 2x*cos(x^2), -5, 5",
            "note": "paste the derivative back in; its zeros mark the peaks above"
          }
        ]
      },
      {
        "title": "Gradient, divergence, and curl",
        "why": "The three first-order operators. grad takes a single scalar field and returns a vector; div and curl take a vector field written as ';'-separated components, followed by the coordinate variables. Mind the punctuation: ';' separates the field's components, ',' separates the arguments. curl is dimension-aware — three components give a vector, two components give the scalar (z-component) curl.",
        "steps": [
          {
            "code": "grad x^2 + y^2, x, y",
            "note": "∇f = (2*x, 2*y)"
          },
          {
            "code": "div x*y; y*z; z*x, x, y, z",
            "note": "∇·F = x + y + z"
          },
          {
            "code": "curl -y; x; 0, x, y, z",
            "note": "3-D curl of solid rotation = (0, 0, 2)"
          },
          {
            "code": "curl -y; x, x, y",
            "note": "same field in 2-D returns the scalar curl 2"
          }
        ]
      },
      {
        "title": "Laplacian, Jacobian, and Hessian",
        "why": "The second-order and matrix operators. laplacian and hessian act on a scalar field; jacobian acts on a vector field (its rows are the gradients of each component). The Laplacian is the trace of the Hessian, and a field with Laplacian 0 is harmonic. The Jacobian of a coordinate map is the classic change-of-variables object — for polar coordinates its determinant is r.",
        "steps": [
          {
            "code": "laplacian x^2 + y^2 + z^2, x, y, z",
            "note": "∇²f = 6"
          },
          {
            "code": "laplacian x^2 - y^2, x, y",
            "note": "returns 0 — this f is harmonic"
          },
          {
            "code": "hessian x^2*y + y^3, x, y",
            "note": "symmetric 2×2 [2*y, 2*x; 2*x, 6*y]"
          },
          {
            "code": "jacobian x*y; y*z; z*x, x, y, z",
            "note": "∂F_i/∂x_j = [y, x, 0; 0, z, y; z, 0, x]"
          },
          {
            "code": "jacobian r*cos(t); r*sin(t), r, t",
            "note": "polar map: [cos(t), -r*sin(t); sin(t), r*cos(t)] (det = r)"
          }
        ]
      },
      {
        "title": "Verify the classic vector identities",
        "why": "The centerpiece workflow: chain operators by hand, copying one command's printed output into the next, to confirm the identities every vector-calculus course proves. curl of a gradient is always the zero vector; divergence of a curl is always zero; the Laplacian equals div of grad; and the Jacobian of a gradient is the Hessian. Great for sanity-checking your own derivations.",
        "steps": [
          {
            "code": "grad x^2*y + y*z, x, y, z",
            "note": "∇f = (2*x*y, x^2 + z, y)"
          },
          {
            "code": "curl 2x*y; x^2 + z; y, x, y, z",
            "note": "curl of that gradient = (0, 0, 0) ✓"
          },
          {
            "code": "curl x*y*z; x^2*z; y*z^2, x, y, z",
            "note": "∇×F = (z^2 - x^2, x*y, x*z)"
          },
          {
            "code": "div z^2 - x^2; x*y; x*z, x, y, z",
            "note": "div of that curl = 0 ✓"
          },
          {
            "code": "laplacian x^2*y + y^3, x, y",
            "note": "∇²f = 8*y ..."
          },
          {
            "code": "grad x^2*y + y^3, x, y",
            "note": "∇f = (2*x*y, x^2 + 3*y^2) ..."
          },
          {
            "code": "div 2x*y; x^2 + 3y^2, x, y",
            "note": "... div of the gradient = 8*y, matching the Laplacian ✓"
          },
          {
            "code": "grad x^3 + x*y^2, x, y",
            "note": "∇f = (y^2 + 3*x^2, 2*x*y) ..."
          },
          {
            "code": "jacobian y^2 + 3x^2; 2x*y, x, y",
            "note": "Jacobian of the gradient = [6*x, 2*y; 2*y, 2*x] ..."
          },
          {
            "code": "hessian x^3 + x*y^2, x, y",
            "note": "... equals the Hessian directly ✓"
          }
        ]
      },
      {
        "title": "Quiver plots with vecfield",
        "why": "vecfield draws a planar vector field as arrows over the x,y plane. Write the two components as Fx; Fy (';'-separated); with no bounds it samples the box -2..2 in both x and y. Append four numbers — xlo, xhi, ylo, yhi (with xlo < xhi and ylo < yhi) — to reframe; partial or malformed bounds are an error, not a silent default. Compare the archetypes: a swirl, an outward source, and an inward sink.",
        "steps": [
          {
            "code": "vecfield -y; x",
            "note": "solid rotation — arrows circulate counterclockwise"
          },
          {
            "code": "vecfield x; y",
            "note": "a source — arrows point radially outward"
          },
          {
            "code": "vecfield -x; -y",
            "note": "a sink — arrows point inward"
          },
          {
            "code": "vecfield -y; x, -3, 3, -3, 3",
            "note": "same swirl on a wider custom window"
          }
        ]
      },
      {
        "title": "Read a field's character: quiver + div + curl",
        "why": "The picture shows the shape; div and curl put numbers on it. Draw the field, then measure it: a nonzero curl means local rotation, a nonzero divergence means net expansion. Doing both makes the intuition stick — the swirl is incompressible but rotating, the source spreads but doesn't rotate.",
        "steps": [
          {
            "code": "vecfield -y; x",
            "note": "looks like it's spinning"
          },
          {
            "code": "curl -y; x, x, y",
            "note": "scalar curl = 2 → constant counterclockwise rotation"
          },
          {
            "code": "div -y; x, x, y",
            "note": "divergence = 0 → incompressible, no source"
          },
          {
            "code": "vecfield x; y",
            "note": "looks like it's spreading"
          },
          {
            "code": "div x; y, x, y",
            "note": "divergence = 2 → a uniform source"
          },
          {
            "code": "curl x; y, x, y",
            "note": "curl = 0 → irrotational"
          }
        ]
      }
    ]
  },
  {
    "id": "dsp",
    "title": "Signal Processing: Filter Design with the dsp Plugin",
    "blurb": "The dsp plugin turns the console into a filter-design bench. It designs IIR filters (Butterworth, Chebyshev I/II, elliptic), linear-phase FIR filters (windowed-sinc and optimal Parks-McClellan equiripple), and analyzes any biquad cascade you hand it. Each command returns a design summary with measured edge gains, a ready-to-ship biquad or tap table, and response charts: the IIR designs plot magnitude, phase, and time response; the linear-phase FIR designs plot magnitude and time response; and dsp.freqz plots a full magnitude, phase, group-delay, and time view. Every command is namespaced dsp.* and takes comma-separated args with no quotes. Two rules apply everywhere: every frequency must sit strictly inside (0, fs/2), and IIR order runs 1-12. Pick a family by what you can trade away: Butterworth for flatness and clean transients, Chebyshev/elliptic for a steeper skirt at the cost of ripple, and FIR when you need exactly linear phase.",
    "recipes": [
      {
        "title": "Your first filter: a Butterworth low-pass",
        "why": "This is the argument shape every IIR family shares: type, order, cutoff, sample rate. The output bundles a summary (with the *measured* gain at the edge), the biquad cascade you'd actually ship, and magnitude/phase/time charts. Butterworth is the maximally-flat default -- no ripple anywhere, the gentlest rolloff, and the cleanest step response -- so reach for it when passband flatness and transient behavior matter more than a razor-sharp skirt. Gotcha: the cutoff is the -3.01 dB point, and it must lie strictly inside (0, fs/2).",
        "steps": [
          {
            "code": "dsp.butter lowpass, 4, 1000, 48000",
            "note": "order-4 LP, -3.01 dB at 1 kHz, 48 kHz rate; folds into 2 biquad sections"
          },
          {
            "code": "dsp.butter lowpass, 8, 1000, 48000",
            "note": "double the order -> twice the sections (4) and twice the asymptotic rolloff slope"
          }
        ]
      },
      {
        "title": "The four response shapes from one family",
        "why": "The same family designs low-pass, high-pass, band-pass and band-stop just by changing the type word (aliases: lp, hp, bp, bs, notch). Band types take two edge frequencies f1 < f2 instead of a single cutoff and internally use twice the poles, so sections = order for a band design. Reach here once you know which part of the spectrum to keep; the -3.01 dB marks land on both edges for band types.",
        "steps": [
          {
            "code": "dsp.butter highpass, 4, 2000, 44100",
            "note": "passband above 2 kHz; gain is 0 dB at Nyquist"
          },
          {
            "code": "dsp.butter bandpass, 3, 500, 2000, 48000",
            "note": "two edges; -3.01 dB at both 500 and 2000 Hz, ~0 dB at the geometric center; order 3 -> 3 sections"
          },
          {
            "code": "dsp.butter bandstop, 3, 500, 2000, 48000",
            "note": "rejects 500-2000 Hz, passes everything outside"
          },
          {
            "code": "dsp.butter notch, 2, 500, 2000, 48000",
            "note": "'notch' is an accepted alias for bandstop (so is bs / bandreject)"
          }
        ]
      },
      {
        "title": "Sharper skirts: Chebyshev I, Chebyshev II, and elliptic",
        "why": "When Butterworth would need an impractically high order, trade flatness for steepness. Chebyshev I ripples the passband (you pass the ripple in dB) to steepen the skirt -- its band edge sits at -ripple dB, not -3 dB. Chebyshev II keeps the passband flat and ripples the stopband instead (you pass the stopband attenuation); its edge is the -atten dB point. Elliptic (Cauer) ripples *both* bands and delivers the sharpest transition for a given order -- the lowest order that meets any spec -- at the cost of the worst phase/transient behavior. Ripple must be in (0, 12] dB and attenuation in [10, 120] dB; for elliptic the attenuation must exceed the ripple.",
        "steps": [
          {
            "code": "dsp.cheby1 lowpass, 5, 1, 1000, 48000",
            "note": "1 dB passband ripple buys a steeper skirt than Butterworth order 5; edge = -1 dB"
          },
          {
            "code": "dsp.cheby2 lowpass, 4, 40, 2000, 48000",
            "note": "flat passband, equiripple stopband floor at -40 dB; the cutoff is the -40 dB point"
          },
          {
            "code": "dsp.ellip lowpass, 5, 1, 60, 1000, 48000",
            "note": "pass both ripple (1 dB) and atten (60 dB): sharpest transition per order; edge = -1 dB"
          },
          {
            "code": "dsp.ellip bandpass, 4, 1, 60, 500, 2000, 48000",
            "note": "elliptic band-pass: order, ripple, atten, then f1 f2, then fs"
          }
        ]
      },
      {
        "title": "Linear-phase FIR and choosing a window",
        "why": "FIR windowed-sinc filters are always stable and have exactly linear phase -- a constant group delay of (taps-1)/2 samples, so every frequency is delayed equally and waveforms keep their shape (crucial for audio and comms). The window sets the stopband-depth vs transition-width trade: rectangular is sharpest but leaks (peak sidelobe only ~-21 dB, visible Gibbs ringing in the time chart), Hann/Hamming are the workhorses (~-44 / -53 dB), Blackman goes deeper (~-74 dB) with a wider skirt. Gotchas: taps must be in [5, 255]; the cutoff is the -6 dB (half-amplitude) point by the windowed-sinc convention, not -3 dB; and high-pass/band-stop require an ODD tap count.",
        "steps": [
          {
            "code": "dsp.fir lowpass, 101, 1000, 48000",
            "note": "default window is Hamming; 101 taps -> 50-sample linear-phase group delay"
          },
          {
            "code": "dsp.fir lowpass, 101, 1000, 48000, rect",
            "note": "rectangular: sharpest transition but only ~-21 dB stopband (Gibbs ringing)"
          },
          {
            "code": "dsp.fir lowpass, 101, 1000, 48000, blackman",
            "note": "Blackman: ~-74 dB stopband at the price of a wider transition band"
          },
          {
            "code": "dsp.fir highpass, 101, 2000, 48000, hann",
            "note": "HP (and BS) need odd taps for a type-I linear-phase filter"
          }
        ]
      },
      {
        "title": "Kaiser: one knob for the whole trade-off",
        "why": "The Kaiser window is the tunable one: a single shape parameter beta (0-30) slides continuously from a rectangular-like sharp-but-leaky window (low beta) to a deep-stopband, wide-transition window (high beta), getting near-optimal stopband depth for a given length. Rules of thumb: beta ~0 is rectangular, ~5 is Hamming-class, ~8.6 is Blackman-class, ~10+ reaches -70 dB and beyond. Gotcha: a beta argument is accepted only with the kaiser window -- pass it to any other window and the command errors.",
        "steps": [
          {
            "code": "dsp.fir lowpass, 101, 1000, 48000, kaiser, 5",
            "note": "beta 5 ~ Hamming-class stopband"
          },
          {
            "code": "dsp.fir lowpass, 101, 1000, 48000, kaiser, 10",
            "note": "beta 10 -> ~-70 dB stopband, noticeably wider skirt"
          },
          {
            "code": "dsp.fir bandpass, 121, 2000, 6000, 48000, kaiser, 8.6",
            "note": "band-pass: two edges first, then the window name and beta"
          }
        ]
      },
      {
        "title": "Optimal equiripple FIR (Parks-McClellan)",
        "why": "dsp.remez designs the shortest FIR that meets a spec by spreading the approximation error equally across each band (equiripple) -- for the same length and transition it beats any windowed-sinc's peak ripple. You specify the actual band edges (the transition region), so the ordering depends on type: lowpass = fpass, fstop; highpass = fstop, fpass; bandpass = fstop1, fpass1, fpass2, fstop2; bandstop = fpass1, fstop1, fstop2, fpass2. An optional final stop-weight (in (0, 1000]) makes the stopband ripple that many times smaller (deeper) at the cost of more passband ripple. Gotchas: taps must be ODD, and every edge must be strictly increasing and inside (0, fs/2).",
        "steps": [
          {
            "code": "dsp.remez lowpass, 31, 1000, 1500, 8000",
            "note": "pass to 1 kHz, stop from 1.5 kHz; summary reports ~0.1 dB ripple, ~-39 dB stopband"
          },
          {
            "code": "dsp.remez lowpass, 31, 1000, 1500, 8000, 10",
            "note": "stop-weight 10 deepens the stopband (~-47 dB) by trading away passband ripple (~0.37 dB)"
          },
          {
            "code": "dsp.remez highpass, 31, 1000, 1500, 8000",
            "note": "highpass edges are fstop then fpass -- still strictly increasing"
          },
          {
            "code": "dsp.remez bandpass, 41, 800, 1200, 2800, 3200, 8000",
            "note": "four edges: fstop1, fpass1, fpass2, fstop2"
          }
        ]
      },
      {
        "title": "Analyze any biquad cascade with freqz",
        "why": "dsp.freqz takes coefficients you already have -- pasted from a design's biquad table, exported from another tool, or written by hand -- and plots magnitude, phase, group delay, and the impulse/step response. Coefficients come in groups of five per biquad: b0, b1, b2, a1, a2 (a0 is assumed 1). Append more groups of five to cascade sections (up to 16). Reach for it to sanity-check a filter, compare two designs' phase, or inspect group delay -- the one curve the IIR design view does not draw.",
        "steps": [
          {
            "code": "dsp.freqz 48000, 0.2, 0.4, 0.2, -0.5, 0.3",
            "note": "one biquad; note the group-delay chart that freqz adds over the design view"
          },
          {
            "code": "dsp.freqz 48000, 0.003916, 0.007832, 0.003916, -1.815341, 0.831006",
            "note": "an order-2 Butterworth LP section entered by hand"
          },
          {
            "code": "dsp.freqz 48000, 0.003916, 0.007832, 0.003916, -1.815341, 0.831006, 0.003916, 0.007832, 0.003916, -1.815341, 0.831006",
            "note": "two identical sections cascaded -> doubled rolloff"
          }
        ]
      },
      {
        "title": "Workflow: pin the rate, design, then re-analyze",
        "why": "A realistic session chains three verbs. Pin the sample rate in a := variable so it can't drift between lines, design an IIR filter, then copy a biquad row from its cascade table straight into freqz to inspect group delay (which the IIR design view omits) and to build a longer cascade. Session variables are lazy -- they bind only on LATER lines -- so assign the rate before you use it. Gotcha: assignment targets must be a single variable name (use r, not fs -- 'fs' parses as the product f*s).",
        "steps": [
          {
            "code": "r := 48000",
            "note": "pin the sample rate once; every later line resolves r to 48000"
          },
          {
            "code": "dsp.butter lowpass, 2, 1000, r",
            "note": "order-2 LP at rate r; read the single biquad row off the cascade table"
          },
          {
            "code": "dsp.freqz r, 0.003916, 0.007832, 0.003916, -1.815341, 0.831006",
            "note": "that same section re-analyzed -- now with a group-delay curve"
          },
          {
            "code": "dsp.freqz r, 0.003916, 0.007832, 0.003916, -1.815341, 0.831006, 0.003916, 0.007832, 0.003916, -1.815341, 0.831006",
            "note": "cascade two copies for a 4th-order response, still at rate r"
          }
        ]
      }
    ]
  },
  {
    "id": "control",
    "title": "Control Systems (sys plugin)",
    "blurb": "The sys plugin turns the console into an LTI-systems bench. From a transfer function H(s) or an ODE it derives poles and zeros (with damping), a stability verdict, DC gain, the pole-zero map, Bode magnitude and phase with gain and phase margins, and the step plus impulse responses — one line per question. It also closes feedback loops, sweeps root loci, discretizes H(s) to digital biquads, analyzes discrete H(z) systems in the z-plane, and simulates delay differential equations. Arguments are comma-separated polynomials in s (or z); any spelling works — factored s(s+1)(s+2) as readily as s^2 + 3s + 2 — and denominators are auto-normalized monic.",
    "recipes": [
      {
        "title": "Characterize a plant with sys.tf",
        "why": "sys.tf is the workhorse: one line returns poles/zeros with damping ratios, a stability verdict, DC gain, the pole-zero map, Bode magnitude and phase, and the step and impulse responses. Reach for it first to characterize any continuous-time plant. The denominator is normalized monic automatically, and any polynomial spelling is accepted (including factored forms and implicit multiplication like 3s).",
        "steps": [
          {
            "code": "sys.tf s+1, s^2 + 3s + 2",
            "note": "stable plant; real poles at -1 and -2, DC gain 0.5"
          },
          {
            "code": "sys.tf 100, s^2 + 2s + 100",
            "note": "resonant 2nd-order: wn = 10 rad/s, damping zeta = 0.1, overshooting step"
          },
          {
            "code": "apart 100/(s*(s^2 + 2s + 100)), s",
            "note": "CAS cross-check of the step response H(s)/s: gives 1/s (the DC term) plus (-s-2)/(s^2+2s+100), the decaying resonant mode"
          },
          {
            "code": "sys.tf 1, s(s+1)(s+2)",
            "note": "factored, type-1 plant (pole at the origin) -> DC gain reported as infinite"
          },
          {
            "code": "sys.tf 1, s^2 - 1",
            "note": "a right-half-plane pole -> verdict 'unstable'"
          },
          {
            "code": "sys.tf 1, s^2 + 1",
            "note": "poles on the jw axis -> 'marginally stable' (undamped oscillator)"
          }
        ]
      },
      {
        "title": "Build H(s) straight from an ODE",
        "why": "sys.ode takes a linear constant-coefficient differential equation in output y and input u (primes are derivatives), assumes zero initial conditions, and runs the same full analysis as sys.tf. y-terms form the denominator, u-terms the numerator. Gotchas: the equation must contain both a y term and a u term, and it must be proper — the highest input-derivative order may not exceed the output order.",
        "steps": [
          {
            "code": "sys.ode y'' + 3y' + 2y = u' + u",
            "note": "-> H(s) = (s + 1)/(s^2 + 3s + 2); the result echoes 'Derived from' the ODE"
          },
          {
            "code": "sys.ode y'' + 2y' + 100y = 100u",
            "note": "the resonant plant H(s) = 100/(s^2 + 2s + 100), now obtained from its differential equation"
          },
          {
            "code": "sys.ode y = u''",
            "note": "improper (input order 2 > output order 0) -> shows the error you get"
          }
        ]
      },
      {
        "title": "Read gain and phase margins off the Bode plot",
        "why": "Every sys.tf / sys.ode analysis also reports the classical gain margin and phase margin and marks the phase-crossover (wpc) and gain-crossover (wgc) frequencies as vertical lines on both Bode charts. Key gotcha: the margins treat the supplied H as an OPEN-LOOP L(s). To judge a feedback design, feed sys.tf the open loop L = K*G by scaling the numerator by K — do not read margins off a closed loop.",
        "steps": [
          {
            "code": "sys.tf 1, s(s+1)(s+2)",
            "note": "open loop at K=1: gain margin 15.56 dB @ 1.414 rad/s, phase margin 53.4 deg @ 0.446 rad/s"
          },
          {
            "code": "sys.tf 5, s(s+1)(s+2)",
            "note": "15.56 dB ~= a factor of 6, so raising loop gain to K=5 shrinks the margin to 1.58 dB / 5 deg"
          },
          {
            "code": "sys.tf 6, s(s+1)(s+2)",
            "note": "K=6 sits on the stability boundary: gain margin ~ 0 dB, phase margin ~ 0 deg"
          }
        ]
      },
      {
        "title": "Close the loop with unity feedback",
        "why": "sys.feedback forms the closed loop T = K*G/(1 + K*G) under unity negative feedback and analyzes T directly, so its poles have moved and its DC gain and responses are the closed-loop ones. K is optional and defaults to 1. Note that any margins reported here describe T itself; for true loop margins run the previous recipe on the open loop L = K*G.",
        "steps": [
          {
            "code": "sys.feedback 100, s^2 + 2s + 100, 1",
            "note": "close the resonant plant; poles migrate outward, closed-loop T = 100/(s^2 + 2s + 200), DC gain 0.5"
          },
          {
            "code": "sys.feedback 1, s(s+1)(s+2), 5",
            "note": "K=5 closed loop is stable, consistent with the ~6x gain headroom"
          },
          {
            "code": "sys.feedback 1, s(s+1)(s+2), 8",
            "note": "K=8 (> 6) pushes a pole into the right half-plane -> unstable"
          },
          {
            "code": "sys.feedback 1, s + 1",
            "note": "K omitted defaults to unity: T = 1/(s + 2)"
          }
        ]
      },
      {
        "title": "Root locus: where the poles go as gain grows",
        "why": "sys.rlocus sweeps the loop gain K (geometrically, over four decades up to K max) and scatters the closed-loop pole positions, overlaying the open-loop poles as x and zeros as o, then reports the smallest swept K where the locus first crosses into the right half-plane. The optional third argument sets K max (default 100). It is the go-to visual for choosing a gain and cross-checks the margin story.",
        "steps": [
          {
            "code": "sys.rlocus 1, s^3 + 3s^2 + 2s",
            "note": "verdict: closed loop goes unstable near K ~ 6.2, matching the 15.56 dB (~6x) open-loop margin"
          },
          {
            "code": "sys.rlocus s+2, s(s+1)(s+4), 200",
            "note": "a plant with a finite zero at -2; one locus branch terminates on it as K grows. K max = 200"
          },
          {
            "code": "sys.rlocus 1, (s+1)(s+2), 50",
            "note": "both poles real and in the LHP -> stable for the whole sweep"
          }
        ]
      },
      {
        "title": "Discretize H(s) to digital biquads (c2d)",
        "why": "sys.c2d maps a continuous H(s) to a cascade of digital biquads via the bilinear transform at sample rate fs, printing the b/a coefficients (a0 = 1) and overlaying the digital and analog magnitude responses. Gotcha: the bilinear transform warps frequency, so the two curves diverge toward Nyquist; pass the resulting b0,b1,b2,a1,a2 groups to dsp.freqz when you need phase and group delay.",
        "steps": [
          {
            "code": "sys.c2d 1, s+1, 100",
            "note": "one-pole lowpass at fs = 100 Hz -> a single biquad section"
          },
          {
            "code": "sys.c2d 100, s^2 + 2s + 100, 1000",
            "note": "the resonant plant at 1 kHz; watch the peak shift under warping"
          },
          {
            "code": "sys.c2d 5, s^3 + 3s^2 + 2s + 5, 200",
            "note": "a 3rd-order closed loop (3 poles) -> two biquad sections"
          }
        ]
      },
      {
        "title": "Analyze a discrete transfer function H(z) (tfz)",
        "why": "sys.tfz analyzes a system already in the z-domain, written in POSITIVE powers of z. Stability flips to the unit-circle test |pole| < 1, which the tool draws, alongside the magnitude/phase response up to Nyquist and the discrete step and impulse responses. The sample rate fs (Hz) is required and only scales the frequency axis (DC gain and stability are fs-independent).",
        "steps": [
          {
            "code": "sys.tfz z, z^2 - 0.5z + 0.06, 8000",
            "note": "poles at 0.3 and 0.2, inside the unit circle -> stable"
          },
          {
            "code": "sys.tfz 0.05z, z - 0.95, 1000",
            "note": "leaky integrator (pole at 0.95): stable, DC gain 1"
          },
          {
            "code": "sys.tfz 1, z - 1.2, 8000",
            "note": "pole at 1.2 outside the circle -> unstable"
          }
        ]
      },
      {
        "title": "Delay differential equations (dde)",
        "why": "sys.dde simulates x'(t) = f(t, x, x_d) where x_d is the delayed state x(t - tau), integrating by the method of steps (RK4). Provide the right-hand side (symbols t, x, x_d), the delay tau, the history phi(t) that defines x on [-tau, 0], and the horizon T. Delays destabilize: the same right-hand side can settle or grow depending on tau.",
        "steps": [
          {
            "code": "sys.dde -x_d, 1, 1, 20",
            "note": "x' = -x(t-1): with tau = 1 (below the pi/2 critical delay) the response damps toward 0"
          },
          {
            "code": "sys.dde -x_d, 2, 1, 30",
            "note": "same law, tau = 2 past the pi/2 ~= 1.571 critical delay -> oscillations grow, unstable"
          },
          {
            "code": "sys.dde -x + sin(t), 1, 0, 20",
            "note": "a forced non-delayed rhs (x_d unused) driven to a sinusoidal steady state of amplitude ~0.707"
          },
          {
            "code": "sys.dde x*(1 - x_d), 1, 0.5, 40",
            "note": "the delayed logistic (Hutchinson) equation: a nonlinear rhs using both x and x_d, settling to x = 1"
          }
        ]
      },
      {
        "title": "Multi-step: design a proportional controller end-to-end",
        "why": "This chains the whole toolkit on one plant G(s) = 1/(s(s+1)(s+2)): read the open-loop margin, confirm the safe gain with the root locus, bind that gain to a session variable, close the loop, verify the steady-state error with the CAS, then hand the finished design to the discrete domain. It also shows a numeric := feeding a plugin argument — only a bare identifier whose binding resolves to a pure number substitutes into plugin args (polynomials and factored forms pass through verbatim) — with the three stability tools agreeing.",
        "steps": [
          {
            "code": "sys.tf 1, s(s+1)(s+2)",
            "note": "open loop L = G: gain margin 15.56 dB (about 6x headroom), phase margin 53.4 deg"
          },
          {
            "code": "sys.rlocus 1, s(s+1)(s+2)",
            "note": "the locus confirms instability near K ~ 6.2, matching the margin"
          },
          {
            "code": "K := 5",
            "note": "pick a gain safely under the limit; the binding applies to the lines below"
          },
          {
            "code": "sys.feedback 1, s(s+1)(s+2), K",
            "note": "close the loop at K=5 -> stable T(s) = 5/(s^3 + 3s^2 + 2s + 5); K resolves from the binding"
          },
          {
            "code": "eval 5/(s^3 + 3s^2 + 2s + 5), s=0",
            "note": "CAS cross-check: T(0) = 1, i.e. zero steady-state error to a step (the type-1 loop tracks it)"
          },
          {
            "code": "sys.c2d 5, s^3 + 3s^2 + 2s + 5, 200",
            "note": "discretize the finished closed loop to biquads at 200 Hz for implementation"
          }
        ]
      }
    ]
  },
  {
    "id": "linalg",
    "title": "Linear Algebra (linalg plugin)",
    "blurb": "The linalg plugin brings dense numerical linear algebra to the console, plus an exact symbolic path for small matrices. Every command is namespaced (linalg.det, linalg.solve, ...) and takes matrix literals written with rows separated by ';' and entries by ',' or spaces, optionally bracketed: [1,2;3,4], [1 2; 3 4], and 1,2;3,4 all parse to the same 2x2. Matrices are passed as literals on each call (they are not session variables), and vectors are just one-row or one-column matrices. When every entry folds to a number, solve/det/inv take LU with partial pivoting, eig uses shifted QR, and svd/rank/lstsq share a one-sided Jacobi SVD. Symbolic entries route det (up to 5x5) through exact fraction-free Bareiss arithmetic; eig goes further, taking an exact route for every matrix up to 4x4 — numeric or symbolic — and only falling back to numeric QR beyond 4x4.",
    "recipes": [
      {
        "title": "Matrix literals and the determinant",
        "why": "The determinant is the gentlest way to learn the [rows;entries] literal and to see the two computing paths. A numeric matrix takes the LU route; the instant one entry is a symbol, det switches to exact fraction-free Bareiss elimination over expressions. Exact det is capped at 5x5 (numeric det has no size limit), so keep symbolic matrices small.",
        "steps": [
          {
            "code": "linalg.det [1,2;3,4]",
            "note": "numeric 2x2: -2. Rows split on ';', entries on ','"
          },
          {
            "code": "linalg.det [1 2; 3 4]",
            "note": "spaces separate entries too, and the brackets are optional"
          },
          {
            "code": "linalg.det [a,b;c,d]",
            "note": "any symbolic entry routes to the exact path: -b*c + a*d"
          },
          {
            "code": "linalg.det [a,b,c;0,d,e;0,0,f]",
            "note": "exact 3x3 of a triangular matrix folds to a*d*f"
          }
        ]
      },
      {
        "title": "Solve a linear system A x = b",
        "why": "linalg.solve runs LU with partial pivoting and reports both x and the residual max|Ax - b| so you can trust the answer. The right-hand side b is a vector, which the plugin accepts as either a one-row [3,5] or a one-column [3;5] literal. Singular systems are refused rather than returning garbage: fall back to linalg.lstsq for those.",
        "steps": [
          {
            "code": "linalg.solve [2,1;1,3], [3,5]",
            "note": "x = (0.8, 1.4); residual ~1e-16 confirms an essentially exact hit"
          },
          {
            "code": "linalg.solve [2,1;1,3], [3;5]",
            "note": "b as a column vector gives the same solution"
          },
          {
            "code": "linalg.solve [2,1,-1;-3,-1,2;-2,1,2], [8,-11,-3]",
            "note": "scales to n x n: x = (2, 3, -1)"
          },
          {
            "code": "linalg.solve [1,2;2,4], [1,1]",
            "note": "singular A is rejected: 'the matrix is singular (try lstsq)'"
          }
        ]
      },
      {
        "title": "Inverse and condition number",
        "why": "linalg.inv prints A^-1 as a table and, crucially, cond(A) next to it so you can judge how trustworthy that inverse is. A cond near 1 is well-conditioned; a large cond warns that the inverse amplifies error. A singular matrix has no inverse and is reported as such, so check det first when in doubt.",
        "steps": [
          {
            "code": "linalg.inv [4,7;2,6]",
            "note": "inverse plus cond(A) = 10.4"
          },
          {
            "code": "linalg.inv [2,1;1,2]",
            "note": "symmetric and well-conditioned: cond(A) = 3"
          },
          {
            "code": "linalg.inv [1,2;2,4]",
            "note": "singular: 'inv: the matrix is singular'"
          }
        ]
      },
      {
        "title": "Eigenvalues and eigenvectors, exact for small matrices",
        "why": "For matrices up to 4x4 linalg.eig takes an exact route: it builds the characteristic polynomial by Bareiss elimination, finds the roots symbolically, and returns exact eigenvectors. That means true surds and complex pairs instead of rounded decimals. It even handles fully symbolic 2x2 matrices, which is perfect for deriving formulas by hand.",
        "steps": [
          {
            "code": "linalg.eig [2,1;1,2]",
            "note": "symmetric: lambda = 1 -> (1,-1), lambda = 3 -> (1,1), with the charpoly"
          },
          {
            "code": "linalg.eig [1,1;1,0]",
            "note": "the Fibonacci matrix: exact golden-ratio surds lambda = 1/2 +- sqrt(5)/2"
          },
          {
            "code": "linalg.eig [0,-1;1,0]",
            "note": "a rotation: exact complex conjugate pair lambda = +-i"
          },
          {
            "code": "linalg.eig [a,1;1,a]",
            "note": "fully symbolic: lambda = a - 1 -> (1,-1) and a + 1 -> (1,1)"
          }
        ]
      },
      {
        "title": "Repeated eigenvalues, eigenspaces, and the numeric fallback",
        "why": "The exact 3x3/4x4 path shines on degenerate spectra: for a repeated eigenvalue it returns a full basis of the eigenspace from an exact rational null space, not just one vector. Beyond 4x4, or when the characteristic polynomial won't factor exactly, eig automatically drops to numeric QR and reports the spectral radius and trace instead. Watch the title: '(exact)' vs plain tells you which path ran.",
        "steps": [
          {
            "code": "linalg.eig [2,0,0;0,3,0;0,0,5]",
            "note": "diagonal: exact integer spectrum with standard-basis eigenvectors"
          },
          {
            "code": "linalg.eig [2,1,0;1,2,0;0,0,3]",
            "note": "lambda = 3 is repeated -> a 2-D eigenspace: (1,1,0) and (0,0,1)"
          },
          {
            "code": "linalg.eig [6,-1,0,0,0;-1,5,-1,0,0;0,-1,4,-1,0;0,0,-1,3,-1;0,0,0,-1,2]",
            "note": "5x5 exceeds the exact reach: numeric QR, spectral radius 6.74616, trace 20"
          }
        ]
      },
      {
        "title": "SVD, rank, and least squares",
        "why": "These three share the one-sided Jacobi SVD engine and answer the 'what is really going on' questions. linalg.rank gives the numeric rank; linalg.svd returns the singular values (sorted descending), rank, cond, and the U and V factors even for rectangular matrices; linalg.lstsq solves overdetermined or rank-deficient systems in the least-squares sense via the SVD pseudoinverse, printing the residual norm.",
        "steps": [
          {
            "code": "linalg.rank [1,2;2,4]",
            "note": "rank 1: the rows are dependent"
          },
          {
            "code": "linalg.svd [1,2;3,4;5,6]",
            "note": "3x2: singular values (9.52552, 0.514301), rank 2, cond, and U/V"
          },
          {
            "code": "linalg.lstsq [1,0;1,1;1,2], [1,2,4]",
            "note": "best-fit line through (0,1),(1,2),(2,4): x = (5/6, 3/2) = (0.833333, 1.5)"
          }
        ]
      },
      {
        "title": "Structured solvers: tridiagonal, Toeplitz, circulant",
        "why": "When the matrix has structure, these dedicated solvers are dramatically faster than forming a dense matrix and are what you reach for on large problems. Each takes vectors of coefficients, not a full matrix. Mind the shapes: trisolve wants n-1 sub, n diag, n-1 super, n rhs; toeplitz and circulant each take a first column and a right-hand side of equal length.",
        "steps": [
          {
            "code": "linalg.trisolve [-1,-1], [2,2,2], [-1,-1], [1,0,1]",
            "note": "the 1D Laplacian (-1,2,-1) by the Thomas algorithm, O(n): x = (1,1,1)"
          },
          {
            "code": "linalg.toeplitz [2,1,0], [3,4,3]",
            "note": "symmetric Toeplitz from its first column, Levinson recursion O(n^2): x = (1,1,1)"
          },
          {
            "code": "linalg.circulant [2,1,1], [4,4,4]",
            "note": "circulant from its first column, solved by DFT diagonalization: x = (1,1,1)"
          }
        ]
      },
      {
        "title": "Workflow: diagnose a system, then solve the right way",
        "why": "A realistic multi-step session combines verbs: probe the matrix before committing to a method. Check the determinant; if it vanishes, confirm rank deficiency, watch the exact solver refuse the system, then recover the minimum-norm answer with least squares. This same det -> inv -> solve pattern (or det -> rank -> lstsq when singular) is the backbone of most linalg work.",
        "steps": [
          {
            "code": "linalg.det [1,2;2,4]",
            "note": "determinant is 0 -> the matrix is singular, do not invert"
          },
          {
            "code": "linalg.rank [1,2;2,4]",
            "note": "rank 1 of a 2x2 confirms the rows are dependent"
          },
          {
            "code": "linalg.solve [1,2;2,4], [3,6]",
            "note": "as expected, solve refuses: 'singular (try lstsq)'"
          },
          {
            "code": "linalg.lstsq [1,2;2,4], [3,6]",
            "note": "least squares returns the minimum-norm solution x = (0.6, 1.2)"
          },
          {
            "code": "linalg.det [4,2;2,4]",
            "note": "contrast: a nonzero determinant (12) means the invertible path is safe"
          },
          {
            "code": "linalg.solve [4,2;2,4], [6,6]",
            "note": "now solve is exact: x = (1, 1), residual 0"
          }
        ]
      }
    ]
  },
  {
    "id": "pde-numerics",
    "title": "PDEs & Numerical Methods",
    "blurb": "Four plugins turn the console into a partial-differential-equation lab: pde solves the classic heat and wave equations by separation of variables (and marches nonlinear reaction-diffusion in time by the method of lines), fem runs 1-D finite elements for boundary-value problems and Sturm-Liouville eigenmodes, ie solves second-kind Fredholm and Volterra integral equations, and hyb simulates event-driven hybrid systems with guards and reset maps. Every command returns a titled result with key-value facts, usually a table (coefficients, samples, or events), and a plot, so you can read the physics at a glance. Plugin arguments are comma-separated with no quotes; expressions may use pi and exact fractions, and the dynamics/reset pairs in hyb.sim use a semicolon to separate their two components.",
    "recipes": [
      {
        "title": "Heat equation by separation of variables",
        "why": "pde.heat solves u_t = alpha u_xx on [0,L] with u(0)=u(L)=0 by expanding the initial profile f(x) in a sine series and decaying each mode. Reach for it to watch a temperature profile relax and to read off Fourier coefficients. Gotchas: f(x) may use only x and should vanish at both ends (Dirichlet); the optional 4th argument pins the time horizon T, otherwise the horizon is the time mode 1 has decayed to 10%.",
        "steps": [
          {
            "code": "pde.heat 1, 1, x*(1-x)",
            "note": "parabolic bump on [0,1]; coefficient table shows b_1=0.258, even modes 0 (the classic b_n = 8/(n^3 pi^3), odd n)"
          },
          {
            "code": "pde.heat 1, 1, x*(1-x), 0.02",
            "note": "pin a short horizon T=0.02 to inspect the early transient before diffusion smooths the corner"
          },
          {
            "code": "eval 8/pi^3",
            "note": "0.258... — the reported b_1 coefficient in closed form"
          },
          {
            "code": "eval 1/pi^2",
            "note": "0.1013 — matches the reported mode-1 time constant 1/(alpha (pi/L)^2) for alpha=L=1"
          }
        ]
      },
      {
        "title": "Wave equation: plucked vs struck string, and predicting the period",
        "why": "pde.wave solves u_tt = c^2 u_xx with initial displacement f(x) and optional initial velocity g(x). A plucked string starts from a displacement (give f); a struck string starts from rest with a velocity kick (set f=0 and supply g as the 4th argument). This recipe also shows the lazy := workflow: bind L and c, then let a later eval predict the fundamental period 2L/c that the plot reports.",
        "steps": [
          {
            "code": "pde.wave 1, 2, sin(pi*x)",
            "note": "displacement is a pure fundamental mode; it oscillates as cos(2 pi t), fundamental period 2L/c = 1"
          },
          {
            "code": "pde.wave 1, 2, x*(1-x)",
            "note": "broad parabolic pluck excites many harmonics at once (same period 1)"
          },
          {
            "code": "pde.wave 1, 1, 0, sin(pi*x)",
            "note": "struck string: zero initial displacement f=0, sinusoidal initial velocity g (here c=1, so period 2)"
          },
          {
            "code": "L := 1",
            "note": "bind session variables (they apply to later lines only)"
          },
          {
            "code": "c := 2"
          },
          {
            "code": "eval 2*L/c",
            "note": "1 = 2L/c for c=2 — the fundamental period the first two pde.wave runs report"
          }
        ]
      },
      {
        "title": "Nonlinear reaction-diffusion (Fisher-KPP and friends)",
        "why": "pde.simulate marches u_t = alpha u_xx + f(u) in time by the method of lines (central differences + Crank-Nicolson + Newton with the exact symbolic f'(u)), so it handles genuinely nonlinear reactions the separated solutions cannot. Use it for population fronts, combustion, and pattern formation. Gotchas: the reaction f depends only on u (not x or t), the initial profile u0(x) uses only x and should vanish at the ends, and a runaway reaction stops early with an honest note rather than throwing. The optional 6th argument sets the time-step count (default 400, range 8..20000).",
        "steps": [
          {
            "code": "pde.simulate 10, 1, u*(1-u), 0.5*sin(pi*x/10), 8",
            "note": "supercritical domain (L=10 > pi*sqrt(alpha) = pi): the seed profile grows to the carrying capacity u=1"
          },
          {
            "code": "pde.simulate 10, 1, u*(1-u), 0.5*sin(pi*x/10), 8, 800",
            "note": "optional 6th arg refines the time-step count to 800"
          },
          {
            "code": "pde.simulate 1, 1, 0, sin(pi*x), 0.1",
            "note": "set f=0 to recover pure diffusion — cross-check against pde.heat 1, 1, sin(pi*x), 0.1"
          },
          {
            "code": "pde.simulate 1, 0.05, u*(1-u)*(u-0.2), sin(pi*x), 4",
            "note": "bistable (Nagumo / Allen-Cahn-type) reaction with an unstable threshold at u=0.2 between the stable states u=0 and u=1"
          }
        ]
      },
      {
        "title": "1-D boundary-value problem by finite elements",
        "why": "fem.bvp solves the Sturm-Liouville form -(p(x) u')' + q(x) u = f(x) by Galerkin FEM and reports the observed convergence order against a refined mesh. Reach for it when the coefficients vary in x or the geometry is not separable. Boundary conditions are written u=<value> (Dirichlet) or u'=<value> (natural flux p u' = value); switch P1 (linear) to P2 (quadratic) elements and set the element count (range 4..~124 for P2, since the solve refines to 4x) with the optional trailing arguments.",
        "steps": [
          {
            "code": "fem.bvp 1, 0, pi^2*sin(pi*x), 0, 1, u=0, u=0",
            "note": "Poisson problem -u'' = pi^2 sin(pi x); exact solution sin(pi x); observed order ~2 for P1"
          },
          {
            "code": "fem.bvp 1, 0, pi^2*sin(pi*x), 0, 1, u=0, u=0, p2",
            "note": "quadratic elements lift the observed order to ~3 (theory: degree+1)"
          },
          {
            "code": "fem.bvp 1, 0, pi^2*sin(pi*x), 0, 1, u=0, u=0, p2, 16",
            "note": "control the mesh: 16 P2 elements (refined to 64 for the error estimate)"
          },
          {
            "code": "fem.bvp 1, 0, 1, 0, 1, u=0, u'=0",
            "note": "mixed conditions: Dirichlet u=0 at the left, natural zero-flux u'=0 at the right; solution u = x - x^2/2"
          }
        ]
      },
      {
        "title": "Cross-check a FEM solution against the closed form",
        "why": "A multi-step workflow that pairs plugin numerics with CAS verbs. Solve the BVP numerically, then verify the analytic candidate satisfies the strong form by differentiating it twice with diff (each console line is independent, so read one output into the next line by hand). This is the fastest way to build trust in a numerical PDE result.",
        "steps": [
          {
            "code": "fem.bvp 1, 0, pi^2*sin(pi*x), 0, 1, u=0, u=0",
            "note": "numeric u(x); the plotted solution peaks at 1 near x=0.5"
          },
          {
            "code": "diff sin(pi*x), x",
            "note": "candidate u = sin(pi x): first derivative is pi*cos(pi x)"
          },
          {
            "code": "diff pi*cos(pi*x), x",
            "note": "-pi^2*sin(pi x), so -u'' = pi^2 sin(pi x) = f — the candidate satisfies the equation exactly"
          }
        ]
      },
      {
        "title": "Sturm-Liouville eigenmodes",
        "why": "fem.modes returns the smallest eigenpairs of -(p u')' + q u = lambda w u with u(a)=u(b)=0, via a generalized eigenproblem with inverse iteration and M-orthogonal deflation. Use it for vibration modes, quantum wells, and any self-adjoint spectral question. Gotchas: the ends are always homogeneous Dirichlet; the optional trailing args are count (1..6, default 4), then p1|p2, then element count (4..256, default 128).",
        "steps": [
          {
            "code": "fem.modes 1, 0, 1, 0, pi",
            "note": "textbook -u'' = lambda u on [0,pi]: eigenvalues 1,4,9,16 and sqrt(lambda) = mode number"
          },
          {
            "code": "fem.modes 1, 0, 1, 0, pi, 6, p2",
            "note": "six modes with quadratic elements for sharper high modes"
          },
          {
            "code": "fem.modes 1, x^2, 1, -5, 5, 4",
            "note": "quantum harmonic oscillator (potential q=x^2) truncated to [-5,5]: eigenvalues ~1,3,5,7 = 2n-1 for mode n"
          }
        ]
      },
      {
        "title": "Fredholm integral equation (second kind)",
        "why": "ie.fredholm solves u(x) = f(x) + lambda * integral_a^b K(x,t) u(t) dt by Nystrom quadrature (composite Simpson) and reports an error estimate against half resolution. Separable kernels have closed-form solutions, which makes this a great correctness check. The kernel K is written in the two variables x and t; the forcing f may use only x.",
        "steps": [
          {
            "code": "ie.fredholm x*t, x, 1, 0, 1",
            "note": "separable kernel; exact answer is u=1.5x (sample table runs 0, 0.375, 0.75, 1.125, 1.5)"
          },
          {
            "code": "ie.fredholm cos(x-t), 1, 0.5, 0, pi",
            "note": "smooth oscillatory kernel on [0,pi] with constant forcing f=1"
          }
        ]
      },
      {
        "title": "Volterra integral equation (second kind)",
        "why": "ie.volterra solves u(x) = f(x) + lambda * integral_a^x K(x,t) u(t) dt by trapezoidal marching. Because the upper limit is x, these are causal and equivalent to initial-value problems, so you can validate them against known ODE solutions. Same argument shape as ie.fredholm; only the upper limit of the integral differs.",
        "steps": [
          {
            "code": "ie.volterra 1, 1, 1, 0, 2",
            "note": "u = 1 + integral_0^x u dt gives u'=u, u(0)=1, so u=e^x (u(2)=7.389)"
          },
          {
            "code": "ie.volterra x - t, x, -1, 0, 3",
            "note": "convolution kernel equivalent to u''=-u, u(0)=0, u'(0)=1: the solution is sin(x) (u(3)=0.141)"
          }
        ]
      },
      {
        "title": "Event-driven hybrid system: the bouncing ball",
        "why": "hyb.sim integrates a two-state flow x'=f_x, v'=f_v by RK4 until a guard surface crosses from >0 to <=0, then applies a reset map to the state and continues. The dynamics pair and the reset pair are each two expressions separated by a semicolon; the guard between them is a single expression. It is the tool for impacts, switching, and any piecewise-continuous dynamics, and it detects Zeno behavior (infinitely many events in finite time) instead of grinding forever.",
        "steps": [
          {
            "code": "hyb.sim v; -9.81, x, x; -0.8*v, 1, 0, 3",
            "note": "gravity with guard x (height hits 0) and reset v <- -0.8v: six damped bounces in 3 s"
          },
          {
            "code": "hyb.sim v; -9.81, x, x; -0.8*v, 1, 0, 8",
            "note": "longer horizon: bounces pile up and Zeno detection stops it near the accumulation time t ~ 4.06 (= 9*sqrt(2/9.81))"
          },
          {
            "code": "hyb.sim v; -9.81, x, x; -v, 1, 0, 3",
            "note": "lossless reset v <- -v: energy conserved, perfectly periodic bounces"
          }
        ]
      }
    ]
  }
];
