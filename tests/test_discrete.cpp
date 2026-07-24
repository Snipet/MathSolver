// Discrete calculus tests: closed-form sums/products verified against
// direct accumulation, and recurrences verified by iterating the recurrence.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <string>
#include <vector>

#include "mathsolver/discrete.hpp"
#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/printer.hpp"
#include "mathsolver/simplify.hpp"

using namespace mathsolver;
using Catch::Matchers::ContainsSubstring;

namespace {

Expr P(const std::string& s) { return parse_expression(s); }

/// Closed form vs direct accumulation at several numeric upper bounds.
void check_sum(const std::string& term, long long lo,
               const std::vector<long long>& upper_bounds) {
    const SumResult r = sum_finite(P(term), "k", P(std::to_string(lo)), P("n"));
    INFO("sum of " << term << " from " << lo << " (method " << r.method << ")");
    REQUIRE(r.status == SumResult::Status::Exact);
    for (const long long n : upper_bounds) {
        double direct = 0.0;
        for (long long k = lo; k <= n; ++k) {
            direct += evaluate(P(term), Bindings{{"k", static_cast<double>(k)}});
        }
        const double closed =
            evaluate(r.value, Bindings{{"n", static_cast<double>(n)}});
        INFO("at n=" << n << ": closed " << closed << " direct " << direct);
        CHECK(std::abs(closed - direct) < 1e-6 * (1.0 + std::abs(direct)));
    }
}

/// Iterate a linear recurrence directly from its solved closed form.
void check_rsolve(const std::string& rec,
                  const std::vector<std::string>& conditions,
                  const std::vector<double>& expected_prefix) {
    const RsolveResult r = rsolve(rec, conditions);
    INFO("rsolve(" << rec << ") method " << r.method);
    for (std::size_t n = 0; n < expected_prefix.size(); ++n) {
        const double got =
            evaluate(r.solution, Bindings{{"n", static_cast<double>(n)}});
        INFO("a(" << n << "): got " << got << " want " << expected_prefix[n]);
        CHECK(std::abs(got - expected_prefix[n]) <
              1e-6 * (1.0 + std::abs(expected_prefix[n])));
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Sums
// ---------------------------------------------------------------------------

TEST_CASE("sum: polynomial closed forms (Faulhaber)") {
    check_sum("k", 1, {100, 1000});
    check_sum("k^2", 1, {100, 500});
    check_sum("k^3", 0, {80, 200});
    check_sum("2k + 1", 1, {150});
    check_sum("k^2 - k + 3", 5, {120});
}

TEST_CASE("sum: known identities hold exactly") {
    // Σ k from 1..100 = 5050; Σ k^2 from 1..100 = 338350.
    const SumResult s1 = sum_finite(P("k"), "k", P("1"), P("100"));
    REQUIRE(s1.status == SumResult::Status::Exact);
    CHECK(evaluate(s1.value, Bindings{}) == 5050.0);
    const SumResult s2 = sum_finite(P("k^2"), "k", P("1"), P("100"));
    CHECK(evaluate(s2.value, Bindings{}) == 338350.0);
}

TEST_CASE("sum: geometric and mixed closed forms") {
    check_sum("2^k", 0, {70});
    check_sum("(1/3)^k", 0, {80});
    check_sum("k * 2^k", 1, {75});
    check_sum("3^k + k", 0, {70});
}

TEST_CASE("sum: symbolic ratio yields the textbook formula") {
    const SumResult r = sum_finite(P("x^k"), "k", P("0"), P("n"));
    REQUIRE(r.status == SumResult::Status::Exact);
    // Compare against (x^{n+1} - 1)/(x - 1) numerically.
    for (const double x : {0.5, 2.0, -0.7}) {
        for (const double n : {4.0, 9.0}) {
            const double got = evaluate(r.value, Bindings{{"x", x}, {"n", n}});
            const double want = (std::pow(x, n + 1) - 1.0) / (x - 1.0);
            CHECK(std::abs(got - want) < 1e-9 * (1.0 + std::abs(want)));
        }
    }
    CHECK(!r.warnings.empty()); // pivot condition x != 1 reported
}

TEST_CASE("sum: infinite series") {
    // Geometric: Σ (1/2)^k from 0 = 2.
    const SumResult g = sum_infinite(P("(1/2)^k"), "k", P("0"));
    REQUIRE(g.status == SumResult::Status::Exact);
    CHECK(evaluate(g.value, Bindings{}) == 2.0);
    // With polynomial factor: Σ k (1/2)^k from 1 = 2.
    const SumResult kg = sum_infinite(P("k * (1/2)^k"), "k", P("1"));
    REQUIRE(kg.status == SumResult::Status::Exact);
    CHECK(evaluate(kg.value, Bindings{}) == 2.0);
    // Telescoping: Σ 1/(k(k+1)) from 1 = 1.
    const SumResult t = sum_infinite(P("1/(k*(k+1))"), "k", P("1"));
    REQUIRE(t.status == SumResult::Status::Exact);
    CHECK(evaluate(t.value, Bindings{}) == 1.0);
    // Σ 1/((k+1)(k+3)) from 0: apart gives 1/2[1/(k+1) - 1/(k+3)];
    // value = 1/2 (1 + 1/2) = 3/4.
    const SumResult t2 = sum_infinite(P("1/((k+1)*(k+3))"), "k", P("0"));
    REQUIRE(t2.status == SumResult::Status::Exact);
    CHECK(evaluate(t2.value, Bindings{}) == 0.75);
}

TEST_CASE("sum: divergence is reported") {
    CHECK(sum_infinite(P("2^k"), "k", P("0")).status ==
          SumResult::Status::Diverges);
    CHECK(sum_infinite(P("k"), "k", P("1")).status ==
          SumResult::Status::Diverges);
    // Harmonic: partial fractions do not cancel.
    CHECK(sum_infinite(P("1/(k+1)"), "k", P("0")).status ==
          SumResult::Status::Diverges);
}

TEST_CASE("sum: direct numeric ranges and empty ranges") {
    const SumResult direct = sum_finite(P("1/k"), "k", P("1"), P("10"));
    REQUIRE(direct.status == SumResult::Status::Exact);
    CHECK(std::abs(evaluate(direct.value, Bindings{}) - 2.9289682539682538) <
          1e-12);
    const SumResult empty = sum_finite(P("k"), "k", P("5"), P("2"));
    CHECK(evaluate(empty.value, Bindings{}) == 0.0);
}

// ---------------------------------------------------------------------------
// Products
// ---------------------------------------------------------------------------

TEST_CASE("product: numeric, constant, and geometric") {
    // 5! via k from 1..5.
    const SumResult f = product_finite(P("k"), "k", P("1"), P("5"));
    REQUIRE(f.status == SumResult::Status::Exact);
    CHECK(evaluate(f.value, Bindings{}) == 120.0);
    // Constant to a symbolic count.
    const SumResult c = product_finite(P("3"), "k", P("1"), P("n"));
    REQUIRE(c.status == SumResult::Status::Exact);
    CHECK(evaluate(c.value, Bindings{{"n", 4.0}}) == 81.0);
    // Geometric: Π 2^k for k=1..n = 2^{n(n+1)/2}.
    const SumResult g = product_finite(P("2^k"), "k", P("1"), P("n"));
    REQUIRE(g.status == SumResult::Status::Exact);
    CHECK(evaluate(g.value, Bindings{{"n", 4.0}}) == 1024.0);
    // Symbolic bound with a genuine k-dependence has no closed form yet.
    CHECK(product_finite(P("k"), "k", P("1"), P("n")).status ==
          SumResult::Status::Unsolved);
}

// ---------------------------------------------------------------------------
// Recurrences
// ---------------------------------------------------------------------------

TEST_CASE("rsolve: Fibonacci gets Binet's formula exactly") {
    const RsolveResult r = rsolve("a(n+2) = a(n+1) + a(n)", {"a(0)=0", "a(1)=1"});
    CHECK(r.order == 2);
    const std::vector<double> fib{0, 1, 1, 2, 3, 5, 8, 13, 21, 34, 55};
    for (std::size_t n = 0; n < fib.size(); ++n) {
        const double got =
            evaluate(r.solution, Bindings{{"n", static_cast<double>(n)}});
        CHECK(std::abs(got - fib[n]) < 1e-9 * (1.0 + fib[n]));
    }
}

TEST_CASE("rsolve: rational roots, repeated roots, and order one") {
    check_rsolve("a(n+1) = 2 a(n)", {"a(0)=3"}, {3, 6, 12, 24, 48});
    // (x-2)^2: a(n) = (c1 + c2 n) 2^n.
    check_rsolve("a(n+2) = 4 a(n+1) - 4 a(n)", {"a(0)=1", "a(1)=4"},
                 {1, 4, 12, 32, 80});
    check_rsolve("a(n+2) = 5 a(n+1) - 6 a(n)", {"a(0)=2", "a(1)=5"},
                 {2, 5, 13, 35, 97}); // 2^n + 3^n
}

TEST_CASE("rsolve: forcing terms with and without resonance") {
    // a(n+1) = 2a(n) + 1, a(0)=0 -> 2^n - 1.
    check_rsolve("a(n+1) = 2 a(n) + 1", {"a(0)=0"}, {0, 1, 3, 7, 15, 31});
    // Resonant geometric forcing: a(n+1) = 2a(n) + 2^n, a(0)=0 -> n 2^{n-1}.
    check_rsolve("a(n+1) = 2 a(n) + 2^n", {"a(0)=0"}, {0, 1, 4, 12, 32, 80});
    // Polynomial forcing: a(n+1) = a(n) + n, a(0)=0 -> n(n-1)/2.
    check_rsolve("a(n+1) = a(n) + n", {"a(0)=0"}, {0, 0, 1, 3, 6, 10});
}

TEST_CASE("rsolve: missing conditions default to zero with a warning") {
    const RsolveResult r = rsolve("a(n+2) = a(n+1) + a(n)", {"a(1)=1"});
    REQUIRE(r.warnings.size() >= 1);
    CHECK_THAT(r.warnings.front(), ContainsSubstring("a(0) = 0"));
    // Same as Fibonacci.
    CHECK(std::abs(evaluate(r.solution, Bindings{{"n", 10.0}}) - 55.0) < 1e-6);
}

// ---------------------------------------------------------------------------
// Regressions from the adversarial review (each was a confirmed defect)
// ---------------------------------------------------------------------------

TEST_CASE("sum review: constant irrational ratios are classified, not warned") {
    // 2^(k/2) has ratio sqrt(2) > 1: divergent, not "Exact with a warning".
    CHECK(sum_infinite(P("2^(k/2)"), "k", P("0")).status ==
          SumResult::Status::Diverges);
}

TEST_CASE("sum review: fractional numeric bounds are rejected") {
    const SumResult r = sum_finite(P("k"), "k", P("1"), P("-5/2"));
    CHECK(r.status == SumResult::Status::Unsolved);
    const SumResult r2 = sum_finite(P("k"), "k", P("1"), P("21/2"));
    CHECK(r2.status == SumResult::Status::Unsolved);
}

TEST_CASE("sum review: telescoping cancels across additive terms") {
    // 1/k - 1/(k+1) telescopes to exactly 1 even though each term alone is
    // harmonically divergent.
    const SumResult r = sum_infinite(P("1/k - 1/(k+1)"), "k", P("1"));
    REQUIRE(r.status == SumResult::Status::Exact);
    CHECK(evaluate(r.value, Bindings{}) == 1.0);
}

TEST_CASE("sum review: ratio comparison at 1 is exact, not rounded") {
    // (2^62-1)/2^62 < 1 exactly, though it rounds to 1.0 in double.
    const SumResult r = sum_infinite(
        P("(4611686018427387903/4611686018427387904)^k"), "k", P("0"));
    CHECK(r.status != SumResult::Status::Diverges);
}

TEST_CASE("sum review: large-denominator telescoping sum is now exact") {
    // Σ 1/(k(k+45)) = H(45)/45; H(45) does not fit a 64-bit rational but is now
    // computed exactly (arbitrary precision).
    const SumResult r = sum_infinite(P("1/(k*(k+45))"), "k", P("1"));
    CHECK(r.status == SumResult::Status::Exact);
}

TEST_CASE("rsolve review: fully cancelling coefficients error, not crash") {
    CHECK_THROWS_WITH(rsolve("a(n) = a(n)", {}),
                      ContainsSubstring("not a recurrence"));
    CHECK_THROWS_WITH(rsolve("a(n) + a(n) = 2*a(n) + n", {}),
                      ContainsSubstring("not a recurrence"));
}

TEST_CASE("rsolve: errors are specific") {
    CHECK_THROWS_WITH(rsolve("a(n+1) + a(n)", {}), ContainsSubstring("'='"));
    CHECK_THROWS_WITH(rsolve("n = 1", {}), ContainsSubstring("no a("));
    CHECK_THROWS_WITH(rsolve("a(n+1) = a(n)", {"a(0)=1", "a(0)=2"}),
                      ContainsSubstring("duplicate"));
    CHECK_THROWS_WITH(rsolve("a(n+2) = -a(n)", {"a(0)=0", "a(1)=1"}),
                      ContainsSubstring("complex"));
    // a(n+2) = a(n+1) normalizes by shifting to an order-1 recurrence, so
    // two conditions are one too many.
    CHECK_THROWS_WITH(rsolve("a(n+2) = a(n+1)", {"a(0)=1", "a(1)=1"}),
                      ContainsSubstring("order-1"));
}

TEST_CASE("integer functions: binomial and factorial fold through gamma") {
    const auto S = [](std::string_view s) {
        return to_string(simplify(parse_expression(s)), PrintStyle::Plain);
    };
    CHECK(S("binomial(5, 2)") == "10");
    CHECK(S("binomial(10, 5)") == "252");
    CHECK(S("binomial(7, 0)") == "1");
    CHECK(S("factorial(5)") == "120");
    CHECK(S("factorial(0)") == "1");
    CHECK(S("fib(10)") == "55");
    CHECK(S("fib(92)") == "7540113804746346429");
    CHECK(S("fib(-6)") == "-8");
    CHECK(S("fib(-7)") == "13");
    CHECK(S("harmonic(1)") == "1");
    CHECK(S("harmonic(4)") == "25/12");
    CHECK(S("harmonic(0)") == "0");
    // Now exact past the old 64-bit reach (arbitrary precision).
    CHECK(S("harmonic(100)") ==
          "14466636279520351160221518043104131447711/"
          "2788815009188499086581352357412492142272");
    CHECK(std::abs(evaluate(parse_expression("harmonic(100)"), Bindings{}) -
                   5.187377517639621) < 1e-9);
    CHECK(std::abs(evaluate(parse_expression("fib(30)"), Bindings{}) -
                   832040.0) < 1e-3);
}

TEST_CASE("sum of 1/k produces harmonic numbers") {
    const SumResult r = sum_finite(parse_expression("1/k"), "k",
                                   parse_expression("1"), parse_expression("n"));
    REQUIRE(r.status == SumResult::Status::Exact);
    CHECK(to_string(r.value, PrintStyle::Plain) == "harmonic(n)");
    CHECK(r.method == "harmonic numbers");

    const SumResult s = sum_finite(parse_expression("3/k"), "k",
                                   parse_expression("5"), parse_expression("n"));
    REQUIRE(s.status == SumResult::Status::Exact);
    CHECK(to_string(s.value, PrintStyle::Plain) == "3*(harmonic(n) - 25/12)");
}

TEST_CASE("seq: recognizes the classic sequence families") {
    const auto terms = [](std::initializer_list<long long> xs) {
        std::vector<Rational> v;
        for (const long long x : xs) v.push_back(Rational(x));
        return v;
    };

    const SeqResult fib = recognize_sequence(terms({0, 1, 1, 2, 3, 5, 8}));
    CHECK(fib.kind == SeqResult::Kind::Recurrence);
    CHECK(fib.recurrence == "a(n+2) = a(n+1) + a(n)");
    CHECK_THAT(fib.description, Catch::Matchers::ContainsSubstring("Fibonacci"));
    REQUIRE(fib.next.size() == 3);
    CHECK(fib.next[0] == Rational(13));
    CHECK(fib.next[2] == Rational(34));
    REQUIRE(fib.formula);
    CHECK_THAT(to_string(fib.formula, PrintStyle::Plain),
               Catch::Matchers::ContainsSubstring("sqrt(5)"));

    const SeqResult sq = recognize_sequence(terms({1, 4, 9, 16, 25}));
    CHECK(sq.kind == SeqResult::Kind::Polynomial);
    CHECK(to_string(sq.formula, PrintStyle::Plain) == "n^2 + 2*n + 1");
    CHECK(sq.next[0] == Rational(36));

    const SeqResult ar = recognize_sequence(terms({2, 5, 8, 11}));
    CHECK(ar.kind == SeqResult::Kind::Arithmetic);
    CHECK(to_string(ar.formula, PrintStyle::Plain) == "3*n + 2");

    const SeqResult ge = recognize_sequence(terms({3, 6, 12, 24, 48}));
    CHECK(ge.kind == SeqResult::Kind::Geometric);
    CHECK(to_string(ge.formula, PrintStyle::Plain) == "3*2^n");
    CHECK(ge.next[0] == Rational(96));

    // Pell numbers: a(n+2) = 2 a(n+1) + a(n).
    const SeqResult pell = recognize_sequence(terms({0, 1, 2, 5, 12, 29}));
    CHECK(pell.kind == SeqResult::Kind::Recurrence);
    CHECK(pell.recurrence == "a(n+2) = 2*a(n+1) + a(n)");
    CHECK(pell.next[0] == Rational(70));

    const SeqResult unknown =
        recognize_sequence(terms({1, 1, 2, 5, 29, 866}));
    CHECK(unknown.kind == SeqResult::Kind::Unknown);

    CHECK_THROWS_AS(recognize_sequence(terms({1, 2, 3})), Error);
}
