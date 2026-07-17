// Integral-equation numerics (ie_core.hpp).

#include "ie_core.hpp"

#include <cmath>
#include <format>

#include "../linalg/linalg_core.hpp"
#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/expr.hpp"

namespace mathsolver::plugins::ie {

namespace {

namespace la = mathsolver::plugins::linalg;

void check_symbols(const Expr& e, bool allow_t, const char* what) {
    for (const std::string& s : free_symbols(e)) {
        if (s == "x" || (allow_t && s == "t")) {
            continue;
        }
        throw Error(std::format(
            "the {} may only use {} (found '{}')", what,
            allow_t ? "x and t" : "x", s));
    }
}

double eval_kernel(const Expr& kernel, double x, double t) {
    const double v = evaluate(kernel, Bindings{{"x", x}, {"t", t}});
    if (!std::isfinite(v)) {
        throw Error(std::format(
            "the kernel is not finite at (x, t) = ({:.6g}, {:.6g})", x, t));
    }
    return v;
}

double eval_forcing(const Expr& f, double x) {
    const double v = evaluate(f, Bindings{{"x", x}});
    if (!std::isfinite(v)) {
        throw Error(std::format("f is not finite at x = {:.6g}", x));
    }
    return v;
}

/// Composite-Simpson nodes and weights on [a, b] with an odd node count.
void simpson_grid(double a, double b, int nodes, std::vector<double>& xs,
                  std::vector<double>& ws) {
    const double h = (b - a) / (nodes - 1);
    xs.clear();
    ws.clear();
    for (int i = 0; i < nodes; ++i) {
        xs.push_back(a + h * i);
        const bool edge = i == 0 || i == nodes - 1;
        ws.push_back(h / 3.0 * (edge ? 1.0 : (i % 2 == 1 ? 4.0 : 2.0)));
    }
}

/// One Nyström solve at a fixed resolution (no error estimate).
IeSolution nystrom_solve(const Expr& kernel, const Expr& f, double lambda,
                         double a, double b, int nodes) {
    IeSolution s;
    simpson_grid(a, b, nodes, s.x, s.w);
    la::Matrix mat(static_cast<std::size_t>(nodes),
                   la::Vector(static_cast<std::size_t>(nodes), 0.0));
    la::Vector rhs(static_cast<std::size_t>(nodes), 0.0);
    for (int i = 0; i < nodes; ++i) {
        const auto ii = static_cast<std::size_t>(i);
        rhs[ii] = eval_forcing(f, s.x[ii]);
        for (int j = 0; j < nodes; ++j) {
            const auto jj = static_cast<std::size_t>(j);
            mat[ii][jj] = (i == j ? 1.0 : 0.0) -
                          lambda * s.w[jj] * eval_kernel(kernel, s.x[ii], s.x[jj]);
        }
    }
    try {
        s.u = la::lu_solve(std::move(mat), std::move(rhs));
    } catch (const la::LinalgError&) {
        throw Error(std::format(
            "lambda = {:.6g} is (numerically) a characteristic value of the "
            "kernel — the Fredholm operator I - lambda K is singular",
            lambda));
    }
    return s;
}

} // namespace

IeSolution solve_fredholm(const Expr& kernel, const Expr& f, double lambda,
                          double a, double b, int nodes) {
    check_symbols(kernel, true, "kernel K(x, t)");
    check_symbols(f, false, "forcing f(x)");
    if (!(b > a)) {
        throw Error("the interval must satisfy a < b");
    }
    if (nodes < 5 || nodes > la::k_max_n - 1 || nodes % 2 == 0) {
        throw Error(std::format("node count must be odd and in [5, {}]",
                                la::k_max_n - 1));
    }
    IeSolution fine = nystrom_solve(kernel, f, lambda, a, b, nodes);

    // Half resolution: halve the interval count, rounded down to even so
    // composite Simpson still applies.
    int coarse_intervals = (nodes - 1) / 2;
    if (coarse_intervals % 2 == 1) {
        --coarse_intervals;
    }
    if (coarse_intervals >= 4) {
        const IeSolution coarse =
            nystrom_solve(kernel, f, lambda, a, b, coarse_intervals + 1);
        double err = 0.0;
        for (std::size_t i = 0; i < fine.x.size(); ++i) {
            err = std::max(err, std::abs(fine.u[i] - fredholm_eval(
                                             coarse, kernel, f, lambda,
                                             fine.x[i])));
        }
        fine.error_estimate = err;
    }
    return fine;
}

double fredholm_eval(const IeSolution& s, const Expr& kernel, const Expr& f,
                     double lambda, double x) {
    double acc = eval_forcing(f, x);
    for (std::size_t j = 0; j < s.x.size(); ++j) {
        acc += lambda * s.w[j] * eval_kernel(kernel, x, s.x[j]) * s.u[j];
    }
    return acc;
}

namespace {

/// One trapezoidal march at a fixed resolution (no error estimate).
IeSolution volterra_march(const Expr& kernel, const Expr& f, double lambda,
                          double a, double b, int steps) {
    const double h = (b - a) / steps;
    IeSolution s;
    s.x.push_back(a);
    s.u.push_back(eval_forcing(f, a));
    for (int i = 1; i <= steps; ++i) {
        const double xi = a + h * i;
        double acc = 0.5 * eval_kernel(kernel, xi, s.x[0]) * s.u[0];
        for (int j = 1; j < i; ++j) {
            const auto jj = static_cast<std::size_t>(j);
            acc += eval_kernel(kernel, xi, s.x[jj]) * s.u[jj];
        }
        const double denom = 1.0 - lambda * h * 0.5 * eval_kernel(kernel, xi, xi);
        if (std::abs(denom) < 1e-12) {
            throw Error(std::format(
                "the marching step is singular at x = {:.6g} "
                "(lambda h K(x, x) / 2 = 1); try a different resolution",
                xi));
        }
        const double ui = (eval_forcing(f, xi) + lambda * h * acc) / denom;
        if (!std::isfinite(ui) || std::abs(ui) > 1e12) {
            throw Error(std::format(
                "the solution blows up near x = {:.6g}", xi));
        }
        s.x.push_back(xi);
        s.u.push_back(ui);
    }
    return s;
}

} // namespace

IeSolution solve_volterra(const Expr& kernel, const Expr& f, double lambda,
                          double a, double b, int steps) {
    check_symbols(kernel, true, "kernel K(x, t)");
    check_symbols(f, false, "forcing f(x)");
    if (!(b > a)) {
        throw Error("the interval must satisfy a < b");
    }
    if (steps < 10 || steps > 2000 || steps % 2 == 1) {
        throw Error("step count must be even and in [10, 2000]");
    }
    IeSolution fine = volterra_march(kernel, f, lambda, a, b, steps);
    const IeSolution coarse =
        volterra_march(kernel, f, lambda, a, b, steps / 2);
    double err = 0.0;
    for (std::size_t k = 0; k < coarse.x.size(); ++k) {
        err = std::max(err, std::abs(fine.u[2 * k] - coarse.u[k]));
    }
    fine.error_estimate = err;
    return fine;
}

} // namespace mathsolver::plugins::ie
