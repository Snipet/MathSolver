#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/expr.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/simplify.hpp"
#include "mathsolver/solver.hpp"

using namespace mathsolver;
using Status = SolveResult::Status;

namespace {

Equation eq(std::string_view s) {
    return parse_equation(s);
}

/// Expected solution values are written as strings and normalized the same
/// way the solver normalizes its results: through simplify().
Expr expected(std::string_view s) {
    return simplify(parse_expression(s));
}

/// Verify a solution by numeric substitution into the original equation.
/// The value is bound numerically (not spliced in as an Expr) so that e.g.
/// a 15-digit numeric root raised to the third power cannot overflow the
/// exact rational folds inside the factories.
void check_solution_numerically(const Equation& equation, std::string_view var,
                                const Expr& value, const Bindings& extra = {}) {
    Bindings bindings = extra;
    bindings[std::string(var)] = evaluate(value, extra);
    const double l = evaluate(equation.lhs, bindings);
    const double r = evaluate(equation.rhs, bindings);
    INFO("solution " << debug_string(value) << ": lhs = " << l << ", rhs = " << r);
    CHECK(std::abs(l - r) <= 1e-6 * std::max({1.0, std::abs(l), std::abs(r)}));
}

void check_all_solutions_numerically(const Equation& equation, std::string_view var,
                                     const SolveResult& res, const Bindings& extra = {}) {
    REQUIRE(!res.solutions.empty());
    for (const Solution& s : res.solutions) {
        check_solution_numerically(equation, var, s.value, extra);
    }
}

bool warning_mentions(const SolveResult& res, std::string_view needle) {
    return std::any_of(res.warnings.begin(), res.warnings.end(), [&](const std::string& w) {
        return w.find(needle) != std::string::npos;
    });
}

void expect_values(const SolveResult& res, const std::vector<std::string_view>& wants) {
    REQUIRE(res.solutions.size() == wants.size());
    for (std::size_t i = 0; i < wants.size(); ++i) {
        const Expr want = expected(wants[i]);
        INFO("solution " << i << ": got " << debug_string(res.solutions[i].value)
                         << ", expected " << debug_string(want));
        CHECK(structurally_equal(res.solutions[i].value, want));
        CHECK(res.solutions[i].exact);
    }
}

double solution_as_double(const Solution& s) {
    return evaluate(s.value);
}

} // namespace

// ---------------------------------------------------------------------------
// Trivial cases: identity, contradiction, missing symbol
// ---------------------------------------------------------------------------

TEST_CASE("solve: x = x is an identity") {
    const SolveResult res = solve(eq("x = x"), "x");
    CHECK(res.status == Status::AllReals);
    CHECK(res.solutions.empty());
}

TEST_CASE("solve: 0 = 1 is a contradiction") {
    const SolveResult res = solve(eq("0 = 1"), "x");
    CHECK(res.status == Status::NoRealSolution);
    CHECK(res.solutions.empty());
}

TEST_CASE("solve: x + 1 = x is a contradiction") {
    const SolveResult res = solve(eq("x + 1 = x"), "x");
    CHECK(res.status == Status::NoRealSolution);
}

TEST_CASE("solve: 2 = 2 for x holds for all reals") {
    const SolveResult res = solve(eq("2 = 2"), "x");
    CHECK(res.status == Status::AllReals);
}

TEST_CASE("solve: equation without the symbol is Unsolved with a warning") {
    const SolveResult res = solve(eq("a = b"), "x");
    CHECK(res.status == Status::Unsolved);
    CHECK(warning_mentions(res, "x"));
}

// ---------------------------------------------------------------------------
// Linear equations
// ---------------------------------------------------------------------------

TEST_CASE("solve: 2x + 3 = 7") {
    const Equation e = eq("2x + 3 = 7");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    CHECK(res.method == "linear");
    expect_values(res, {"2"});
    check_all_solutions_numerically(e, "x", res);
}

TEST_CASE("solve: a*x + b = 0 for x gives -b/a with a nonzero-divisor warning") {
    const Equation e = eq("a*x + b = 0");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    CHECK(res.method == "linear");
    expect_values(res, {"-b/a"});
    CHECK(warning_mentions(res, "!= 0"));
    check_all_solutions_numerically(e, "x", res, Bindings{{"a", 2.5}, {"b", -1.25}});
}

