#pragma once

#include <string>
#include <string_view>

#include "mathsolver/expr.hpp"

namespace mathsolver {

/// Result of dividing one polynomial by another: `dividend = quotient * divisor
/// + remainder`, with `deg(remainder) < deg(divisor)`.
struct PolyDivResult {
    enum class Status { Ok, NotPolynomial, DivByZero };

    Status status = Status::NotPolynomial;
    Expr quotient;
    Expr remainder;
    std::string message;
};

/// Polynomial long division of `dividend` by `divisor` in `var` (other symbols
/// are treated as coefficients). Exact over the rationals; symbolic
/// coefficients are kept symbolic.
PolyDivResult polynomial_divide(const Expr& dividend, const Expr& divisor,
                                std::string_view var);

} // namespace mathsolver
