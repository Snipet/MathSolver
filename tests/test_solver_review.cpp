#include <string_view>

#include <catch2/catch_test_macros.hpp>

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
