// PDE separation-of-variables numerics (pde_core.hpp).

#include "pde_core.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

#include "../linalg/linalg_core.hpp"
#include "mathsolver/derivative.hpp"
#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/expr.hpp"
#include "mathsolver/integrate.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/printer.hpp"
#include "mathsolver/simplify.hpp"

namespace mathsolver::plugins::pde {

SineSeries sine_coefficients(const Expr& f, const std::string& var,
                             double length, int modes) {
    if (!(length > 0.0)) {
        throw Error("the interval length L must be positive");
    }
    if (modes < 1 || modes > 64) {
        throw Error("mode count must be in [1, 64]");
    }
    for (const std::string& s : free_symbols(f)) {
        if (s != var) {
            throw Error(std::format(
                "the profile may only use the variable {} (found '{}')", var,
                s));
        }
    }
    SineSeries out;
    // Rational approximation of L for the exact-integration attempt; the
    // sine argument is built as n*pi*x / L.
    for (int n = 1; n <= modes; ++n) {
        const Expr arg = simplify(make_div(
            make_mul({make_num(n), make_const(ConstantId::Pi), make_sym(var)}),
            parse_expression(std::format("{:.12g}", length))));
        const Expr integrand =
            simplify(make_mul({f, make_fn(FunctionId::Sin, arg)}));
        const DefiniteIntegralResult r = integrate_definite(
            integrand, var, make_num(0),
            parse_expression(std::format("{:.12g}", length)));
        double value = 0.0;
        switch (r.status) {
            case DefiniteIntegralResult::Status::Exact:
                out.exact_count += 1;
                [[fallthrough]];
            case DefiniteIntegralResult::Status::Numeric:
                value = evaluate(r.value, Bindings{});
                break;
            case DefiniteIntegralResult::Status::Unsolved:
                throw Error(std::format(
                    "cannot integrate the profile against mode {}: {}", n,
                    r.warnings.empty() ? "integration failed"
                                       : r.warnings.front()));
        }
        out.b.push_back(2.0 / length * value);
    }
    return out;
}

double heat_eval(const SineSeries& s, double length, double alpha, double x,
                 double t) {
    double u = 0.0;
    for (std::size_t k = 0; k < s.b.size(); ++k) {
        const double n = static_cast<double>(k + 1);
        const double w = n * std::numbers::pi / length;
        u += s.b[k] * std::sin(w * x) * std::exp(-alpha * w * w * t);
    }
    return u;
}

double wave_eval(const SineSeries& a, const SineSeries& g, double length,
                 double c, double x, double t) {
    double u = 0.0;
    const std::size_t modes = std::max(a.b.size(), g.b.size());
    for (std::size_t k = 0; k < modes; ++k) {
        const double n = static_cast<double>(k + 1);
        const double kx = n * std::numbers::pi / length;
        const double w = kx * c;
        const double an = k < a.b.size() ? a.b[k] : 0.0;
        const double gn = k < g.b.size() ? g.b[k] : 0.0;
        u += std::sin(kx * x) *
             (an * std::cos(w * t) + (w > 0 ? gn / w * std::sin(w * t) : 0.0));
    }
    return u;
}

// --- method-of-lines reaction–diffusion ------------------------------------

namespace {

namespace la = mathsolver::plugins::linalg;

constexpr double k_blow_up = 1e8;

struct Stepper {
    double alpha = 0.0;
    double h = 0.0;              // grid spacing
    const Expr* f = nullptr;     // reaction f(u)
    const Expr* df = nullptr;    // exact f'(u) from the CAS
    int newton_total = 0;
    int newton_max = 0;
    int halvings = 0;

    double reaction_at(double u) const {
        const double v = evaluate(*f, Bindings{{"u", u}});
        if (!std::isfinite(v)) {
            throw Error(std::format("the reaction is not finite at u = {:.6g}", u));
        }
        return v;
    }

    double reaction_slope_at(double u) const {
        const double v = evaluate(*df, Bindings{{"u", u}});
        if (!std::isfinite(v)) {
            throw Error(std::format(
                "the reaction derivative is not finite at u = {:.6g}", u));
        }
        return v;
    }

    /// alpha * u_xx by central differences with zero Dirichlet ends;
    /// `u` holds interior values only.
    std::vector<double> diffuse(const std::vector<double>& u) const {
        const std::size_t m = u.size();
        std::vector<double> out(m, 0.0);
        const double c = alpha / (h * h);
        for (std::size_t i = 0; i < m; ++i) {
            const double left = i > 0 ? u[i - 1] : 0.0;
            const double right = i + 1 < m ? u[i + 1] : 0.0;
            out[i] = c * (left - 2.0 * u[i] + right);
        }
        return out;
    }

