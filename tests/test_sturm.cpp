// Sturm real-root counting and isolation tests.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "mathsolver/errors.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/rational.hpp"
#include "mathsolver/sturm.hpp"

using namespace mathsolver;
using Catch::Matchers::WithinAbs;

namespace {

int count(const std::string& poly) {
    return sturm_root_count(parse_expression(poly), "x");
}
int count_in(const std::string& poly, const Rational& lo, const Rational& hi) {
    return sturm_root_count(parse_expression(poly), "x", lo, hi);
}
std::vector<double> approxes(const std::string& poly) {
    std::vector<double> xs;
    for (const RootInterval& r : sturm_isolate_roots(parse_expression(poly), "x"))
        xs.push_back(r.approx);
    std::sort(xs.begin(), xs.end());
    return xs;
}

} // namespace

TEST_CASE("sturm: distinct real root counts over R") {
    CHECK(count("x^2 - 2") == 2);
    CHECK(count("x^2 + 1") == 0);
    CHECK(count("(x - 1)*(x - 2)*(x - 3)") == 3);
    CHECK(count("x^3 - x") == 3);
    CHECK(count("x^5 - x - 1") == 1);        // one real root, four complex
    CHECK(count("x^4 - 5*x^2 + 4") == 4);    // ±1, ±2
    // Multiplicity does not inflate the count: (x^2 - 2)^2 has two DISTINCT roots.
    CHECK(count("(x^2 - 2)^2") == 2);
    CHECK(count("(x - 1)^3") == 1);
    CHECK(count("5") == 0); // nonzero constant: no roots
}

TEST_CASE("sturm: counts on a half-open interval") {
    CHECK(count_in("x^2 - 2", Rational(0), Rational(5)) == 1);   // only +sqrt2
    CHECK(count_in("x^2 - 2", Rational(-5), Rational(5)) == 2);
    CHECK(count_in("(x-1)*(x-2)*(x-3)", Rational(0), Rational(2)) == 2); // 1 and 2
    CHECK(count_in("(x-1)*(x-2)*(x-3)", Rational(2), Rational(3)) == 1); // just 3
    CHECK(count_in("x^3 - x", Rational(-1, 2), Rational(2)) == 2);       // 0 and 1
}

TEST_CASE("sturm: isolate exact rational roots") {
    const auto roots = sturm_isolate_roots(parse_expression("2*x^2 - 3*x + 1"), "x");
    REQUIRE(roots.size() == 2);
    CHECK(roots[0].exact);
    CHECK(roots[1].exact);
    CHECK(roots[0].lo == Rational(1, 2));
    CHECK(roots[1].lo == Rational(1));

    const auto cubic = sturm_isolate_roots(parse_expression("(x-1)*(x-2)*(x-3)"), "x");
    REQUIRE(cubic.size() == 3);
    for (const auto& r : cubic) CHECK(r.exact);
    CHECK(cubic[0].lo == Rational(1));
    CHECK(cubic[2].lo == Rational(3));

    // A triple root reported once, exactly.
    const auto triple = sturm_isolate_roots(parse_expression("(x - 1)^3"), "x");
    REQUIRE(triple.size() == 1);
    CHECK(triple[0].exact);
    CHECK(triple[0].lo == Rational(1));
}

TEST_CASE("sturm: isolate irrational roots numerically") {
    const auto sqrt2 = approxes("x^2 - 2");
    REQUIRE(sqrt2.size() == 2);
    CHECK_THAT(sqrt2[0], WithinAbs(-std::sqrt(2.0), 1e-7));
    CHECK_THAT(sqrt2[1], WithinAbs(std::sqrt(2.0), 1e-7));

    // The real root of x^3 - x - 1 (the plastic number, ~1.3247).
    const auto plastic = approxes("x^3 - x - 1");
    REQUIRE(plastic.size() == 1);
    CHECK_THAT(plastic[0], WithinAbs(1.3247179572, 1e-7));

    // Golden ratio and its conjugate: x^2 - x - 1.
    const auto golden = approxes("x^2 - x - 1");
    REQUIRE(golden.size() == 2);
    CHECK_THAT(golden[1], WithinAbs((1.0 + std::sqrt(5.0)) / 2.0, 1e-7));
}

TEST_CASE("sturm: every isolating interval brackets its root") {
    for (const char* poly : {"x^5 - 3*x - 1", "x^4 - 10*x^2 + 1", "x^3 - 2"}) {
        const auto roots = sturm_isolate_roots(parse_expression(poly), "x");
        for (const RootInterval& r : roots) {
            CHECK(r.lo <= r.hi);
            CHECK(r.approx >= r.lo.to_double() - 1e-9);
            CHECK(r.approx <= r.hi.to_double() + 1e-9);
        }
    }
}

TEST_CASE("sturm: mixed rational and irrational roots") {
    // x*(x - 1)*(x^2 - 2): roots 0, 1 (exact) and ±sqrt(2) (irrational).
    const auto roots = sturm_isolate_roots(
        parse_expression("x*(x - 1)*(x^2 - 2)"), "x");
    REQUIRE(roots.size() == 4);
    int exactCount = 0;
    for (const auto& r : roots) exactCount += r.exact ? 1 : 0;
    CHECK(exactCount == 2); // 0 and 1
    CHECK_THAT(roots.front().approx, WithinAbs(-std::sqrt(2.0), 1e-7));
    CHECK_THAT(roots.back().approx, WithinAbs(std::sqrt(2.0), 1e-7));
}

TEST_CASE("sturm: error paths") {
    CHECK_THROWS_AS(sturm_root_count(parse_expression("x + a"), "x"), Error);
    CHECK_THROWS_AS(sturm_isolate_roots(parse_expression("sin(x)"), "x"), Error);
    CHECK_THROWS_AS(sturm_isolate_roots(parse_expression("1/x"), "x"), Error);
    CHECK_THROWS_AS(
        sturm_root_count(parse_expression("x^2 - 1"), "x", Rational(2), Rational(1)),
        Error);
}
