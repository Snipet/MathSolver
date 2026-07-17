#pragma once

// Hybrid-system simulation numerics, separated from the plugin command layer
// so the native test suite can verify the math directly.
//
// A hybrid system here is a two-state flow with an event surface:
//
//   x' = f_x(t, x, v),  v' = f_v(t, x, v)          (continuous dynamics)
//   guard g(t, x, v):  event when g crosses > 0 → <= 0
//   reset (x, v) ← (r_x(t, x, v), r_v(t, x, v))    at the pre-event state
//
// Integration is classical RK4 on a uniform grid. When an armed guard
// changes sign inside a step, the event time is refined by bisection on the
// RK4 sub-step, the reset map is applied, and integration restarts from the
// post-event state. Two safety valves keep the classic pathologies honest:
//
//   - Arming: a reset typically lands ON the guard surface (a bouncing ball
//     resets to x = 0), so events stay disarmed until the guard is strictly
//     positive again — otherwise the event would immediately re-fire.
//   - Zeno detection: when events accumulate (bounce times form a convergent
//     series), the simulation stops at the accumulation point rather than
//     grinding through infinitely many events; the result carries a zeno
//     flag and a human-readable note.

#include <string>
#include <vector>

#include "mathsolver/expr.hpp"

namespace mathsolver::plugins::hyb {

struct HybEvent {
    double t = 0.0;         ///< Refined event time.
    double x_before = 0.0;  ///< State just before the reset...
    double v_before = 0.0;
    double x_after = 0.0;   ///< ...and just after.
    double v_after = 0.0;
};

struct HybResult {
    std::vector<double> t;  ///< Sample times (step ends + event instants).
    std::vector<double> x;
    std::vector<double> v;
    std::vector<HybEvent> events;
    bool zeno = false;      ///< Stopped at an event accumulation point.
    std::string note;       ///< Set when the run ended early (Zeno, blow-up).
};

/// Simulate from t = 0 to `horizon` with `steps` nominal RK4 steps. All five
/// expressions may use the symbols t, x, v only. Throws Error for bad
/// symbols or a non-positive horizon; Zeno accumulation and solution
/// blow-up end the run early with a note instead of throwing.
HybResult simulate(const Expr& fx, const Expr& fv, const Expr& guard,
                   const Expr& reset_x, const Expr& reset_v, double x0,
                   double v0, double horizon, int steps = 2000);

} // namespace mathsolver::plugins::hyb