    /// One Crank–Nicolson step of size dt via Newton; false when Newton
    /// stalls (the caller halves dt).
    bool cn_step(std::vector<double>& u, double dt) {
        const std::size_t m = u.size();
        // Explicit half: b = u^n + dt/2 (A u^n + F(u^n)).
        std::vector<double> b = diffuse(u);
        for (std::size_t i = 0; i < m; ++i) {
            b[i] = u[i] + 0.5 * dt * (b[i] + reaction_at(u[i]));
        }
        std::vector<double> v = u; // Newton iterate for u^{n+1}
        const double c = alpha / (h * h);
        for (int it = 1; it <= 25; ++it) {
            ++newton_total;
            newton_max = std::max(newton_max, it);
            // Residual r(v) = v - dt/2 (A v + F(v)) - b.
            std::vector<double> r = diffuse(v);
            double scale = 1.0;
            for (std::size_t i = 0; i < m; ++i) {
                r[i] = v[i] - 0.5 * dt * (r[i] + reaction_at(v[i])) - b[i];
                scale = std::max(scale, std::abs(v[i]));
            }
            double rnorm = 0.0;
            for (const double ri : r) {
                rnorm = std::max(rnorm, std::abs(ri));
            }
            if (rnorm <= 1e-11 * scale) {
                u = std::move(v);
                return true;
            }
            // J = I - dt/2 (A + diag f'(v)): tridiagonal, Thomas-solved.
            la::Vector diag(m, 0.0);
            la::Vector off(m - 1, -0.5 * dt * c);
            for (std::size_t i = 0; i < m; ++i) {
                diag[i] = 1.0 + dt * c - 0.5 * dt * reaction_slope_at(v[i]);
            }
            la::Vector delta;
            try {
                delta = la::tridiag_solve(off, diag, off, r);
            } catch (const la::LinalgError&) {
                return false; // singular Jacobian: retry with a smaller dt
            }
            bool finite = true;
            for (std::size_t i = 0; i < m; ++i) {
                v[i] -= delta[i];
                finite = finite && std::isfinite(v[i]);
            }
            if (!finite) {
                return false;
            }
        }
        return false;
    }

    /// Advance by dt, halving on Newton failure (up to 4 levels deep).
    /// False means the step is unresolvable even at dt/16 — with an
    /// A-stable implicit scheme that is the signature of finite-time
    /// blow-up (or extreme stiffness), which the caller reports as an
    /// early stop rather than an exception: it is a real answer.
    bool advance(std::vector<double>& u, double dt, int depth) {
        if (cn_step(u, dt)) {
            return true;
        }
        if (depth >= 4) {
            return false;
        }
        ++halvings;
        return advance(u, dt / 2.0, depth + 1) &&
               advance(u, dt / 2.0, depth + 1);
    }
};

} // namespace

SimulateResult simulate_reaction_diffusion(double length, double alpha,
                                           const Expr& reaction,
                                           const Expr& initial, double horizon,
                                           int interior, int steps) {
    if (!(length > 0.0)) {
        throw Error("the interval length L must be positive");
    }
    if (!(alpha > 0.0)) {
        throw Error("the diffusivity alpha must be positive");
    }
    if (!(horizon > 0.0)) {
        throw Error("the horizon T must be positive");
    }
    if (interior < 9 || interior > 999) {
        throw Error("interior grid points must be in [9, 999]");
    }
    if (steps < 8 || steps > 20000) {
        throw Error("time steps must be in [8, 20000]");
    }
    for (const std::string& s : free_symbols(reaction)) {
        if (s != "u") {
            throw Error(std::format(
                "the reaction may only use u (found '{}')", s));
        }
    }
    for (const std::string& s : free_symbols(initial)) {
        if (s != "x") {
            throw Error(std::format(
                "the initial profile may only use x (found '{}')", s));
        }
    }

    Stepper st;
    st.alpha = alpha;
    st.h = length / (interior + 1);
    const Expr df = simplify(differentiate(reaction, "u"));
    st.f = &reaction;
    st.df = &df;

    SimulateResult out;
    for (int i = 0; i <= interior + 1; ++i) {
        out.x.push_back(length * i / (interior + 1));
    }
    std::vector<double> u(static_cast<std::size_t>(interior), 0.0);
    for (int i = 0; i < interior; ++i) {
        const double v = evaluate(initial, Bindings{{"x", out.x[i + 1]}});
        if (!std::isfinite(v)) {
            throw Error(std::format(
                "the initial profile is not finite at x = {:.6g}",
                out.x[i + 1]));
        }
        u[static_cast<std::size_t>(i)] = v;
    }

    const auto record = [&](double t) {
        std::vector<double> row{0.0};
        row.insert(row.end(), u.begin(), u.end());
        row.push_back(0.0);
        out.times.push_back(t);
        out.profiles.push_back(std::move(row));
    };
    record(0.0);

    const double dt = horizon / steps;
    int next_snapshot = 1;
    for (int n = 1; n <= steps; ++n) {
        if (!st.advance(u, dt, 0)) {
            out.stopped_early = true;
            out.note = std::format(
                "the implicit step stopped converging near t = {:.6g} even "
                "at 1/16 of the time step — simulation stopped (finite-time "
                "blow-up or extreme stiffness)",
                dt * n);
            record(dt * n);
            break;
        }
        double peak = 0.0;
        for (const double v : u) {
            peak = std::max(peak, std::abs(v));
        }
        if (peak > k_blow_up) {
            out.stopped_early = true;
            out.note = std::format(
                "the solution exceeded {:.0e} near t = {:.6g} — simulation "
                "stopped (reaction blow-up)",
                k_blow_up, dt * n);
            record(dt * n);
            break;
        }
        // 4 post-initial snapshots at T/4, T/2, 3T/4, T.
        while (next_snapshot <= 4 &&
               n == (steps * next_snapshot + 3) / 4) {
            record(dt * n);
            ++next_snapshot;
        }
    }
    out.newton_total = st.newton_total;
    out.newton_max = st.newton_max;
    out.halvings = st.halvings;
    return out;
}

} // namespace mathsolver::plugins::pde
