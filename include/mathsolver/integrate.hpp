#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "mathsolver/expr.hpp"

namespace mathsolver {

/// Result of symbolic (indefinite) integration. The integrator is
/// rule-based (DESIGN.md §8b): table forms with linear inner arguments,
/// linearity, polynomial expansion, derivative-divides u-substitution,
/// pattern-bounded integration by parts, partial fractions over rational
/// coefficients (via solve_system), and basic trig-power identities.
/// Every candidate antiderivative is self-verified by differentiating it
/// back and numerically comparing against the integrand; an integral the
/// rules cannot handle returns Unsolved honestly rather than guessing.
struct IntegrateResult {
    enum class Status {
        Integrated,  ///< antiderivative found and verified
        Unsolved     ///< no applicable rule (or verification failed)
    };

    Status status = Status::Unsolved;
    Expr antiderivative;   ///< Integrated only; the "+ C" is implicit.
    std::string method;    ///< "table", "u-substitution", "integration by
                           ///< parts", "partial fractions", ... ("+"-joined)
    std::vector<std::string> warnings;
};

/// Antiderivative of `e` with respect to `symbol` (DESIGN.md §8b).
/// Real domain; antiderivatives of u^(-1) use ln(abs(u)).
IntegrateResult integrate(const Expr& e, std::string_view symbol);

/// Result of a definite integral over [lo, hi].
struct DefiniteIntegralResult {
    enum class Status {
        Exact,     ///< FTC on a verified antiderivative, symbolic value
        Numeric,   ///< adaptive-Simpson approximation, value is a Number
        Unsolved   ///< integrand/bounds not usable (see warnings)
    };

    Status status = Status::Unsolved;
    Expr value;
    std::string method;  ///< "FTC" or "numeric (adaptive Simpson)"
    std::vector<std::string> warnings;
};

/// Definite integral of `e` d`symbol` from `lo` to `hi` (DESIGN.md §8b).
/// Bounds must be symbol-free and finite. FTC path is cross-checked against
/// quadrature and falls back to numeric when the integrand's continuity on
/// the interval cannot be established.
DefiniteIntegralResult integrate_definite(const Expr& e, std::string_view symbol,
                                          const Expr& lo, const Expr& hi);

} // namespace mathsolver