TEST_CASE("solve: x + y = 3 for x") {
    const Equation e = eq("x + y = 3");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    expect_values(res, {"3 - y"});
    check_all_solutions_numerically(e, "x", res, Bindings{{"y", 0.75}});
}

// ---------------------------------------------------------------------------
// Quadratics
// ---------------------------------------------------------------------------

TEST_CASE("solve: x^2 = 4 gives -2 and 2, sorted ascending") {
    const Equation e = eq("x^2 = 4");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    CHECK(res.method == "quadratic formula");
    expect_values(res, {"-2", "2"});
    check_all_solutions_numerically(e, "x", res);
}

TEST_CASE("solve: x^2 = 2 gives exact +-sqrt(2)") {
    const Equation e = eq("x^2 = 2");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    expect_values(res, {"-sqrt(2)", "sqrt(2)"});
    check_all_solutions_numerically(e, "x", res);
}

TEST_CASE("solve: x^2 + x + 1 = 0 has no real solutions") {
    const SolveResult res = solve(eq("x^2 + x + 1 = 0"), "x");
    CHECK(res.status == Status::NoRealSolution);
    CHECK(res.solutions.empty());
}

TEST_CASE("solve: x^2 = -1 has no real solutions") {
    const SolveResult res = solve(eq("x^2 = -1"), "x");
    CHECK(res.status == Status::NoRealSolution);
    CHECK(res.solutions.empty());
}

TEST_CASE("solve: x^2 = 0 gives the single root 0") {
    const Equation e = eq("x^2 = 0");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    expect_values(res, {"0"});
    check_all_solutions_numerically(e, "x", res);
}

TEST_CASE("solve: x^2 + 2x + 1 = 0 collapses the zero discriminant to one root") {
    const Equation e = eq("x^2 + 2x + 1 = 0");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    expect_values(res, {"-1"});
    check_all_solutions_numerically(e, "x", res);
}

TEST_CASE("solve: 2x^2 - 3x - 2 = 0 gives rational roots") {
    const Equation e = eq("2x^2 - 3x - 2 = 0");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    expect_values(res, {"-1/2", "2"});
    check_all_solutions_numerically(e, "x", res);
}

TEST_CASE("solve: x^2 = pi extracts the numeric square factor from the discriminant") {
    const Equation e = eq("x^2 = pi");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    expect_values(res, {"-sqrt(pi)", "sqrt(pi)"});
    check_all_solutions_numerically(e, "x", res);
}

TEST_CASE("solve: symbolic discriminant keeps both roots with a warning") {
    const Equation e = eq("x^2 + b*x + 1 = 0");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    REQUIRE(res.solutions.size() == 2);
    CHECK(warning_mentions(res, ">= 0"));
    // b = 3 makes the discriminant positive; both roots must then check out.
    check_all_solutions_numerically(e, "x", res, Bindings{{"b", 3.0}});
}

TEST_CASE("solve: symbolic leading coefficient adds a nonzero warning") {
    const Equation e = eq("a*x^2 - 4a = 0");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    CHECK(warning_mentions(res, "!= 0"));
    REQUIRE(res.solutions.size() == 2);
    check_all_solutions_numerically(e, "x", res, Bindings{{"a", 1.5}});
}

// ---------------------------------------------------------------------------
// Higher-degree polynomials
// ---------------------------------------------------------------------------

TEST_CASE("solve: x^3 - 6x^2 + 11x - 6 = 0 gives 1, 2, 3 exactly") {
    const Equation e = eq("x^3 - 6x^2 + 11x - 6 = 0");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    CHECK(res.method == "rational roots + quadratic");
    expect_values(res, {"1", "2", "3"});
    check_all_solutions_numerically(e, "x", res);
}

TEST_CASE("solve: x^4 - 5x^2 + 4 = 0 via poly-in-x^2 substitution") {
    const Equation e = eq("x^4 - 5x^2 + 4 = 0");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    CHECK(res.method.find("substitution") != std::string::npos);
    expect_values(res, {"-2", "-1", "1", "2"});
    check_all_solutions_numerically(e, "x", res);
}

TEST_CASE("solve: x^3 = -8 gives the sign-extracted odd root -2") {
    const Equation e = eq("x^3 = -8");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    expect_values(res, {"-2"});
    check_all_solutions_numerically(e, "x", res);
}

TEST_CASE("solve: x^4 = 16 gives -2 and 2") {
    const Equation e = eq("x^4 = 16");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    expect_values(res, {"-2", "2"});
    check_all_solutions_numerically(e, "x", res);
}

