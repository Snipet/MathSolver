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

/// Result of a polynomial GCD/LCM computation.
struct PolyGcdResult {
    enum class Status { Ok, NotPolynomial };
    Status status = Status::NotPolynomial;
    Expr value;
    std::string message;
};

/// The monic greatest common divisor of two polynomials in `var` (via the
/// Euclidean algorithm over the polynomial remainder). gcd(0, 0) = 0.
PolyGcdResult polynomial_gcd(const Expr& a, const Expr& b, std::string_view var);

/// The monic least common multiple, a·b / gcd(a, b). lcm with 0 is 0.
PolyGcdResult polynomial_lcm(const Expr& a, const Expr& b, std::string_view var);

} // namespace mathsolver
