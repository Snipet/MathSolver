#pragma once

#include <string_view>
#include <variant>

#include "mathsolver/errors.hpp"
#include "mathsolver/expr.hpp"

namespace mathsolver {

/// LaTeX-style math parser. Accepts both LaTeX (`\frac{1}{2} + \sin{x}`) and
/// plain ASCII (`1/2 + sin(x)`) input with implicit multiplication (`2x`,
/// `(x+1)(x-2)`). Full grammar, token rules, and the identifier-segmentation
/// algorithm are specified in DESIGN.md §4. All functions throw ParseError
/// (with a byte span into the input) on malformed input.

/// Parse either a bare expression or an equation (single top-level '=').
std::variant<Expr, Equation> parse_input(std::string_view src);

/// Parse an expression; an equation or malformed input throws ParseError.
Expr parse_expression(std::string_view src);

/// Parse an equation; input without '=' throws ParseError.
Equation parse_equation(std::string_view src);

} // namespace mathsolver
