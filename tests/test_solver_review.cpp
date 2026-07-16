#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "mathsolver/evaluator.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/solver.hpp"

using namespace mathsolver;
using Status = SolveResult::Status;

namespace {

Equation eq(std::string_view s) {
    return parse_equation(s);
}

} // namespace

// ---------------------------------------------------------------------------
// Finding 1: inverse-function range checks must reject an exactly-out-of-range
// rational constant even when it misses the bound by less than kRangeEps.
// ---------------------------------------------------------------------------

TEST_CASE("solve: abs(x) = -1/1000000000000 has no real solution (exact range check)") {
    const SolveResult res = solve(eq("abs(x) = -1/1000000000000"), "x");
    CHECK(res.status == Status::NoRealSolution);
    CHECK(res.solutions.empty());
}

TEST_CASE("solve: sin(x) = 1 + 1/1000000000000 has no real solution (exact range check)") {
    const SolveResult res = solve(eq("sin(x) = 1 + 1/1000000000000"), "x");
    CHECK(res.status == Status::NoRealSolution);
    CHECK(res.solutions.empty());
}

TEST_CASE("solve: cosh(x) = 1 - 1/1000000000000 has no real solution (exact range check)") {
    const SolveResult res = solve(eq("cosh(x) = 1 - 1/1000000000000"), "x");
    CHECK(res.status == Status::NoRealSolution);
    CHECK(res.solutions.empty());
}

TEST_CASE("solve: abs(x) = 3 still yields both signed roots") {
    const SolveResult res = solve(eq("abs(x) = 3"), "x");
    CHECK(res.status == Status::Solved);
    CHECK(res.solutions.size() == 2);
}

TEST_CASE("solve: cosh(x) = 1 still yields the single root x = 0") {
    const SolveResult res = solve(eq("cosh(x) = 1"), "x");
    CHECK(res.status == Status::Solved);
    CHECK(res.solutions.size() == 1);
}

// ---------------------------------------------------------------------------
// Finding 2: a non-polynomial identity that simplify cannot fold must not be
// harvested into thousands of per-grid-point numeric roots.
// ---------------------------------------------------------------------------

TEST_CASE("solve: sin(2*x) = 2*sin(x)*cos(x) is an identity, not a dense root set") {
    const SolveResult res = solve(eq("sin(2*x) = 2*sin(x)*cos(x)"), "x");
    CHECK(res.status == Status::AllReals);
    CHECK(res.solutions.empty());
}

TEST_CASE("solve: ln(x^2) = 2*ln(x) is an identity, not a dense root set") {
    const SolveResult res = solve(eq("ln(x^2) = 2*ln(x)"), "x");
    CHECK(res.status == Status::AllReals);
    CHECK(res.solutions.empty());
}

TEST_CASE("solve: cos(x) = x is still a genuine single numeric root") {
    const SolveResult res = solve(eq("cos(x) = x"), "x");
    CHECK(res.status == Status::NumericOnly);
    CHECK(res.solutions.size() == 1);
}

// ---------------------------------------------------------------------------
// DESIGN §9 step 4: roots harvested from the no-sign-change (|f| minimum)
// branch carry a tangency note; sign-change roots do not.
// ---------------------------------------------------------------------------

TEST_CASE("solve: near-miss tangency root carries the tangency note") {
    // f = e^x + e^-x - 2 + 1e-9 has minimum exactly 1e-9 > 0 at x = 0: the
    // reported root is a numerical near-miss and must say so.
    const SolveResult res = solve(eq("e^(x) + e^(-x) = 2 - 1/1000000000"), "x");
    REQUIRE(res.status == SolveResult::Status::NumericOnly);
    REQUIRE_FALSE(res.solutions.empty());
    for (const Solution& s : res.solutions) {
        CHECK(s.note.find("tangency-type root") != std::string::npos);
        CHECK(s.note.find("no sign change observed") != std::string::npos);
    }
}

TEST_CASE("solve: genuine double root still reported, with the tangency note") {
    NumericOptions opts;
    opts.lo = -1.0;
    opts.hi = 1.0;
    const SolveResult res = solve_numeric(eq("sin(x)^2 = 0"), "x", opts);
    REQUIRE(res.status == SolveResult::Status::NumericOnly);
    REQUIRE(res.solutions.size() == 1);
    CHECK(std::abs(evaluate(res.solutions[0].value)) < 1e-6);
    CHECK(res.solutions[0].note.find("tangency-type root") != std::string::npos);
}

TEST_CASE("solve: sign-change numeric root carries no tangency note") {
    const SolveResult res = solve(eq("cos(x) = x"), "x");
    REQUIRE(res.status == SolveResult::Status::NumericOnly);
    REQUIRE(res.solutions.size() == 1);
    CHECK(res.solutions[0].note.empty());
}
