#pragma once

// Linear constant-coefficient ODE initial-value problems, solved
// symbolically by the Laplace-transform method.

#include <string>
#include <string_view>
#include <vector>

#include "mathsolver/expr.hpp"

namespace mathsolver {

struct DsolveResult {
    Expr solution;   ///< y(t) when explicit; F(t, y) with F = 0 when implicit.
    Expr transform;  ///< Laplace path only: Y(s) in partial-fraction form
                     ///< (null for the first-order methods).
    int order = 0;   ///< Highest derivative of y in the equation.
    bool implicit = false;  ///< solution is a relation F(t, y) = 0.
    std::string method;
    /// Initial conditions that were not given and were assumed zero,
    /// e.g. "assuming y'(0) = 0".
    std::vector<std::string> warnings;
};

/// Solve  a_n y^(n) + ... + a_1 y' + a_0 y = f(t)  with initial conditions
/// at t = 0, exactly:  L{y^(k)} = s^k Y - Σ s^(k-1-j) y^(j)(0)  turns the
/// ODE into algebra, Y(s) = (F(s) + ics)/charpoly is expanded into partial
/// fractions, and the inverse transform gives y(t).
///
/// `ode` is the equation text with primes for derivatives — the coefficients
/// must be numeric, the forcing term f(t) may be any expression the forward
/// transform handles (sums of polynomials, exponentials, sin/cos/sinh/cosh
/// and their products): "y'' + 3y' + 2y = e^(-t)".  `conditions` are
/// "y(0)=1", "y'(0)=0", ... (any order k < n; omitted conditions default to
/// zero and are reported in warnings).  Resonant forcing is handled
/// (t·sin/t·cos inverses for repeated quadratic factors).
///
/// Equations spelled "y' = f(t, y)" route to first-order methods instead:
/// linear (integrating factor, p(t) arbitrary where ∫p integrates),
/// Bernoulli (v = y^(1-m)), and separable (∫dy/h = ∫g dt, inverted for y
/// when solve() can; an implicit relation with a warning otherwise).  With
/// no initial condition the general solution keeps a symbolic constant C.
///
/// Throws Error with a user-presentable message for malformed equations,
/// non-numeric coefficients, out-of-range conditions, or forcing terms with
/// no Laplace transform.
DsolveResult dsolve(std::string_view ode,
                    const std::vector<std::string>& conditions);

} // namespace mathsolver
