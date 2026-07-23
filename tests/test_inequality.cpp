// Inequality-solver tests: polynomial and rational inequalities reduced to
// interval solution sets by breakpoint sign analysis, plus the degenerate
// all-reals / no-solution / single-point cases.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>

#include "mathsolver/inequality.hpp"
#include "mathsolver/parser.hpp"

using namespace mathsolver;
using Catch::Matchers::ContainsSubstring;

namespace {

// Solve "<lhs> <op> <rhs>" given as two expression strings, format the set.
std::string solve_fmt(const std::string& lhs, IneqOp op, const std::string& rhs,
                      std::string_view var = "") {
    const IneqResult r =
        solve_inequality(parse_expression(lhs), parse_expression(rhs), op, var);
    return format_solution_set(r, /*latex=*/false);
}

} // namespace

TEST_CASE("quadratic inequalities give the expected intervals") {
    // x^2 < 4  ->  (-2, 2)
    CHECK(solve_fmt("x^2", IneqOp::Lt, "4") == "x ∈ (-2, 2)");
    // x^2 > 4  ->  (-inf, -2) ∪ (2, inf)
    CHECK(solve_fmt("x^2", IneqOp::Gt, "4") == "x ∈ (-∞, -2) ∪ (2, ∞)");
    // x^2 <= 4 ->  [-2, 2]  (roots included)
    CHECK(solve_fmt("x^2", IneqOp::Le, "4") == "x ∈ [-2, 2]");
    // x^2 >= 4 ->  (-inf, -2] ∪ [2, inf)
    CHECK(solve_fmt("x^2", IneqOp::Ge, "4") == "x ∈ (-∞, -2] ∪ [2, ∞)");
}

TEST_CASE("linear and factored polynomial inequalities") {
    // 2x + 1 > 5  ->  x > 2  ->  (2, inf)
    CHECK(solve_fmt("2*x + 1", IneqOp::Gt, "5") == "x ∈ (2, ∞)");
    // 3 - x >= 0  ->  x <= 3  ->  (-inf, 3]
    CHECK(solve_fmt("3 - x", IneqOp::Ge, "0") == "x ∈ (-∞, 3]");
    // (x-1)(x-3) < 0  ->  (1, 3)
    CHECK(solve_fmt("(x-1)*(x-3)", IneqOp::Lt, "0") == "x ∈ (1, 3)");
    // x^3 - x > 0  ->  (-1, 0) ∪ (1, inf)
    CHECK(solve_fmt("x^3 - x", IneqOp::Gt, "0") == "x ∈ (-1, 0) ∪ (1, ∞)");
}

TEST_CASE("degenerate solution sets") {
    // x^2 >= 0 is all reals; x^2 < 0 is empty; x^2 <= 0 is the single point {0}.
    CHECK(solve_fmt("x^2", IneqOp::Ge, "0") == "all real x");
    CHECK(solve_fmt("x^2", IneqOp::Lt, "0") == "no solution");
    CHECK(solve_fmt("x^2", IneqOp::Le, "0") == "x ∈ {0}");
    // A true constant relation.
    CHECK(solve_fmt("3", IneqOp::Lt, "5") == "all real x");
    CHECK(solve_fmt("5", IneqOp::Lt, "3") == "no solution");
}

TEST_CASE("rational inequalities exclude poles") {
    // 1/x > 0  ->  (0, inf)
    CHECK(solve_fmt("1/x", IneqOp::Gt, "0") == "x ∈ (0, ∞)");
    // (x-2)/(x+1) <= 0  ->  (-1, 2]  (x = -1 is a pole, excluded; x = 2 a zero)
    CHECK(solve_fmt("(x-2)/(x+1)", IneqOp::Le, "0") == "x ∈ (-1, 2]");
    // A removable hole stays excluded: (x^2-1)/(x-1) > 0 -> (-1, 1) ∪ (1, inf)
    CHECK(solve_fmt("(x^2-1)/(x-1)", IneqOp::Gt, "0") == "x ∈ (-1, 1) ∪ (1, ∞)");
}

TEST_CASE("irrational roots print exactly, and variable inference") {
    // x^2 < 2  ->  (-sqrt2, sqrt2), endpoints as exact radicals.
    const IneqResult r =
        solve_inequality(parse_expression("x^2"), parse_expression("2"), IneqOp::Lt);
    REQUIRE(r.status == IneqResult::Status::Solved);
    const std::string s = format_solution_set(r, false);
    CHECK_THAT(s, ContainsSubstring("2^(1/2)") || ContainsSubstring("sqrt"));

    // A single non-x variable is inferred.
    CHECK(solve_fmt("t - 3", IneqOp::Gt, "0") == "t ∈ (3, ∞)");
    // Multiple variables is unsolved.
    const IneqResult two = solve_inequality(parse_expression("x + y"),
                                            parse_expression("0"), IneqOp::Lt);
    CHECK(two.status == IneqResult::Status::Unsolved);
}

TEST_CASE("LaTeX rendering") {
    const IneqResult r =
        solve_inequality(parse_expression("x^2"), parse_expression("4"), IneqOp::Gt);
    const std::string tex = format_solution_set(r, /*latex=*/true);
    CHECK_THAT(tex, ContainsSubstring("\\in"));
    CHECK_THAT(tex, ContainsSubstring("\\cup"));
    CHECK_THAT(tex, ContainsSubstring("\\infty"));
}
