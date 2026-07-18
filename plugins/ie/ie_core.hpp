#pragma once

// Integral-equation numerics, separated from the plugin command layer so the
// native test suite can verify the math directly.
//
// Both solvers handle second-kind equations with a symbolic kernel K(x, t)
// and forcing f(x), evaluated pointwise by the CAS evaluator:
//
//   Fredholm:  u(x) = f(x) + lambda * ∫_a^b K(x, t) u(t) dt
//     Nyström method on a composite-Simpson grid: the quadrature turns the
//     equation into the dense linear system (I - lambda K W) u = f, solved
//     by the linalg plugin's LU. Off-node values come from the Nyström
//     interpolation formula (plug the node solution back into the equation),
//     which inherits the quadrature's accuracy.
//
//   Volterra:  u(x) = f(x) + lambda * ∫_a^x K(x, t) u(t) dt
//     Trapezoidal marching: each step solves the scalar implicit equation
//     u_i = (f_i + lambda h (K_i0 u_0 / 2 + sum_{0<j<i} K_ij u_j))
//           / (1 - lambda h K_ii / 2).
//
// Honesty metric: every solve is repeated at half resolution and
// `error_estimate` reports the largest disagreement, so the reported curve
// carries its own accuracy check.

#include <string>
#include <vector>

#include "mathsolver/expr.hpp"

namespace mathsolver::plugins::ie {

struct IeSolution {
    std::vector<double> x;  ///< Grid nodes in [a, b].
    std::vector<double> u;  ///< Solution values at the nodes.
    std::vector<double> w;  ///< Quadrature weights (Fredholm only; empty for Volterra).
    double error_estimate = 0.0;  ///< Max |full - half resolution| over the grid.
};

/// Nyström solve of the Fredholm equation on `nodes` composite-Simpson
/// points (odd, 5..31 so the linear system stays within the linalg caps).
/// Throws Error for bad symbols (kernel may use x and t, f only x), a >= b,
/// non-finite evaluations, or lambda at/near a characteristic value.
IeSolution solve_fredholm(const Expr& kernel, const Expr& f, double lambda,
                          double a, double b, int nodes = 31);

/// Nyström interpolation of a Fredholm solution at an arbitrary x:
/// u(x) = f(x) + lambda * sum_j w_j K(x, t_j) u_j.
double fredholm_eval(const IeSolution& s, const Expr& kernel, const Expr& f,
                     double lambda, double x);

/// Trapezoidal marching solve of the Volterra equation with `steps` uniform
/// steps (even, 10..2000). Throws Error for bad symbols, a >= b, a singular
/// marching step (lambda h K_ii / 2 == 1), or a solution overflow.
IeSolution solve_volterra(const Expr& kernel, const Expr& f, double lambda,
                          double a, double b, int steps = 200);

} // namespace mathsolver::plugins::ie
