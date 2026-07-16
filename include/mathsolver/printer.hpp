#pragma once

#include <string>

#include "mathsolver/expr.hpp"

namespace mathsolver {

enum class PrintStyle { Plain, LaTeX };

/// Render an expression. Plain style prints `2*x^2 + sqrt(y) - 3/2`;
/// LaTeX style prints `2x^{2} + \sqrt{y} - \frac{3}{2}`. Full formatting
/// rules (parenthesization, fraction/sqrt reconstruction from negative and
/// rational powers, e^x, add-term ordering, \cdot placement) are in
/// DESIGN.md §5.
///
/// Round-trip invariant: parse_expression(to_string(e, style)) is
/// structurally_equal to e, for both styles.
std::string to_string(const Expr& e, PrintStyle style = PrintStyle::Plain);

/// "lhs = rhs" in the requested style.
std::string to_string(const Equation& eq, PrintStyle style = PrintStyle::Plain);

} // namespace mathsolver