TEST_CASE("solve: x^4 + 1 = 0 has no real solutions") {
    const SolveResult res = solve(eq("x^4 + 1 = 0"), "x");
    CHECK(res.status == Status::NoRealSolution);
}

TEST_CASE("solve: x^3 + x^2 = 0 peels the zero root") {
    const Equation e = eq("x^3 + x^2 = 0");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    expect_values(res, {"-1", "0"});
    check_all_solutions_numerically(e, "x", res);
}

TEST_CASE("solve: (x-1)^3 = 0 reports the triple root once") {
    const Equation e = eq("x^3 - 3x^2 + 3x - 1 = 0");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    expect_values(res, {"1"});
    check_all_solutions_numerically(e, "x", res);
}

TEST_CASE("solve: cubic with no rational roots falls back to numeric") {
    const Equation e = eq("x^3 + x = 1");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::NumericOnly);
    REQUIRE(res.solutions.size() == 1);
    CHECK_FALSE(res.solutions.front().exact);
    CHECK_THAT(solution_as_double(res.solutions.front()),
               Catch::Matchers::WithinAbs(0.6823278038280193, 1e-6));
    check_all_solutions_numerically(e, "x", res);
}

// ---------------------------------------------------------------------------
// Isolation path
// ---------------------------------------------------------------------------

TEST_CASE("solve: ln(x+1) = 2 gives e^2 - 1 exactly") {
    const Equation e = eq("ln(x+1) = 2");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    CHECK(res.method == "isolation");
    expect_values(res, {"e^2 - 1"});
    check_all_solutions_numerically(e, "x", res);
}

TEST_CASE("solve: e^x = 5 gives ln(5)") {
    const Equation e = eq("e^x = 5");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    CHECK(res.method == "isolation");
    expect_values(res, {"ln(5)"});
    check_all_solutions_numerically(e, "x", res);
}

TEST_CASE("solve: e^x = 0 has no real solutions") {
    const SolveResult res = solve(eq("e^x = 0"), "x");
    CHECK(res.status == Status::NoRealSolution);
}

TEST_CASE("solve: 2^x = 8 gives ln(8)/ln(2)") {
    const Equation e = eq("2^x = 8");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    expect_values(res, {"ln(8)/ln(2)"});
    check_all_solutions_numerically(e, "x", res);
}

TEST_CASE("solve: abs(x) = 3 gives -3 and 3") {
    const Equation e = eq("abs(x) = 3");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    CHECK(res.method == "isolation");
    expect_values(res, {"-3", "3"});
    check_all_solutions_numerically(e, "x", res);
}

TEST_CASE("solve: abs(x) = -2 has no real solutions") {
    const SolveResult res = solve(eq("abs(x) = -2"), "x");
    CHECK(res.status == Status::NoRealSolution);
    CHECK(res.solutions.empty());
}

TEST_CASE("solve: sqrt(x) = 3 gives 9") {
    const Equation e = eq("sqrt(x) = 3");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    CHECK(res.method == "isolation");
    expect_values(res, {"9"});
    check_all_solutions_numerically(e, "x", res);
}

TEST_CASE("solve: sqrt(x) = -1 has no real solutions") {
    const SolveResult res = solve(eq("sqrt(x) = -1"), "x");
    CHECK(res.status == Status::NoRealSolution);
}

TEST_CASE("solve: 1/x = 4 gives 1/4") {
    const Equation e = eq("1/x = 4");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    expect_values(res, {"1/4"});
    check_all_solutions_numerically(e, "x", res);
}

TEST_CASE("solve: sin(x) = 1/2 gives the principal pi/6 with a periodicity note") {
    const Equation e = eq("sin(x) = 1/2");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    CHECK(res.method == "isolation");
    expect_values(res, {"pi/6"});
    REQUIRE(res.solutions.size() == 1);
    CHECK(res.solutions.front().note.find("2*pi*n") != std::string::npos);
    CHECK(res.solutions.front().note.find("principal") != std::string::npos);
    check_all_solutions_numerically(e, "x", res);
}

TEST_CASE("solve: sin(x) = 2 has no real solutions") {
    const SolveResult res = solve(eq("sin(x) = 2"), "x");
    CHECK(res.status == Status::NoRealSolution);
}

TEST_CASE("solve: cos(x) = 1/2 gives pi/3 with the +- family in the note") {
    const Equation e = eq("cos(x) = 1/2");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    expect_values(res, {"pi/3"});
    CHECK(res.solutions.front().note.find("2*pi*n") != std::string::npos);
    check_all_solutions_numerically(e, "x", res);
}

