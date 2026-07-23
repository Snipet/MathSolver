// Continued-fraction tests: finite expansions of rationals with exact
// convergents, the periodic expansion of sqrt(n), and the numeric fallback —
// all cross-checked against known values and the defining recurrence.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <numbers>
#include <vector>

#include "mathsolver/cfrac.hpp"
#include "mathsolver/errors.hpp"

using namespace mathsolver;
using Catch::Matchers::ContainsSubstring;

namespace {

using LL = std::vector<long long>;
LL terms_of(const CFrac& cf) { return cf.terms; }

// Reconstruct the value of a convergent p/q as a double.
double value(std::pair<long long, long long> c) {
    return static_cast<double>(c.first) / static_cast<double>(c.second);
}

} // namespace

TEST_CASE("rational continued fractions are finite and exact") {
    // 355/113 = [3; 7, 16]; the last convergent is the input itself.
    const CFrac a = cf_rational(355, 113);
    CHECK(a.kind == CFrac::Kind::Rational);
    CHECK(a.exact);
    CHECK(terms_of(a) == LL{3, 7, 16});
    REQUIRE(!a.convergents.empty());
    CHECK(a.convergents.back().first == 355);
    CHECK(a.convergents.back().second == 113);
    // Convergents step through the classic pi approximations.
    CHECK(a.convergents[0] == std::make_pair<long long, long long>(3, 1));
    CHECK(a.convergents[1] == std::make_pair<long long, long long>(22, 7));

    // Integers are a single term.
    CHECK(terms_of(cf_rational(7, 1)) == LL{7});
    // 43/19 = [2; 3, 1, 4].
    CHECK(terms_of(cf_rational(43, 19)) == LL{2, 3, 1, 4});
    // Sign handling: -7/2 = [-4; 2] (a0 = floor, the rest positive).
    CHECK(terms_of(cf_rational(-7, 2)) == LL{-4, 2});
    // Denominator sign is normalized: 3/-4 == -3/4.
    CHECK(terms_of(cf_rational(3, -4)) == terms_of(cf_rational(-3, 4)));

    CHECK_THROWS_AS(cf_rational(1, 0), DivisionByZeroError);
}

TEST_CASE("sqrt continued fractions are periodic") {
    // sqrt(2) = [1; (2)].
    const CFrac s2 = cf_sqrt(2);
    CHECK(s2.kind == CFrac::Kind::Surd);
    CHECK(s2.exact);
    CHECK(s2.period_start == 1);
    CHECK(terms_of(s2) == LL{1, 2});
    // A convergent of sqrt(2) is near the true value, and 17/12 appears.
    bool has_17_12 = false;
    for (auto c : s2.convergents)
        if (c.first == 17 && c.second == 12) has_17_12 = true;
    CHECK(has_17_12);

    // Known periods (Wikipedia's canonical table).
    CHECK(terms_of(cf_sqrt(3)) == LL{1, 1, 2});    // [1; (1, 2)]
    CHECK(terms_of(cf_sqrt(7)) == LL{2, 1, 1, 1, 4}); // [2; (1,1,1,4)]
    CHECK(terms_of(cf_sqrt(23)) == LL{4, 1, 3, 1, 8});

    // Golden-ratio cousin: sqrt(N) with a long period still terminates and its
    // best convergent approximates the root.
    const CFrac s = cf_sqrt(61);
    const double approx = value(s.convergents.back());
    CHECK(std::abs(approx - std::sqrt(61.0)) < 1e-6);

    // Perfect squares degenerate to a single rational term.
    const CFrac s9 = cf_sqrt(9);
    CHECK(s9.kind == CFrac::Kind::Rational);
    CHECK(terms_of(s9) == LL{3});
}

TEST_CASE("numeric continued fractions approximate the value") {
    // pi -> [3; 7, 15, 1, 292, …]; the third convergent is 355/113.
    const CFrac p = cf_numeric(std::numbers::pi, 8);
    CHECK(p.kind == CFrac::Kind::Numeric);
    CHECK_FALSE(p.exact);
    CHECK(p.terms[0] == 3);
    CHECK(p.terms[1] == 7);
    CHECK(p.terms[2] == 15);
    bool has_355_113 = false;
    for (auto c : p.convergents)
        if (c.first == 355 && c.second == 113) has_355_113 = true;
    CHECK(has_355_113);

    // Every convergent is close to pi and the last is the closest.
    CHECK(std::abs(value(p.convergents.back()) - std::numbers::pi) < 1e-6);

    // e = [2; 1, 2, 1, 1, 4, 1, 1, 6, …].
    const CFrac e = cf_numeric(std::numbers::e, 9);
    CHECK(e.terms[0] == 2);
    CHECK(e.terms[1] == 1);
    CHECK(e.terms[2] == 2);
    CHECK(e.terms[3] == 1);
    CHECK(e.terms[4] == 1);
    CHECK(e.terms[5] == 4);

    // An exact integer double terminates immediately.
    CHECK(terms_of(cf_numeric(5.0, 10)) == LL{5});
}

TEST_CASE("formatting shows periods and truncation") {
    CHECK(format_cfrac(cf_rational(355, 113)) == "[3; 7, 16]");
    CHECK(format_cfrac(cf_rational(7, 1)) == "[7]");
    CHECK(format_cfrac(cf_sqrt(2)) == "[1; (2)]");
    CHECK(format_cfrac(cf_sqrt(6)) == "[2; (2, 4)]");
    CHECK_THAT(format_cfrac(cf_numeric(std::numbers::pi, 5)), ContainsSubstring("…"));

    CHECK_THAT(format_cfrac_latex(cf_sqrt(2)), ContainsSubstring("\\overline{2}"));
}
