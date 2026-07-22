// Rational-expression cancellation tests (docs/proposals/cancel-poly-gcd.md).
//
// Three layers, per the proposal §11:
//   1. Structural unit rows — every §6 example, cancelling and guard, plus the
//      Equation overload, the degree-cap refusal, and an overflow bail.
//   2. Differential value-preservation — cancel(e) equals e at sample points
//      where the *original* denominator is nonzero (I1; formal cancellation).
//   3. Idempotence (I3) and a constructive fuzz: e = (A*G)/(B*G) must cancel
//      back to a value-equal A/B and be idempotent.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/expr.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/printer.hpp"
#include "mathsolver/simplify.hpp"

using namespace mathsolver;

namespace {

Expr P(const std::string& s) { return parse_expression(s); }

/// cancel(input) is structurally the simplified `expected`.
void check_cancel(const std::string& input, const std::string& expected) {
    const Expr got = cancel(P(input));
    const Expr want = simplify(P(expected));
    INFO("cancel(" << input << ") = " << to_string(got, PrintStyle::Plain)
                   << ", want " << to_string(want, PrintStyle::Plain));
    CHECK(structurally_equal(got, want));
}

/// cancel(input) returns the simplified input unchanged (a no-op / refusal).
void check_noop(const std::string& input) {
    const Expr got = cancel(P(input));
    const Expr want = simplify(P(input));
    INFO("cancel(" << input << ") = " << to_string(got, PrintStyle::Plain)
                   << " (expected unchanged)");
    CHECK(structurally_equal(got, want));
}

/// I1: cancel(e) equals e wherever the original denominator is nonzero.
/// evaluate throws at singular points, so a throw on `e` marks a skip.
void check_value_preserving(const std::string& input) {
    const Expr in = P(input);
    const Expr out = cancel(in);
    const std::vector<double> xs = {-2.7, -1.3, -0.51, 0.37, 1.9, 3.1, 4.25};
    int checked = 0;
    for (const double x : xs) {
        double b = 0.0;
        try {
            b = evaluate(in, Bindings{{"x", x}});
        } catch (const Error&) {
            continue;  // original undefined here — I1 promises nothing
        }
        if (!std::isfinite(b)) continue;
        const double a = evaluate(out, Bindings{{"x", x}});
        INFO("value at x=" << x << " of " << input << ": cancel=" << a
                           << " orig=" << b);
        CHECK(std::abs(a - b) < 1e-9 * (1.0 + std::abs(b)));
        ++checked;
    }
    CHECK(checked > 0);
}

}  // namespace

TEST_CASE("cancel: §6 cancelling rows", "[cancel]") {
    check_cancel("(x^2 - 1)/(x - 1)", "x + 1");
    check_cancel("(x^3 - 1)/(x - 1)", "x^2 + x + 1");
    check_cancel("(x^2 + 2x + 1)/(x + 1)", "x + 1");
    check_cancel("(x^2 - 1)/(x^2 - 3x + 2)", "(x + 1)/(x - 2)");
    check_cancel("(x^2 - 1)/(x - 1)^2", "(x + 1)/(x - 1)");
    check_cancel("2/(2x - 2)", "1/(x - 1)");
    check_cancel("(2x + 2)/(4x - 2)", "(x + 1)/(2*x - 1)");
    check_cancel("(6x^2 - 6)/(4x - 4)", "(3*x + 3)/2");
}

TEST_CASE("cancel: guard rows return the input unchanged", "[cancel]") {
    // Non-cancelling (gcd = 1).
    check_noop("(x^2 - 1)/(x - 2)");
    check_noop("(x^2 + 1)/(x + 1)");
    // Non-polynomial numerator / no denominator / fractional power.
    check_noop("sin(x)/x");
    check_noop("x^2 + 3x");
    check_noop("1/(x - 1)^(1/2)");
    // v1 refusals: symbolic coefficient, multivariate.
    check_noop("(a*x^2 - a)/(x - 1)");
    check_noop("(x*y + x*z)/x");
    check_noop("(x^2 - y^2)/(x - y)");
}

TEST_CASE("cancel: value-preserving on the reals (I1)", "[cancel]") {
    check_value_preserving("(x^2 - 1)/(x - 1)");
    check_value_preserving("(x^3 - 1)/(x - 1)");
    check_value_preserving("(x^2 - 1)/(x^2 - 3x + 2)");
    check_value_preserving("(6x^2 - 6)/(4x - 4)");
    check_value_preserving("(2x + 2)/(4x - 2)");
}

