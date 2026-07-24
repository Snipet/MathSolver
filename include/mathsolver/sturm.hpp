#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "mathsolver/expr.hpp"
#include "mathsolver/rational.hpp"

namespace mathsolver {

/// One isolated real root of a polynomial. For an exact rational root
/// `lo == hi == value` and `exact` is true; otherwise the root lies in the open
/// interval (lo, hi) with rational endpoints and `exact` is false.
struct RootInterval {
    Rational lo;
    Rational hi;
    bool exact = false;
    double approx = 0.0;
};

/// Count the distinct real roots of the univariate polynomial `poly` (i.e. the
/// solutions of poly = 0), by Sturm's theorem over exact rational arithmetic.
/// When both `lo` and `hi` are given, counts the roots in the half-open
/// interval (lo, hi]; otherwise counts over all of R. Multiplicity does not
/// matter — a double root counts once.
///
/// Throws `Error` if `poly` is not a polynomial in `var`, has non-numeric
/// (symbolic) coefficients, is the zero polynomial, or if `lo >= hi`.
int sturm_root_count(const Expr& poly, std::string_view var,
                     const std::optional<Rational>& lo = std::nullopt,
                     const std::optional<Rational>& hi = std::nullopt);

/// Isolate every distinct real root of `poly` into a disjoint rational interval,
/// sorted ascending. Exact rational roots are reported exactly; irrational roots
/// are bracketed and refined to a narrow interval (with a numeric midpoint in
/// `approx`). Throws `Error` under the same conditions as `sturm_root_count`.
std::vector<RootInterval> sturm_isolate_roots(const Expr& poly,
                                              std::string_view var);

} // namespace mathsolver
