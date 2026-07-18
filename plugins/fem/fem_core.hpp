#pragma once

// 1-D finite-element numerics, separated from the plugin command layer so
// the native test suite can verify the math directly.
//
// Both solvers work on the Sturm–Liouville operator
//
//   L u = -(p(x) u')' + q(x) u        on [a, b],  p(x) > 0
//
// with symbolic coefficients evaluated pointwise by the CAS:
//
//   solve_bvp:   L u = f(x) with a Dirichlet value (u = v) or natural flux
//     (p u' = v) condition at each endpoint. Galerkin assembly with P1 or
//     P2 Lagrange elements and 3-point Gauss quadrature; the dense system
//     is LU-factored with partial pivoting (no size cap needed at these
//     mesh sizes).
//
//   solve_modes: L u = lambda w(x) u with homogeneous Dirichlet ends —
//     the generalized eigenproblem K u = lambda M u, its smallest
//     eigenpairs found by inverse iteration with M-orthogonal deflation
//     (K factored once, reused every iteration).
//
// Honesty metric: solve_bvp runs the same problem at n, 2n, and 4n
// elements and reports both the disagreement against the finest mesh and
// the OBSERVED convergence order log2(e_n / e_2n) — evidence that the
// method converges at its textbook rate on this particular problem, not
// just a claim.

#include <string>
#include <vector>

#include "mathsolver/expr.hpp"

namespace mathsolver::plugins::fem {

struct Bc {
    bool dirichlet = true;  ///< true: u = value; false: natural flux p u' = value.
    double value = 0.0;
};

struct FemSolution {
    std::vector<double> x;  ///< Nodes ascending (P2 includes midside nodes).
    std::vector<double> u;  ///< Nodal values.
    int degree = 1;         ///< 1 (linear) or 2 (quadratic) elements.
};

/// Interpolate a FEM solution at any x in [a, b] through its element basis.
double fem_eval(const FemSolution& s, double x);

struct BvpResult {
    FemSolution solution;    ///< The finest (4n-element) solve.
    double error_estimate;   ///< max over samples of |u_2n - u_4n|.
    double observed_order;   ///< log2(e_n / e_2n); NaN when at roundoff.
    std::vector<std::string> warnings;
};

/// Solve the two-point BVP at `elements`, 2x, and 4x resolution. Throws
/// Error for stray symbols (p, q, f may use x only), a >= b, p <= 0 at a
/// quadrature point, degree outside {1, 2}, elements outside [4, 256], or
/// a singular system (e.g. the pure-Neumann problem with q = 0).
BvpResult solve_bvp(const Expr& p, const Expr& q, const Expr& f, double a,
                    double b, Bc left, Bc right, int degree, int elements);

struct ModesResult {
    std::vector<double> lambdas;      ///< Ascending eigenvalues.
    std::vector<FemSolution> modes;   ///< M-normalized, peak-positive.
    std::vector<std::string> warnings;
};

/// Smallest `count` eigenpairs of -(p u')' + q u = lambda w u with u = 0 at
/// both ends. Throws Error for stray symbols, w <= 0 or p <= 0 at a
/// quadrature point, count outside [1, 6], or a singular stiffness matrix.
ModesResult solve_modes(const Expr& p, const Expr& q, const Expr& w, double a,
                        double b, int count, int degree, int elements);

} // namespace mathsolver::plugins::fem
