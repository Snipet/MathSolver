// Hybrid-system simulation numerics (hyb_core.hpp).

#include "hyb_core.hpp"

#include <cmath>
#include <format>

#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/expr.hpp"

namespace mathsolver::plugins::hyb {

namespace {

constexpr int k_max_events = 400;
constexpr double k_blow_up = 1e9;

struct State {
    double t = 0.0;
    double x = 0.0;
    double v = 0.0;
};

void check_symbols(const Expr& e, const char* what) {
    for (const std::string& s : free_symbols(e)) {
        if (s != "t" && s != "x" && s != "v") {
            throw Error(std::format(
                "the {} may only use t, x, v (found '{}')", what, s));
        }
    }
}

double eval3(const Expr& e, const State& s) {
    const double r =
        evaluate(e, Bindings{{"t", s.t}, {"x", s.x}, {"v", s.v}});
    if (!std::isfinite(r)) {
        throw Error(std::format(
            "an expression is not finite at t = {:.6g}, x = {:.6g}, "
            "v = {:.6g}",
            s.t, s.x, s.v));
    }
    return r;
}

/// One classical RK4 step of size h from s.
State rk4_step(const Expr& fx, const Expr& fv, const State& s, double h) {
    const auto deriv = [&](const State& p) {
        return State{1.0, eval3(fx, p), eval3(fv, p)};
    };
    const State k1 = deriv(s);
    const State k2 = deriv({s.t + h / 2, s.x + h / 2 * k1.x, s.v + h / 2 * k1.v});
    const State k3 = deriv({s.t + h / 2, s.x + h / 2 * k2.x, s.v + h / 2 * k2.v});
    const State k4 = deriv({s.t + h, s.x + h * k3.x, s.v + h * k3.v});
    return {s.t + h,
            s.x + h / 6 * (k1.x + 2 * k2.x + 2 * k3.x + k4.x),
            s.v + h / 6 * (k1.v + 2 * k2.v + 2 * k3.v + k4.v)};
}

} // namespace

HybResult simulate(const Expr& fx, const Expr& fv, const Expr& guard,
                   const Expr& reset_x, const Expr& reset_v, double x0,
                   double v0, double horizon, int steps) {
    check_symbols(fx, "dynamics x'");
    check_symbols(fv, "dynamics v'");
    check_symbols(guard, "guard");
    check_symbols(reset_x, "reset for x");
    check_symbols(reset_v, "reset for v");
    if (!(horizon > 0.0)) {
        throw Error("the horizon T must be positive");
    }
    if (steps < 100 || steps > 20000) {
        throw Error("step count must be in [100, 20000]");
    }

    const double h = horizon / steps;
    // Events closer together than the integrator can resolve are treated as
    // an accumulation (the classic Zeno situation); below this gap a
    // crossing inside a single step could be silently missed instead.
    const double min_gap = std::max(1e-6, 4.0 * h);

    HybResult out;
    State s{0.0, x0, v0};
    const auto record = [&](const State& p) {
        out.t.push_back(p.t);
        out.x.push_back(p.x);
        out.v.push_back(p.v);
    };
    record(s);

    double prev_gap = -1.0;
    while (s.t < horizon - 1e-12 * horizon) {
        const double h_eff = std::min(h, horizon - s.t);
        const State next = rk4_step(fx, fv, s, h_eff);
        const double g0 = eval3(guard, s);
        const double g1 = eval3(guard, next);

        // Armed crossing: the guard was strictly positive at the step start
        // (a reset lands ON the surface with g = 0, which must not re-fire)
        // and non-positive at the step end.
        if (g0 > 0.0 && g1 <= 0.0) {
            double lo = 0.0;
            double hi = h_eff;
            for (int it = 0; it < 60; ++it) {
                const double mid = (lo + hi) / 2;
                (eval3(guard, rk4_step(fx, fv, s, mid)) > 0.0 ? lo : hi) = mid;
            }
            const State ev = rk4_step(fx, fv, s, hi);
            record(ev);

            HybEvent e;
            e.t = ev.t;
            e.x_before = ev.x;
            e.v_before = ev.v;
            e.x_after = eval3(reset_x, ev);
            e.v_after = eval3(reset_v, ev);
            out.events.push_back(e);

            const double gap = out.events.size() >= 2
                                   ? e.t - out.events[out.events.size() - 2].t
                                   : e.t;
            const bool too_many =
                static_cast<int>(out.events.size()) >= k_max_events;
            if (too_many || (out.events.size() >= 2 && gap < min_gap)) {
                out.zeno = true;
                // Geometric extrapolation of the gap sequence estimates the
                // accumulation time: t_inf ~= t_n + gap * r / (1 - r).
                std::string acc;
                if (prev_gap > 0.0 && gap > 0.0 && gap < 0.999 * prev_gap) {
                    const double r = gap / prev_gap;
                    acc = std::format("; estimated accumulation at t = {:.6g}",
                                      e.t + gap * r / (1.0 - r));
                }
                out.note = std::format(
                    "Zeno behavior: {} events by t = {:.6g} with the "
                    "inter-event gap down to {:.3g}{} — simulation stopped",
                    out.events.size(), e.t, gap, acc);
                break;
            }
            prev_gap = gap;

            s = State{ev.t, e.x_after, e.v_after};
            record(s);
            continue;
        }

        s = next;
        record(s);
        if (std::abs(s.x) > k_blow_up || std::abs(s.v) > k_blow_up) {
            out.note = std::format(
                "the solution exceeded {:.0e} near t = {:.6g} — simulation "
                "stopped",
                k_blow_up, s.t);
            break;
        }
    }
    return out;
}

} // namespace mathsolver::plugins::hyb
