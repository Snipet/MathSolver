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

// --- method-of-lines reaction–diffusion ------------------------------------
//
// u_t = alpha u_xx + f(u) on [0, L] with homogeneous Dirichlet ends and
// u(x, 0) = u0(x). Second-order central differences in space; Crank–
// Nicolson in time with a full Newton solve per step — the Jacobian
// diagonal uses the EXACT symbolic derivative f'(u) from the CAS, and each
// Newton update is one tridiagonal (Thomas) solve. A step whose Newton
// iteration stalls retries at half the step size (recursively, a few
// levels); solutions escaping past 1e8 stop the run with a note instead of
// throwing, because reaction blow-up (e.g. f = u^2) is a real answer.

struct SimulateResult {
    std::vector<double> x;                      ///< Full grid incl. the ends.
    std::vector<double> times;                  ///< Snapshot times.
    std::vector<std::vector<double>> profiles;  ///< One per snapshot time.
    int newton_total = 0;   ///< Newton iterations across the whole run.
    int newton_max = 0;     ///< Worst single step.
    int halvings = 0;       ///< Time-step halvings forced by stalled Newton.
    bool stopped_early = false;
    std::string note;       ///< Set when the run ended early (blow-up).
};

/// Simulate to t = horizon with `interior` grid points and `steps` nominal
/// time steps, recording 5 evenly spaced snapshots. The reaction may use
/// the symbol u only; the initial profile only x. Throws Error for bad
/// symbols, alpha <= 0, horizon <= 0, or a Newton iteration that stalls
/// even after repeated halving.
SimulateResult simulate_reaction_diffusion(double length, double alpha,
                                           const Expr& reaction,
                                           const Expr& initial, double horizon,
                                           int interior = 119, int steps = 400);

} // namespace mathsolver::plugins::pde
