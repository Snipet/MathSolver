#pragma once

#include <string>
#include <string_view>
#include <vector>

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

/// The resultant of two polynomials in `var` — the product of the differences
/// of their roots, times a leading-coefficient factor. It is zero exactly when
/// the polynomials share a common factor. Computed via the Euclidean recursion
/// over the polynomial remainder (no Sylvester matrix). Reuses PolyGcdResult.
PolyGcdResult polynomial_resultant(const Expr& a, const Expr& b, std::string_view var);

/// Extended Euclidean algorithm for polynomials: the monic gcd `g` together
/// with Bézout cofactors `s`, `t` satisfying s·a + t·b = g exactly over the
/// rationals (all in `var`). For a = b = 0, g = s = t = 0.
struct PolyBezoutResult {
    enum class Status { Ok, NotPolynomial };
    Status status = Status::NotPolynomial;
    Expr gcd;   ///< the monic gcd
    Expr s;     ///< cofactor of a (s·a + t·b = gcd)
    Expr t;     ///< cofactor of b
    std::string message;
};

PolyBezoutResult polynomial_bezout(const Expr& a, const Expr& b, std::string_view var);

/// The companion matrix of a univariate polynomial in `var`, in the MATLAB
/// `compan` orientation: for the degree-n polynomial normalized to monic form
/// x^n + a_{n-1} x^{n-1} + ... + a_1 x + a_0, an n×n matrix whose top row is
/// (-a_{n-1}, ..., -a_1, -a_0) and whose first subdiagonal is all ones. Its
/// characteristic polynomial is the (monic) input, so its eigenvalues are the
/// polynomial's roots. The matrix is exact over the rationals; symbolic
/// coefficients are kept symbolic. Requires degree ≥ 1.
struct CompanionResult {
    enum class Status { Ok, NotPolynomial, DegreeTooLow };
    Status status = Status::NotPolynomial;
    std::vector<std::vector<Expr>> matrix;  ///< n×n, row-major
    std::string message;
};

CompanionResult companion_matrix(const Expr& poly, std::string_view var);

} // namespace mathsolver
