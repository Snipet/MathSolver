#pragma once

#include <string_view>

#include "mathsolver/expr.hpp"

namespace mathsolver {

/// The [m/n] Padé approximant of `f`: the rational function P(x)/Q(x) with
/// deg P <= m, deg Q <= n and Q(0) = 1 whose Maclaurin expansion agrees with
/// that of `f` through order m + n. Computed from the exact Taylor
/// coefficients (via `series`) by solving the denominator's defining linear
/// system over exact Expr arithmetic, then reading off the numerator.
struct PadeResult {
    Expr approximant;   ///< P(x) / Q(x), simplified.
    Expr numerator;     ///< P(x), the degree-<=m numerator polynomial.
    Expr denominator;   ///< Q(x), the degree-<=n denominator (constant term 1).
    int m = 0;          ///< Numerator degree bound.
    int n = 0;          ///< Denominator degree bound.
};

/// Compute the [m/n] Padé approximant of `f` in `var`, expanded about 0.
/// Throws `Error` when m or n is negative, m + n exceeds the series cap, the
/// Maclaurin series is not a polynomial in `var`, or the approximant does not
/// exist (its defining linear system is singular for these degrees).
PadeResult pade(const Expr& f, std::string_view var, int m, int n);

} // namespace mathsolver
