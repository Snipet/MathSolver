#pragma once

#include <string>
#include <string_view>

#include "mathsolver/expr.hpp"

namespace mathsolver {

/// The discriminant of a univariate polynomial (in `var`, its other symbols
/// treated as coefficients), from the exact closed-form formulas for degree
/// 2–4. Symbolic coefficients are kept symbolic (`a x² + b x + c` → `b² - 4ac`).
struct DiscriminantResult {
    enum class Status { Ok, NotPolynomial, DegreeTooLow, DegreeUnsupported };

    Status status = Status::NotPolynomial;
    Expr value;      ///< the discriminant expression (Status::Ok)
    int degree = 0;  ///< degree of the polynomial in `var`
    /// A plain-language description of the roots, filled in only when the
    /// discriminant is a concrete number (so its sign is known).
    std::string root_nature;
    std::string message; ///< explanation for a non-Ok status
};

/// Compute the discriminant of `f` with respect to `var`.
DiscriminantResult discriminant(const Expr& f, std::string_view var);

} // namespace mathsolver