TEST_CASE("solve: tan(x) = 1 gives pi/4 with period pi in the note") {
    const Equation e = eq("tan(x) = 1");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    expect_values(res, {"pi/4"});
    REQUIRE(res.solutions.size() == 1);
    const std::string& note = res.solutions.front().note;
    CHECK(note.find("+ pi*n") != std::string::npos);
    CHECK(note.find("2*pi*n") == std::string::npos); // tan's period is pi, not 2*pi
    check_all_solutions_numerically(e, "x", res);
}

TEST_CASE("solve: sin(2x) = 1 peels through the argument with an equation-level warning") {
    const Equation e = eq("sin(2x) = 1");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    expect_values(res, {"pi/4"});
    CHECK(warning_mentions(res, "principal"));
    check_all_solutions_numerically(e, "x", res);
}

TEST_CASE("solve: sin(a*x) = 1/3 for x keeps the symbolic divisor with a warning") {
    const Equation e = eq("sin(a*x) = 1/3");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    expect_values(res, {"asin(1/3)/a"});
    CHECK(warning_mentions(res, "!= 0"));
    check_all_solutions_numerically(e, "x", res, Bindings{{"a", 1.75}});
}

TEST_CASE("solve: sin(x)^2 = 1/4 branches through the even power") {
    const Equation e = eq("sin(x)^2 = 1/4");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    expect_values(res, {"-pi/6", "pi/6"});
    check_all_solutions_numerically(e, "x", res);
}

TEST_CASE("solve: asin(x) = pi/2 gives 1; out-of-range asin has no solution") {
    const Equation e = eq("asin(x) = pi/2");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    expect_values(res, {"1"});
    check_all_solutions_numerically(e, "x", res);

    const SolveResult out_of_range = solve(eq("asin(x) = 2"), "x");
    CHECK(out_of_range.status == Status::NoRealSolution);
}

TEST_CASE("solve: sinh(x) = 0 gives 0") {
    const Equation e = eq("sinh(x) = 0");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    expect_values(res, {"0"});
    check_all_solutions_numerically(e, "x", res);
}

TEST_CASE("solve: cosh(x) = 0 has no real solutions") {
    const SolveResult res = solve(eq("cosh(x) = 0"), "x");
    CHECK(res.status == Status::NoRealSolution);
}

TEST_CASE("solve: cosh(x) = 1 gives the single root 0") {
    const Equation e = eq("cosh(x) = 1");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::Solved);
    expect_values(res, {"0"});
    check_all_solutions_numerically(e, "x", res);
}

TEST_CASE("solve: tanh(x) = 2 has no real solutions") {
    const SolveResult res = solve(eq("tanh(x) = 2"), "x");
    CHECK(res.status == Status::NoRealSolution);
}

// ---------------------------------------------------------------------------
// Numeric fallback
// ---------------------------------------------------------------------------

TEST_CASE("solve: cos(x) = x is numeric-only with the Dottie number") {
    const Equation e = eq("cos(x) = x");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::NumericOnly);
    CHECK(res.method == "numeric (Newton/bisection)");
    REQUIRE(res.solutions.size() == 1);
    const Solution& s = res.solutions.front();
    CHECK_FALSE(s.exact);
    CHECK(s.value->kind() == Kind::Number);
    CHECK_THAT(solution_as_double(s), Catch::Matchers::WithinAbs(0.7390851332151607, 1e-6));
    CHECK(warning_mentions(res, "-100"));
    check_all_solutions_numerically(e, "x", res);
}

TEST_CASE("solve: x*e^x = 1 is numeric-only with the Omega constant") {
    const Equation e = eq("x*e^x = 1");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::NumericOnly);
    REQUIRE(res.solutions.size() == 1);
    CHECK_FALSE(res.solutions.front().exact);
    CHECK_THAT(solution_as_double(res.solutions.front()),
               Catch::Matchers::WithinAbs(0.5671432904097838, 1e-6));
    check_all_solutions_numerically(e, "x", res);
}

