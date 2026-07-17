#pragma once

// PDE separation-of-variables numerics, separated from the plugin command
// layer so the native test suite can verify the math directly.
//
// Both solvers work on [0, L] with homogeneous Dirichlet boundary
// conditions, expanding the initial data in the sine eigenbasis
// sin(n pi x / L). Coefficients are computed by the CAS integrator —
// exactly where its rules reach, adaptive-quadrature numerics otherwise.

#include <string>
#include <vector>

#include "mathsolver/expr.hpp"

namespace mathsolver::plugins::pde {

struct SineSeries {
    std::vector<double> b;   ///< b_1..b_N (b[k] is mode k+1).
    int exact_count = 0;     ///< How many coefficients the integrator got exactly.
};

/// b_n = (2/L) ∫_0^L f(x) sin(n pi x / L) dx for n = 1..modes.
/// Throws Error when f contains symbols besides `var` or an integral fails
/// both exactly and numerically.
SineSeries sine_coefficients(const Expr& f, const std::string& var, double length,
                             int modes);

/// u(x, t) = Σ b_n sin(n pi x/L) e^{-alpha (n pi/L)^2 t}  (heat equation).
double heat_eval(const SineSeries& s, double length, double alpha, double x,
                 double t);

/// u(x, t) = Σ sin(n pi x/L) [a_n cos(w_n t) + (g_n / w_n) sin(w_n t)],
/// w_n = n pi c / L (wave equation; a from the displacement, g from the
/// velocity profile).
double wave_eval(const SineSeries& a, const SineSeries& g, double length,
                 double c, double x, double t);

} // namespace mathsolver::plugins::pde
