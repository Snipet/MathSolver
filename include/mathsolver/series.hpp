#pragma once

// Taylor/Maclaurin series expansion.

#include <string_view>

#include "mathsolver/expr.hpp"

namespace mathsolver {

/// The Taylor polynomial of f about var = center, up to and including
/// (var - center)^order, with exact coefficients:
///
///   series(e^x, "x", 0, 4)  ==  1 + x + x^2/2 + x^3/6 + x^4/24
///
/// Derivatives are taken symbolically and evaluated at the center exactly.
/// Throws Error when f (or one of its derivatives) is singular at the
/// expansion point, or when order is outside [0, 20].
Expr series(const Expr& f, std::string_view var, const Expr& center, int order);

} // namespace mathsolver
