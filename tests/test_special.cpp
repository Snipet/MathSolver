// Special functions (gamma, digamma, erf, erfc): exact values, numerics
// against known constants, derivative rules, the Gaussian erf integral, and
// the gamma-vs-greek-letter parse disambiguation.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <string>
#include <string_view>

#include "mathsolver/derivative.hpp"
#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/integrate.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/printer.hpp"
#include "mathsolver/simplify.hpp"

using namespace mathsolver;
using Catch::Matchers::ContainsSubstring;

namespace {

std::string S(std::string_view s) {
    return to_string(simplify(parse_expression(s)), PrintStyle::Plain);
}

double E(std::string_view s) {
    return evaluate(parse_expression(s), Bindings{});
}

constexpr double kEulerGamma = 0.5772156649015329;

} // namespace

TEST_CASE("gamma: exact integer and half-integer values") {
    CHECK(S("gamma(1)") == "1");
    CHECK(S("gamma(5)") == "24");
    CHECK(S("gamma(21)") == "2432902008176640000"); // 20!
    CHECK(S("gamma(1/2)") == "sqrt(pi)");
    CHECK(S("gamma(3/2)") == "sqrt(pi)/2");
    CHECK(S("gamma(7/2)") == "15*sqrt(pi)/8");
    CHECK(S("gamma(-1/2)") == "-2*sqrt(pi)");
    CHECK(S("gamma(-3/2)") == "4*sqrt(pi)/3");
    // Symbolic and non-special arguments stay put.
    CHECK(S("gamma(x)") == "gamma(x)");
    CHECK(S("gamma(1/3)") == "gamma(1/3)");
}

TEST_CASE("gamma and digamma: numerics and poles") {
    CHECK(std::abs(E("gamma(4.5)") - std::tgamma(4.5)) < 1e-12);
    CHECK_THROWS_AS(E("gamma(0)"), Error);
    CHECK_THROWS_AS(E("gamma(-3)"), Error);
    // psi(1) = -euler_gamma; psi(1/2) = -euler_gamma - 2 ln 2;
    // psi(5) = H_4 - euler_gamma = 25/12 - euler_gamma.
    CHECK(std::abs(E("digamma(1)") + kEulerGamma) < 1e-9);
    CHECK(std::abs(E("digamma(1/2)") + kEulerGamma + 2 * std::log(2.0)) < 1e-9);
    CHECK(std::abs(E("digamma(5)") - (25.0 / 12.0 - kEulerGamma)) < 1e-9);
    // Reflection: psi(-1/2) = psi(3/2) - pi cot(-pi/2) = 2 - g - 2ln2.
    CHECK(std::abs(E("digamma(-1/2)") - (2.0 - kEulerGamma - 2 * std::log(2.0))) <
          1e-9);
    CHECK_THROWS_AS(E("digamma(-2)"), Error);
}

TEST_CASE("erf and erfc: anchors, symmetry, numerics") {
    CHECK(S("erf(0)") == "0");
    CHECK(S("erfc(0)") == "1");
    CHECK(S("erf(-x)") == "-erf(x)");
    CHECK(std::abs(E("erf(1)") - std::erf(1.0)) < 1e-15);
    CHECK(std::abs(E("erfc(2)") - std::erfc(2.0)) < 1e-15);
}

TEST_CASE("special function derivatives") {
    const auto D = [](std::string_view s) {
        return to_string(simplify(differentiate(parse_expression(s), "x")),
                         PrintStyle::Plain);
    };
    CHECK(D("erf(x)") == "2*e^(-x^2)/sqrt(pi)");
    CHECK(D("gamma(x)") == "gamma(x)*digamma(x)");
    CHECK_THROWS_WITH(differentiate(parse_expression("digamma(x)"), "x"),
                      ContainsSubstring("polygamma"));
    // The erf Taylor series follows from the derivative machinery.
    // (series erf about 0, order 5 = 2/sqrt(pi) (x - x^3/3 + x^5/10).)
}

TEST_CASE("gaussian integrals produce erf forms") {
    const IntegrateResult r = integrate(parse_expression("e^(-x^2)"), "x");
    REQUIRE(r.status == IntegrateResult::Status::Integrated);
    CHECK(to_string(simplify(r.antiderivative), PrintStyle::Plain) ==
          "sqrt(pi)*erf(x)/2");
    // Completed square with a shift: e^(-x^2 + 2x) = e * e^(-(x-1)^2).
    const IntegrateResult s =
        integrate(parse_expression("e^(-x^2 + 2x)"), "x");
    REQUIRE(s.status == IntegrateResult::Status::Integrated);
    const double at2 = evaluate(s.antiderivative, Bindings{{"x", 2.0}});
    const double at0 = evaluate(s.antiderivative, Bindings{{"x", 0.0}});
    // Numeric cross-check by midpoint quadrature.
    double acc = 0.0;
    const int n = 20000;
    for (int i = 0; i < n; ++i) {
        const double x = 2.0 * (i + 0.5) / n;
        acc += std::exp(-x * x + 2 * x) * 2.0 / n;
    }
    CHECK(std::abs((at2 - at0) - acc) < 1e-6);
}

TEST_CASE("gamma parses as a function only when applied") {
    // gamma(x) is the function; bare gamma stays the greek symbol.
    CHECK(S("gamma * 2") == "2*gamma");
    CHECK(S("gamma + gamma(3)") == "gamma + 2");
    CHECK(S("psi(1/2) + psi") == "psi + digamma(1/2)");
    CHECK(to_string(parse_expression("gamma(x)"), PrintStyle::LaTeX) ==
          "\\Gamma\\left(x\\right)");
    CHECK(to_string(parse_expression("digamma(x)"), PrintStyle::LaTeX) ==
          "\\psi\\left(x\\right)");
    CHECK_THAT(to_string(parse_expression("erf(x)"), PrintStyle::LaTeX),
               ContainsSubstring("\\operatorname{erf}"));
}