TEST_CASE("solve: sin(x) = x/2 finds all three roots in range, sorted") {
    const Equation e = eq("sin(x) = x/2");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::NumericOnly);
    REQUIRE(res.solutions.size() == 3);
    CHECK_THAT(solution_as_double(res.solutions[0]),
               Catch::Matchers::WithinAbs(-1.895494267033981, 1e-6));
    CHECK_THAT(solution_as_double(res.solutions[1]), Catch::Matchers::WithinAbs(0.0, 1e-9));
    CHECK_THAT(solution_as_double(res.solutions[2]),
               Catch::Matchers::WithinAbs(1.895494267033981, 1e-6));
    check_all_solutions_numerically(e, "x", res);
}

TEST_CASE("solve: even-multiplicity numeric root is caught via the |f| minimum") {
    const Equation e = eq("e^x*(x - 1/3)^2 = 0");
    const SolveResult res = solve(e, "x");
    CHECK(res.status == Status::NumericOnly);
    REQUIRE(res.solutions.size() == 1);
    CHECK_THAT(solution_as_double(res.solutions.front()),
               Catch::Matchers::WithinAbs(1.0 / 3.0, 1e-5));
}

TEST_CASE("solve: e^x = x has no roots anywhere and reports Unsolved") {
    const SolveResult res = solve(eq("e^x = x"), "x");
    CHECK(res.status == Status::Unsolved);
    CHECK(res.solutions.empty());
    CHECK(warning_mentions(res, "not reported"));
}

TEST_CASE("solve: numeric fallback refuses extra free symbols") {
    const SolveResult res = solve(eq("sin(x) + a*x = 0"), "x");
    CHECK(res.status == Status::Unsolved);
    CHECK(res.solutions.empty());
    CHECK(warning_mentions(res, "a"));
}

TEST_CASE("solve_numeric: direct call with a custom interval") {
    const Equation e = eq("x^2 = 4");
    NumericOptions opts;
    opts.lo = 0.0;
    opts.hi = 10.0;
    const SolveResult res = solve_numeric(e, "x", opts);
    CHECK(res.status == Status::NumericOnly);
    REQUIRE(res.solutions.size() == 1); // -2 lies outside [0, 10]
    CHECK_FALSE(res.solutions.front().exact);
    CHECK_THAT(solution_as_double(res.solutions.front()),
               Catch::Matchers::WithinAbs(2.0, 1e-8));
    CHECK(warning_mentions(res, "not reported"));
}

TEST_CASE("solve_numeric: domain gaps are skipped") {
    // ln(x) is undefined for x <= 0; the scan must skip the gap, not die.
    const Equation e = eq("ln(x) = 1");
    const SolveResult res = solve_numeric(e, "x");
    CHECK(res.status == Status::NumericOnly);
    REQUIRE(res.solutions.size() == 1);
    CHECK_THAT(solution_as_double(res.solutions.front()),
               Catch::Matchers::WithinAbs(2.718281828459045, 1e-6));
    check_all_solutions_numerically(e, "x", res);
}

TEST_CASE("solve_numeric: trivial classifications still apply") {
    CHECK(solve_numeric(eq("x = x"), "x").status == Status::AllReals);
    CHECK(solve_numeric(eq("0 = 1"), "x").status == Status::NoRealSolution);
}

// ---------------------------------------------------------------------------
// Result hygiene
// ---------------------------------------------------------------------------

TEST_CASE("solve: solution values are already simplified") {
    for (const auto& [input, var] : std::vector<std::pair<std::string_view, std::string_view>>{
             {"2x + 3 = 7", "x"},
             {"x^2 = 2", "x"},
             {"ln(x+1) = 2", "x"},
             {"sin(x) = 1/2", "x"},
             {"a*x + b = 0", "x"},
         }) {
        const SolveResult res = solve(eq(input), var);
        for (const Solution& s : res.solutions) {
            INFO(input << " -> " << debug_string(s.value));
            CHECK(structurally_equal(s.value, simplify(s.value)));
        }
    }
}

TEST_CASE("solve: exact roots survive verification untouched across a battery") {
    for (const std::string_view input : {
             "3x - 12 = 0",
             "x^2 - 9 = 0",
             "x^2 - 3 = 0",
             "x^3 - 6x^2 + 11x - 6 = 0",
             "x^4 - 5x^2 + 4 = 0",
             "abs(x) = 3",
             "sqrt(x) = 3",
             "e^x = 5",
         }) {
        const Equation e = eq(input);
        const SolveResult res = solve(e, "x");
        INFO(input);
        CHECK(res.status == Status::Solved);
        check_all_solutions_numerically(e, "x", res);
        CHECK_FALSE(warning_mentions(res, "dropped"));
    }
}
