#pragma once

// Taylor/Maclaurin series expansion, asymptotic expansions at infinity, and
// the Stirling series for ln Gamma.

#include <string>
#include <string_view>
#include <vector>

#include "mathsolver/expr.hpp"
#include "mathsolver/rational.hpp"

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

/// Exact Bernoulli numbers B_0 .. B_m (with B_1 = -1/2) by the defining
/// recurrence sum_{j=0}^{m} C(m+1, j) B_j = 0. Throws for m outside
/// [0, 20] (the exact 64-bit rationals stay comfortably in range there).
std::vector<Rational> bernoulli_numbers(int m);

/// The single n-th Bernoulli number B_n (B_1 = -1/2 convention), exact over
/// the rationals. Requires 0 <= n <= 20 (throws Error otherwise).
Rational bernoulli_number(int n);

struct StirlingResult {
    Expr series;  ///< The truncated asymptotic series for ln Gamma(var).
    /// Accuracy check against lgamma at sample points, one line each —
    /// evidence that the truncation is honest, not proof of convergence
    /// (the full series diverges for every fixed x).
    std::vector<std::string> checks;
};

/// The Stirling asymptotic series of ln Gamma(x):
///
///   (x - 1/2) ln x - x + ln(2 pi)/2
///       + sum_{k=1}^{terms} B_{2k} / (2k (2k-1) x^(2k-1))
///
/// with exact Bernoulli coefficients (terms = 3 gives the familiar
/// 1/(12x) - 1/(360x^3) + 1/(1260x^5)). ln n! is stirling at x = n + 1.
/// Throws Error when terms is outside [0, 8].
StirlingResult stirling_series(std::string_view var, int terms);

} // namespace mathsolver
