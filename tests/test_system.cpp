// Tests for linear-system solving (DESIGN.md §9b): joint-linearity
// extraction, Gaussian elimination over exact Expr arithmetic, pivot
// reordering, symbolic parameters, inconsistent/underdetermined shapes,
// and in-test verification of every Solved case by substitution.

#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "mathsolver/evaluator.hpp"
#include "mathsolver/expr.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/simplify.hpp"
#include "mathsolver/solver.hpp"

using namespace mathsolver;
using Status = SystemSolveResult::Status;

namespace {

std::vector<Equation> eqs(std::initializer_list<std::string_view> sources) {
    std::vector<Equation> out;
    for (const std::string_view s : sources) {
        out.push_back(parse_equation(s));
    }
    return out;
}

/// Expected values are written as strings and normalized the same way the
/// solver normalizes its results: through simplify().
Expr expected(std::string_view s) {
    return simplify(parse_expression(s));
}

bool any_warning_contains(const SystemSolveResult& res, std::string_view needle) {
    for (const std::string& w : res.warnings) {
        if (w.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

/// Verify a Solved/Underdetermined result inside the test: substitute the
/// values into every input equation; the residual must simplify to 0
/// exactly, or evaluate to ~0 with the given bindings supplying any
/// remaining free symbols (parameters and free variables).
void check_system_by_substitution(const std::vector<Equation>& equations,
                                  const SystemSolveResult& res,
                                  const Bindings& extra = {}) {
    for (const Equation& eq : equations) {
        Expr lhs = eq.lhs;
        Expr rhs = eq.rhs;
        for (const auto& [name, value] : res.values) {
            lhs = substitute(lhs, name, value);
            rhs = substitute(rhs, name, value);
        }
        const Expr residual = simplify(lhs - rhs);
        INFO("equation " << debug_string(eq.lhs) << " = " << debug_string(eq.rhs)
                         << ", residual " << debug_string(residual));
        if (residual->kind() == Kind::Number && residual->number().is_zero()) {
            continue;  // reduced to 0 exactly
        }
        Bindings bindings = extra;
        for (const std::string& s : free_symbols(lhs)) {
            bindings.emplace(s, 0.7357);  // arbitrary test value; extra wins
        }
        for (const std::string& s : free_symbols(rhs)) {
            bindings.emplace(s, 0.7357);
        }
        const double l = evaluate(lhs, bindings);
        const double r = evaluate(rhs, bindings);
        CHECK(std::abs(l - r) <= 1e-9 * std::max({1.0, std::abs(l), std::abs(r)}));
    }
}

} // namespace

TEST_CASE("system: unique 2x2 rational system") {
    const auto equations = eqs({"x + y = 3", "x - y = 1"});
    const SystemSolveResult res = solve_system(equations, {"x", "y"});
    REQUIRE(res.status == Status::Solved);
    REQUIRE(res.values.size() == 2);
    CHECK(structurally_equal(res.values.at("x"), expected("2")));
    CHECK(structurally_equal(res.values.at("y"), expected("1")));
    CHECK(res.free_variables.empty());
    CHECK(res.method == "gaussian elimination");
    CHECK(res.warnings.empty());
    check_system_by_substitution(equations, res);
}

TEST_CASE("system: unique 3x3 rational system") {
    // Solution (1/2, 1/3, 1/4), hand-checked: 1 + 1 + 1 = 3; 2 - 1 = 1;
    // 4 + 1 - 1 = 4.
    const auto equations =
        eqs({"2x + 3y + 4z = 3", "6y - 4z = 1", "8x + 3y - 4z = 4"});
    const SystemSolveResult res = solve_system(equations, {"x", "y", "z"});
    REQUIRE(res.status == Status::Solved);
    CHECK(structurally_equal(res.values.at("x"), expected("1/2")));
    CHECK(structurally_equal(res.values.at("y"), expected("1/3")));
    CHECK(structurally_equal(res.values.at("z"), expected("1/4")));
    CHECK(res.warnings.empty());
    check_system_by_substitution(equations, res);
}

TEST_CASE("system: symbolic parameters with a symbolic pivot warning") {
    // a*x + y = 1; x - y = b  for {x, y}. Adding the equations gives
    // (a + 1)*x = 1 + b, so x = (1 + b)/(a + 1) and
    // y = x - b = (1 - a*b)/(a + 1). Hand check at a = 2, b = 3:
    // x = 4/3, y = -5/3; then a*x + y = 8/3 - 5/3 = 1 and
    // x - y = 4/3 + 5/3 = 3 = b.
    const auto equations = eqs({"a*x + y = 1", "x - y = b"});
    const SystemSolveResult res = solve_system(equations, {"x", "y"});
    REQUIRE(res.status == Status::Solved);
    REQUIRE(res.values.size() == 2);

    // The elimination pivot on the second column is a + 1 (symbolic).
    CHECK(any_warning_contains(res, "valid only when"));
    CHECK(any_warning_contains(res, "a + 1"));

    // Exact spot check at a = 2, b = 3 against the hand-derived solution.
    const Bindings params{{"a", 2.0}, {"b", 3.0}};
    CHECK(std::abs(evaluate(res.values.at("x"), params) - 4.0 / 3.0) < 1e-12);
    CHECK(std::abs(evaluate(res.values.at("y"), params) + 5.0 / 3.0) < 1e-12);

    // The values must be free of the requested symbols.
    for (const auto& [name, value] : res.values) {
        CHECK(!contains_symbol(value, "x"));
        CHECK(!contains_symbol(value, "y"));
    }
    check_system_by_substitution(equations, res, params);
}

TEST_CASE("system: inconsistent numeric system has no solution") {
    const auto equations = eqs({"x + y = 1", "x + y = 2"});
    const SystemSolveResult res = solve_system(equations, {"x", "y"});
    CHECK(res.status == Status::NoSolution);
    CHECK(res.values.empty());
    CHECK(res.free_variables.empty());
}

TEST_CASE("system: underdetermined system reports free variables") {
    const auto equations = eqs({"x + y = 3"});
    const SystemSolveResult res = solve_system(equations, {"x", "y"});
    REQUIRE(res.status == Status::Underdetermined);
    REQUIRE(res.values.size() == 1);
    CHECK(structurally_equal(res.values.at("x"), expected("3 - y")));
    REQUIRE(res.free_variables == std::vector<std::string>{"y"});
    check_system_by_substitution(equations, res);
}

TEST_CASE("system: cross-terms are rejected as nonlinear") {
    const auto equations = eqs({"x*y = 1", "x - y = 0"});
    const SystemSolveResult res = solve_system(equations, {"x", "y"});
    CHECK(res.status == Status::Unsolved);
    CHECK(res.values.empty());
    CHECK(any_warning_contains(res, "system is not linear in the requested variables"));
}

TEST_CASE("system: quadratic terms are rejected as nonlinear") {
    const auto equations = eqs({"x^2 + y = 1", "x - y = 0"});
    const SystemSolveResult res = solve_system(equations, {"x", "y"});
    CHECK(res.status == Status::Unsolved);
    CHECK(any_warning_contains(res, "system is not linear in the requested variables"));
}

TEST_CASE("system: consistent overdetermined system is solved") {
    const auto equations = eqs({"x = 1", "2x = 2"});
    const SystemSolveResult res = solve_system(equations, {"x"});
    REQUIRE(res.status == Status::Solved);
    CHECK(structurally_equal(res.values.at("x"), expected("1")));
    check_system_by_substitution(equations, res);
}

TEST_CASE("system: zero pivot forces a row reorder") {
    // The first equation has no x term, so column 1 must pivot on row 2.
    const auto equations = eqs({"y = 1", "x + y = 2"});
    const SystemSolveResult res = solve_system(equations, {"x", "y"});
    REQUIRE(res.status == Status::Solved);
    CHECK(structurally_equal(res.values.at("x"), expected("1")));
    CHECK(structurally_equal(res.values.at("y"), expected("1")));
    CHECK(res.warnings.empty());
    check_system_by_substitution(equations, res);
}

TEST_CASE("system: 4x4 rational system") {
    // Cross-checked with exact rational arithmetic (Python fractions):
    // x = 117/11, y = -8, z = -89/11, w = 170/11.
    const auto equations = eqs({"x + y + z + w = 10", "2x - y + 3z = 5",
                                "4y - z + 2w = 7", "3x - 2w = 1"});
    const SystemSolveResult res = solve_system(equations, {"x", "y", "z", "w"});
    REQUIRE(res.status == Status::Solved);
    CHECK(structurally_equal(res.values.at("x"), expected("117/11")));
    CHECK(structurally_equal(res.values.at("y"), expected("-8")));
    CHECK(structurally_equal(res.values.at("z"), expected("-89/11")));
    CHECK(structurally_equal(res.values.at("w"), expected("170/11")));
    CHECK(res.warnings.empty());
    check_system_by_substitution(equations, res);
}

TEST_CASE("system: symbolic 0 = c row keeps the solution with a condition") {
    // x + y = 3; x + y = c is consistent only when c = 3.
    const auto equations = eqs({"x + y = 3", "x + y = c"});
    const SystemSolveResult res = solve_system(equations, {"x", "y"});
    REQUIRE(res.status == Status::Underdetermined);
    CHECK(structurally_equal(res.values.at("x"), expected("3 - y")));
    CHECK(res.free_variables == std::vector<std::string>{"y"});
    CHECK(any_warning_contains(res, "inconsistent unless"));
    // Verified under the condition c = 3.
    check_system_by_substitution(equations, res, Bindings{{"c", 3.0}});
}

TEST_CASE("system: requested symbol absent from every equation is free") {
    const auto equations = eqs({"x = 1", "2x = 2"});
    const SystemSolveResult res = solve_system(equations, {"x", "y"});
    REQUIRE(res.status == Status::Underdetermined);
    CHECK(structurally_equal(res.values.at("x"), expected("1")));
    CHECK(res.free_variables == std::vector<std::string>{"y"});
    check_system_by_substitution(equations, res);
}

// ---------------------------------------------------------------------------
// Regressions from the adversarial review of §9b
// ---------------------------------------------------------------------------

TEST_CASE("system: hidden-zero symbolic pivot does not fake a unique solution") {
    // Regression: the eliminated entry -2*(c + 1) + 2*c + 2 is identically
    // zero but survived simplify() (no expand), so it was chosen as a
    // symbolic pivot and the dependent system was reported Solved with the
    // free variable silently pinned to 0.
    const auto equations = eqs({"x + (c+1)*y = 1", "2*x + (2*c+2)*y = 2"});
    const SystemSolveResult res = solve_system(equations, {"x", "y"});
    REQUIRE(res.status == Status::Underdetermined);
    REQUIRE(res.free_variables == std::vector<std::string>{"y"});
    REQUIRE(res.values.size() == 1);
    // x = 1 - (c + 1)*y, spot-checked at c = 2, y = 5: x = 1 - 15 = -14.
    const Bindings b{{"c", 2.0}, {"y", 5.0}};
    CHECK(std::abs(evaluate(res.values.at("x"), b) + 14.0) < 1e-12);
    // No identically-false pivot condition may be reported.
    CHECK(!any_warning_contains(res, "valid only when"));
    check_system_by_substitution(equations, res, b);
}

TEST_CASE("system: hidden-zero symbolic pivot does not mask inconsistency") {
    // Same dependent left-hand sides, incompatible right-hand sides: the
    // truth is NoSolution for every c (previously Solved with a division by
    // an identically-zero expression).
    const auto equations = eqs({"x + (c+1)*y = 1", "2*x + (2*c+2)*y = 3"});
    const SystemSolveResult res = solve_system(equations, {"x", "y"});
    CHECK(res.status == Status::NoSolution);
    CHECK(res.values.empty());
}

TEST_CASE("system: exact rational 0 = c row is inconsistent, no float epsilon") {
    // Regression: the leftover row held the exact nonzero rational
    // 1/10000000000000, which a 1e-12 epsilon silently accepted as zero.
    const auto equations = eqs({"x = 1", "x = 1.0000000000001"});
    const SystemSolveResult res = solve_system(equations, {"x"});
    CHECK(res.status == Status::NoSolution);
    CHECK(res.values.empty());
}

TEST_CASE("system: constant-bearing near-zero 0 = c row keeps a condition") {
    // pi - 3.14159265358979 is below eval precision but not provably zero:
    // the constraint must surface as an "inconsistent unless" warning, never
    // be dropped silently (previous behavior).
    const auto equations =
        eqs({"x + y = pi", "x + y = 3.14159265358979", "x = 0"});
    const SystemSolveResult res = solve_system(equations, {"x", "y"});
    REQUIRE(res.status == Status::Solved);
    CHECK(structurally_equal(res.values.at("x"), expected("0")));
    CHECK(structurally_equal(res.values.at("y"), expected("pi")));
    CHECK(any_warning_contains(res, "inconsistent unless"));
}

TEST_CASE("system: coefficient overflow degrades to Unsolved, never throws") {
    // Regression: elimination folds like 3037000500^2 overflow the 64-bit
    // rational and the OverflowError escaped solve_system to the caller.
    const auto equations =
        eqs({"3037000500*x + y = 1", "x - 3037000500*y = 2"});
    SystemSolveResult res;
    REQUIRE_NOTHROW(res = solve_system(equations, {"x", "y"}));
    CHECK(res.status == Status::Unsolved);
    CHECK(res.values.empty());
    CHECK(any_warning_contains(res, "overflowed 64-bit rationals"));
}

TEST_CASE("system: conditional warnings are printed simplified") {
    // Regression: the leftover row printed as "y - (-y - 1) - 3" because row
    // updates were not expanded; it must reach the warning as "2*y - 2".
    const auto equations = eqs({"x + y = 3", "x - y = 1"});
    const SystemSolveResult res = solve_system(equations, {"x"});
    REQUIRE(res.status == Status::Solved);  // y is a parameter, not requested
    CHECK(any_warning_contains(res, "inconsistent unless 2*y - 2 = 0"));
    CHECK(!any_warning_contains(res, "(-y - 1)"));
}
