// Hybrid-simulation plugin tests: the bouncing ball against its closed-form
// bounce times and apex heights, the pure-ODE path against an exact
// solution, Zeno detection, the error paths, and the command envelope.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <string>

#include "../plugins/hyb/hyb_core.hpp"
#include "mathsolver/errors.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/plugin.hpp"

using namespace mathsolver;
using namespace mathsolver::plugins;
namespace hy = mathsolver::plugins::hyb;
using Catch::Matchers::ContainsSubstring;

namespace {

hy::HybResult bouncing_ball(double horizon) {
    // x'' = -g with restitution 0.8, dropped from x = 1.
    return hy::simulate(parse_expression("v"), parse_expression("-9.81"),
                        parse_expression("x"), parse_expression("x"),
                        parse_expression("-0.8*v"), 1.0, 0.0, horizon);
}

} // namespace

TEST_CASE("hyb: pure ODE with an inactive guard is plain RK4") {
    // x' = v, v' = -x from (1, 0): x = cos t, v = -sin t. Guard never fires.
    const hy::HybResult r = hy::simulate(
        parse_expression("v"), parse_expression("-x"),
        parse_expression("x + 10"), parse_expression("x"),
        parse_expression("v"), 1.0, 0.0, 3.141592653589793);
    CHECK(r.events.empty());
    CHECK_FALSE(r.zeno);
    CHECK(std::abs(r.x.back() - (-1.0)) < 1e-9);
    CHECK(std::abs(r.v.back()) < 1e-9);
}

TEST_CASE("hyb: bouncing ball hits the closed-form bounce times") {
    // Drop from h0 = 1 with g = 9.81, e = 0.8:
    //   t1 = sqrt(2/g), v1 = g t1; flight n lasts 2 e^n t1.
    const double t1 = std::sqrt(2.0 / 9.81);
    const hy::HybResult r = bouncing_ball(2.0);
    REQUIRE(r.events.size() >= 3);
    CHECK(std::abs(r.events[0].t - t1) < 1e-6);
    CHECK(std::abs(r.events[1].t - t1 * (1.0 + 2.0 * 0.8)) < 1e-6);
    CHECK(std::abs(r.events[2].t - t1 * (1.0 + 2.0 * 0.8 + 2.0 * 0.64)) <
          1e-6);
    // Impact and rebound speeds: v1 = g t1, rebound e v1.
    const double v1 = 9.81 * t1;
    CHECK(std::abs(r.events[0].v_before + v1) < 1e-6);
    CHECK(std::abs(r.events[0].v_after - 0.8 * v1) < 1e-6);
    // Impact happens on the guard surface.
    CHECK(std::abs(r.events[0].x_before) < 1e-9);
}

TEST_CASE("hyb: bouncing ball apex heights follow e^(2n)") {
    const hy::HybResult r = bouncing_ball(2.0);
    REQUIRE(r.events.size() >= 3);
    // Apex of flight n (after bounce n) is e^(2n) * h0. Scan the samples
    // between consecutive events.
    for (std::size_t n = 1; n <= 2; ++n) {
        const double lo = r.events[n - 1].t;
        const double hi = r.events[n].t;
        double apex = 0.0;
        for (std::size_t i = 0; i < r.t.size(); ++i) {
            if (r.t[i] > lo && r.t[i] < hi) {
                apex = std::max(apex, r.x[i]);
            }
        }
        CHECK(std::abs(apex - std::pow(0.8, 2.0 * n)) < 1e-5);
    }
}

TEST_CASE("hyb: the bouncing ball's Zeno accumulation is detected") {
    // Bounce times accumulate at t1 (1 + e) / (1 - e) = 9 t1 ~= 4.064 < 5.
    const double t_inf = std::sqrt(2.0 / 9.81) * 9.0;
    const hy::HybResult r = bouncing_ball(5.0);
    CHECK(r.zeno);
    CHECK(r.events.size() >= 10);
    CHECK_THAT(r.note, ContainsSubstring("Zeno"));
    CHECK_THAT(r.note, ContainsSubstring("estimated accumulation"));
    // The run stops just short of the accumulation point.
    CHECK(r.t.back() < t_inf);
    CHECK(r.t.back() > t_inf - 0.15);
}

TEST_CASE("hyb: blow-up ends the run with a note instead of nonsense") {
    // x' = x^2 from x = 1 escapes in finite time (t = 1).
    const hy::HybResult r = hy::simulate(
        parse_expression("x^2"), parse_expression("0"),
        parse_expression("x + 10"), parse_expression("x"),
        parse_expression("v"), 1.0, 0.0, 2.0);
    CHECK_FALSE(r.zeno);
    CHECK_THAT(r.note, ContainsSubstring("stopped"));
    CHECK(r.t.back() < 2.0);
}

TEST_CASE("hyb: simulator errors are specific") {
    const Expr v = parse_expression("v");
    const Expr z = parse_expression("0");
    CHECK_THROWS_WITH(
        hy::simulate(parse_expression("y"), z, v, v, v, 0.0, 0.0, 1.0),
        ContainsSubstring("found 'y'"));
    CHECK_THROWS_WITH(hy::simulate(v, z, v, v, v, 0.0, 0.0, -1.0),
                      ContainsSubstring("positive"));
    CHECK_THROWS_AS(hy::simulate(v, z, v, v, v, 0.0, 0.0, 1.0, 5), Error);
}

TEST_CASE("hyb plugin: command envelopes") {
    register_builtin_plugins();
    const Plugin* p = find("hyb");
    REQUIRE(p != nullptr);
    CHECK(p->commands().size() == 1);

    const std::string ball =
        p->invoke("sim", {"v; -9.81", "x", "x; -0.8*v", "1", "0", "2"});
    CHECK_THAT(ball, ContainsSubstring("\"ok\":true"));
    CHECK_THAT(ball, ContainsSubstring("Events"));
    CHECK_THAT(ball, ContainsSubstring("Trajectory"));
    CHECK_THAT(ball, ContainsSubstring("vlines"));

    const std::string zeno =
        p->invoke("sim", {"v; -9.81", "x", "x; -0.8*v", "1", "0", "5"});
    CHECK_THAT(zeno, ContainsSubstring("Zeno"));

    CHECK_THAT(p->invoke("sim", {"v", "x", "x; -0.8*v", "1", "0", "2"}),
               ContainsSubstring("usage"));
    CHECK_THAT(p->invoke("sim", {"v; -9.81", "x", "x; -0.8*v", "1", "0", "no"}),
               ContainsSubstring("finite numbers"));
    CHECK_THAT(p->invoke("sim", {"q; -9.81", "x", "x; -0.8*v", "1", "0", "2"}),
               ContainsSubstring("found 'q'"));
}
