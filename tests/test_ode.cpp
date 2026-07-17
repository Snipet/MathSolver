// dsolve: linear constant-coefficient IVPs by the Laplace method.
//
// Solutions are verified numerically against hand-derived closed forms at
// several times, and (for a sample) by substituting back into the ODE.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "mathsolver/derivative.hpp"
#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/ode.hpp"

using namespace mathsolver;
using Catch::Matchers::ContainsSubstring;

namespace {

void check_dsolve(const std::string& ode,
                  const std::vector<std::string>& conditions,
                  const std::function<double(double)>& want) {
    const DsolveResult r = dsolve(ode, conditions);
    for (const double t : {0.0, 0.3, 0.9, 1.7, 2.6}) {
        const double got = evaluate(r.solution, Bindings{{"t", t}});
        INFO("dsolve(" << ode << ") at t=" << t << ": got " << got << " want "
                       << want(t));
        CHECK(std::abs(got - want(t)) < 1e-8 * (1.0 + std::abs(want(t))));
    }
}

} // namespace

TEST_CASE("dsolve: first-order decay and forcing") {
    check_dsolve("y' + y = 0", {"y(0)=1"},
                 [](double t) { return std::exp(-t); });
    check_dsolve("y' + y = 1", {"y(0)=0"},
                 [](double t) { return 1.0 - std::exp(-t); });
    check_dsolve("y' + y = e^(-2t)", {"y(0)=0"},
                 [](double t) { return std::exp(-t) - std::exp(-2 * t); });
    check_dsolve("y' - 2y = 0", {"y(0)=3"},
                 [](double t) { return 3.0 * std::exp(2 * t); });
}

TEST_CASE("dsolve: second-order distinct real roots") {
    check_dsolve("y'' + 3y' + 2y = 0", {"y(0)=1", "y'(0)=0"}, [](double t) {
        return 2.0 * std::exp(-t) - std::exp(-2 * t);
    });
}

TEST_CASE("dsolve: repeated real root") {
    check_dsolve("y'' + 2y' + y = 0", {"y(0)=1", "y'(0)=0"}, [](double t) {
        return std::exp(-t) * (1.0 + t);
    });
}

TEST_CASE("dsolve: oscillation and damped oscillation") {
    check_dsolve("y'' + y = 0", {"y(0)=0", "y'(0)=1"},
                 [](double t) { return std::sin(t); });
    check_dsolve("y'' + 2y' + 5y = 0", {"y(0)=1", "y'(0)=-1"},
                 [](double t) { return std::exp(-t) * std::cos(2 * t); });
}

TEST_CASE("dsolve: irrational roots go through sinh/cosh") {
    check_dsolve("y'' - 2y = 0", {"y(0)=1", "y'(0)=0"}, [](double t) {
        return std::cosh(std::sqrt(2.0) * t);
    });
}

TEST_CASE("dsolve: sinusoidal forcing off resonance") {
    // y'' + 4y = sin(t), zero ICs -> sin(t)/3 - sin(2t)/6.
    check_dsolve("y'' + 4y = sin(t)", {"y(0)=0", "y'(0)=0"}, [](double t) {
        return std::sin(t) / 3.0 - std::sin(2 * t) / 6.0;
    });
}

TEST_CASE("dsolve: resonance produces the secular t-term") {
    // y'' + y = sin(t), zero ICs -> (sin t - t cos t)/2.
    check_dsolve("y'' + y = sin(t)", {"y(0)=0", "y'(0)=0"}, [](double t) {
        return (std::sin(t) - t * std::cos(t)) / 2.0;
    });
}

TEST_CASE("dsolve: polynomial and mixed forcing") {
    // y' + y = t, y(0)=0 -> t - 1 + e^{-t}.
    check_dsolve("y' + y = t", {"y(0)=0"},
                 [](double t) { return t - 1.0 + std::exp(-t); });
}

TEST_CASE("dsolve: third order") {
    // y''' - y' = 0, y(0)=0, y'(0)=1, y''(0)=0 -> sinh(t).
    check_dsolve("y''' - y' = 0", {"y(0)=0", "y'(0)=1", "y''(0)=0"},
                 [](double t) { return std::sinh(t); });
}

TEST_CASE("dsolve: solutions satisfy the ODE symbolically") {
    const DsolveResult r = dsolve("y'' + 3y' + 2y = e^(-3t)",
                                  {"y(0)=0", "y'(0)=1"});
    const Expr y = r.solution;
    const Expr y1 = differentiate(y, "t");
    const Expr y2 = differentiate(y1, "t");
    for (const double t : {0.2, 0.8, 1.9}) {
        const Bindings b{{"t", t}};
        const double residual = evaluate(y2, b) + 3.0 * evaluate(y1, b) +
                                2.0 * evaluate(y, b) - std::exp(-3 * t);
        CHECK(std::abs(residual) < 1e-9);
    }
    // ICs hit exactly.
    CHECK(std::abs(evaluate(y, Bindings{{"t", 0.0}})) < 1e-12);
    CHECK(std::abs(evaluate(y1, Bindings{{"t", 0.0}}) - 1.0) < 1e-12);
}

TEST_CASE("dsolve: missing conditions default to zero with a warning") {
    const DsolveResult r = dsolve("y'' + y = 1", {"y(0)=1"});
    REQUIRE(r.warnings.size() == 1);
    CHECK_THAT(r.warnings[0], ContainsSubstring("y'(0) = 0"));
    for (const double t : {0.4, 1.3}) {
        const double got = evaluate(r.solution, Bindings{{"t", t}});
        CHECK(std::abs(got - 1.0) < 1e-9); // y = 1 solves it with y(0)=1, y'(0)=0
    }
}

TEST_CASE("dsolve: fractional and starred coefficients parse") {
    check_dsolve("2y' + y = 0", {"y(0)=1"},
                 [](double t) { return std::exp(-t / 2.0); });
    check_dsolve("3/2 * y' + 3y = 0", {"y(0)=2"},
                 [](double t) { return 2.0 * std::exp(-2.0 * t); });
}

TEST_CASE("dsolve: error messages are specific") {
    CHECK_THROWS_WITH(dsolve("y'' + y", {}), ContainsSubstring("'='"));
    CHECK_THROWS_WITH(dsolve("2y = 0", {}),
                      ContainsSubstring("no derivative"));
    CHECK_THROWS_WITH(dsolve("y' + t = 0", {}),
                      ContainsSubstring("right side"));
    CHECK_THROWS_WITH(dsolve("a y' + y = 0", {}),
                      ContainsSubstring("numeric"));
    CHECK_THROWS_WITH(dsolve("y' + y = 0", {"y''(0)=1"}),
                      ContainsSubstring("exceeds"));
    CHECK_THROWS_WITH(dsolve("y' + y = 0", {"y(0)=1", "y(0)=2"}),
                      ContainsSubstring("duplicate"));
    CHECK_THROWS_WITH(dsolve("y' + y = ln(t)", {}),
                      ContainsSubstring("no Laplace transform"));
    CHECK_THROWS_WITH(dsolve("y' + y = y", {}), ContainsSubstring("not contain y"));
}
