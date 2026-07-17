#pragma once

// Discrete calculus: closed-form sums and products, and linear
// constant-coefficient recurrence equations (rsolve).

#include <string>
#include <string_view>
#include <vector>

#include "mathsolver/expr.hpp"

namespace mathsolver {

struct SumResult {
    enum class Status { Exact, Diverges, Unsolved };
    Status status = Status::Unsolved;
    Expr value;  ///< Exact only.
    std::string method;
    std::vector<std::string> warnings;  ///< e.g. "valid for |x| < 1"
};

/// Closed form of  Σ_{var=lo}^{hi} term.  Supported summands: polynomials in
/// var (exact Faulhaber-style fit), p(var)·r^var geometric forms (r numeric
/// or symbolic), and their sums.  `lo` must be a numeric integer; `hi` may
/// be numeric or a free symbol (e.g. n) for a symbolic closed form.
SumResult sum_finite(const Expr& term, std::string_view var, const Expr& lo,
                     const Expr& hi);

/// Closed form of  Σ_{var=lo}^{∞} term.  Geometric forms need |r| < 1
/// (checked when numeric, warned when symbolic); rational summands
/// telescope through apart() when the partial fractions cancel.
SumResult sum_infinite(const Expr& term, std::string_view var, const Expr& lo);

/// Closed form of  Π_{var=lo}^{hi} term  for constant/geometric terms with
/// a symbolic bound, or exact evaluation for numeric bounds (≤ 512 factors).
SumResult product_finite(const Expr& term, std::string_view var, const Expr& lo,
                         const Expr& hi);

struct RsolveResult {
    Expr solution;  ///< a(n) in closed form.
    int order = 0;
    std::string method;
    std::vector<std::string> warnings;
};

/// Solve a linear constant-coefficient recurrence with initial conditions:
///
///   rsolve("a(n+2) = a(n+1) + a(n)", {"a(0)=0", "a(1)=1"})
///     -> Binet's formula (exact irrational roots via the quadratic formula)
///
/// Characteristic roots may be rational (any multiplicity) or irrational
/// real quadratic pairs; complex roots are reported as unsupported. Forcing
/// terms of the form p(n)·s^n are handled by undetermined coefficients,
/// including resonance with a characteristic root. Missing initial
/// conditions default to zero with a warning.
RsolveResult rsolve(std::string_view recurrence,
                    const std::vector<std::string>& conditions);

} // namespace mathsolver