TEST_CASE("cancel: idempotence (I3)", "[cancel]") {
    const std::vector<std::string> corpus = {
        "(x^2 - 1)/(x - 1)",        "(x^2 - 1)/(x^2 - 3x + 2)",
        "(x^2 - 1)/(x - 1)^2",      "(6x^2 - 6)/(4x - 4)",
        "(x^2 + 1)/(x + 1)",        "sin(x)/x",
    };
    for (const std::string& s : corpus) {
        const Expr once = cancel(P(s));
        const Expr twice = cancel(once);
        INFO("idempotence for " << s);
        CHECK(structurally_equal(once, twice));
    }
}

TEST_CASE("cancel: Equation overload cancels each side", "[cancel]") {
    const Equation eq{P("(x^2 - 1)/(x - 1)"), P("(x^3 - 1)/(x - 1)")};
    const Equation got = cancel(eq);
    CHECK(structurally_equal(got.lhs, simplify(P("x + 1"))));
    CHECK(structurally_equal(got.rhs, simplify(P("x^2 + x + 1"))));
}

TEST_CASE("cancel: degree cap refuses very high degrees", "[cancel]") {
    // deg N = 40 > 32: the whole attempt is refused (returns simplified input).
    const std::string big = "(x^40 - 1)/(x - 1)";
    check_noop(big);
}

TEST_CASE("cancel: overflow bails to the unchanged input", "[cancel]") {
    // Extracting the numerator's coefficients expands (1e10·x+1)(1e10·x+3),
    // whose x^2 coefficient 1e20 overflows a 64-bit rational. The pipeline
    // must catch it and bail to simplify(input), never throw (§4.7).
    const std::string huge =
        "((10000000000*x + 1)*(10000000000*x + 3))/(x - 1)";
    Expr got;
    CHECK_NOTHROW(got = cancel(P(huge)));
    CHECK(structurally_equal(got, simplify(P(huge))));
}

namespace {

/// Build a polynomial Expr in x from ascending integer coefficients.
Expr poly(const std::vector<long long>& c) {
    std::vector<Expr> terms;
    for (std::size_t k = 0; k < c.size(); ++k) {
        if (c[k] == 0) continue;
        terms.push_back(make_mul({make_num(c[k]),
                                  make_pow(make_sym("x"), make_num(static_cast<long long>(k)))}));
    }
    return make_add(std::move(terms));
}

// Deterministic LCG — no wall-clock/random seeding (reproducible fuzz).
struct Lcg {
    unsigned long long s;
    long long next(long long lo, long long hi) {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        const unsigned long long span = static_cast<unsigned long long>(hi - lo + 1);
        return lo + static_cast<long long>((s >> 33) % span);
    }
};

std::vector<long long> rand_poly(Lcg& r, int deg) {
    std::vector<long long> c(static_cast<std::size_t>(deg) + 1);
    for (long long& v : c) v = r.next(-9, 9);
    if (c.back() == 0) c.back() = r.next(1, 9);  // keep the declared degree
    return c;
}

}  // namespace

TEST_CASE("cancel: constructive fuzz — (A*G)/(B*G) cancels to A/B", "[cancel]") {
    Lcg r{0x1234567890abcdefULL};
    const std::vector<double> xs = {-2.6, -1.1, 0.37, 1.9, 3.3};
    int trials = 0;
    for (int i = 0; i < 4000; ++i) {
        const std::vector<long long> A = rand_poly(r, static_cast<int>(r.next(0, 3)));
        const std::vector<long long> B = rand_poly(r, static_cast<int>(r.next(1, 3)));
        const std::vector<long long> G = rand_poly(r, static_cast<int>(r.next(1, 3)));
        Expr e, ref;
        try {
            const Expr num = expand(make_mul({poly(A), poly(G)}));
            const Expr den = expand(make_mul({poly(B), poly(G)}));
            e = make_div(num, den);
            ref = make_div(poly(A), poly(B));
        } catch (const Error&) {
            continue;  // overflow during construction — skip
        }
        Expr got;
        try {
            got = cancel(e);
        } catch (...) {
            FAIL("cancel threw on " << to_string(e, PrintStyle::Plain));
        }
        // Value-equality to A/B where both sides are defined (B*G != 0).
        for (const double x : xs) {
            double want = 0.0, orig = 0.0;
            try {
                want = evaluate(ref, Bindings{{"x", x}});
                orig = evaluate(e, Bindings{{"x", x}});
            } catch (const Error&) {
                continue;
            }
            if (!std::isfinite(want) || !std::isfinite(orig)) continue;
            const double a = evaluate(got, Bindings{{"x", x}});
            INFO("fuzz value at x=" << x << " of " << to_string(e, PrintStyle::Plain));
            CHECK(std::abs(a - want) < 1e-7 * (1.0 + std::abs(want)));
        }
        // Idempotence.
        CHECK(structurally_equal(cancel(got), got));
        ++trials;
    }
    CHECK(trials > 1000);
}
