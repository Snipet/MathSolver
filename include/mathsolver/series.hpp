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

/// Asymptotic expansion at +infinity, up to and including (1/var)^order:
///
///   series_at_infinity((x+1)/(x-1), "x", 3)  ==  1 + 2/x + 2/x^2 + 2/x^3
///
/// Rational functions split through apart() first, so improper inputs keep
/// their exact polynomial part (x^3/(x-1) -> x^2 + x + 1 + 1/x + ...); the
/// proper remainder expands via the u = 1/var reduction. Throws Error when
/// f(1/u) is singular at 0 (e.g. e^x, which has no expansion in powers of
/// 1/x).
Expr series_at_infinity(const Expr& f, std::string_view var, int order);

} // namespace mathsolver
