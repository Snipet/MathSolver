#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "mathsolver/expr.hpp"

namespace mathsolver {

/// One recorded step in a worked solution: a named rule and the concrete
/// transformation it produced, rendered in plain text and LaTeX.
struct ExplainStep {
    std::string rule;  // short rule name, e.g. "power rule", "chain rule"
    std::string plain; // "d/dx(x^3) = 3*x^2"
    std::string latex; // LaTeX rendering of the same line
};

/// An ordered worked solution: the steps (innermost rule first, ending with a
/// "simplify" step) and the final result. `result_plain` is identical to what
/// the non-explained verb prints — the recorder never changes the answer.
struct Explanation {
    std::vector<ExplainStep> steps;
    std::string result_plain;
    std::string result_latex;
};

/// Worked steps for d(e)/d(symbol): mirrors `differentiate`, recording one step
/// per rule application (sum, product, power, exponential, general power, and
/// the chain rule through the function table), then a final "simplify" step.
/// Throws the same errors as `differentiate` for non-differentiable inputs.
Explanation explain_derivative(const Expr& e, std::string_view symbol);

} // namespace mathsolver
