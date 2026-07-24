#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

#include "mathsolver/derivative.hpp"
#include "mathsolver/errors.hpp"
#include "mathsolver/integrate.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/printer.hpp"
#include "mathsolver/steps.hpp"

using namespace mathsolver;

namespace {

Explanation steps_of(const std::string& src, const std::string& var = "x") {
    return explain_derivative(parse_expression(src), var);
}

Explanation isteps_of(const std::string& src, const std::string& var = "x") {
    return explain_integral(parse_expression(src), var);
}

bool has_rule(const Explanation& ex, const std::string& rule) {
    return std::any_of(ex.steps.begin(), ex.steps.end(),
                       [&](const ExplainStep& s) { return s.rule == rule; });
}

// The integral recorder must never change the answer: its result equals the
// plain `integrate` antiderivative.
void check_matches_integrate(const std::string& src, const std::string& var = "x") {
    const Explanation ex = isteps_of(src, var);
    const IntegrateResult r = integrate(parse_expression(src), var);
    REQUIRE(r.status == IntegrateResult::Status::Integrated);
    const std::string plain = to_string(r.antiderivative, PrintStyle::Plain);
    INFO(src << " => steps result '" << ex.result_plain << "' vs integrate '" << plain << "'");
    REQUIRE(ex.result_plain == plain);
}

// The recorder must never change the answer: its result equals plain `diff`.
void check_matches_diff(const std::string& src, const std::string& var = "x") {
    const Explanation ex = steps_of(src, var);
    const std::string plain = to_string(differentiate(parse_expression(src), var), PrintStyle::Plain);
    INFO(src << " => steps result '" << ex.result_plain << "' vs diff '" << plain << "'");
    REQUIRE(ex.result_plain == plain);
}

} // namespace

TEST_CASE("steps: result always equals the plain derivative", "[steps]") {
    for (const char* s : {"x^3", "sin(x^2)", "x*e^x", "x^2 + 3*x + 1", "ln(x)/x",
                          "tan(x)", "(x^2 + 1)^5", "x^x", "e^(2*x)", "5", "x"}) {
        check_matches_diff(s);
    }
}

TEST_CASE("steps: power rule", "[steps]") {
    const Explanation ex = steps_of("x^3");
    REQUIRE(has_rule(ex, "power rule"));
    REQUIRE(ex.result_plain == "3*x^2");
    // The recorded step shows the concrete transformation.
    REQUIRE(ex.steps.back().plain == "d/dx(x^3) = 3*x^2");
}

TEST_CASE("steps: chain rule wraps an inner power step", "[steps]") {
    const Explanation ex = steps_of("sin(x^2)");
    REQUIRE(has_rule(ex, "power rule"));  // the inner d/dx(x^2)
    REQUIRE(has_rule(ex, "chain rule"));  // the outer sin
    // Innermost first: the power step precedes the chain step.
    const auto power = std::find_if(ex.steps.begin(), ex.steps.end(),
                                    [](const ExplainStep& s) { return s.rule == "power rule"; });
    const auto chain = std::find_if(ex.steps.begin(), ex.steps.end(),
                                    [](const ExplainStep& s) { return s.rule == "chain rule"; });
    REQUIRE(power < chain);
    REQUIRE(ex.result_plain == "2*x*cos(x^2)");
}

TEST_CASE("steps: product rule", "[steps]") {
    const Explanation ex = steps_of("x*e^x");
    REQUIRE(has_rule(ex, "product rule"));
    REQUIRE(ex.result_plain == to_string(differentiate(parse_expression("x*e^x"), "x"),
                                         PrintStyle::Plain));
}

TEST_CASE("steps: constant multiple vs product", "[steps]") {
    // 3*x^2: only one factor carries x, so it is a constant multiple, not a product.
    const Explanation cm = steps_of("3*x^2");
    REQUIRE(has_rule(cm, "constant multiple rule"));
    REQUIRE_FALSE(has_rule(cm, "product rule"));
}

TEST_CASE("steps: sum rule and exponential rule", "[steps]") {
    const Explanation sum = steps_of("x^2 + 3*x");
    REQUIRE(has_rule(sum, "sum rule"));
    const Explanation ex = steps_of("e^(2*x)");
    REQUIRE(has_rule(ex, "exponential rule"));
    REQUIRE(ex.result_plain == "2*e^(2*x)");
}

TEST_CASE("steps: general power rule for x^x", "[steps]") {
    const Explanation ex = steps_of("x^x");
    REQUIRE(has_rule(ex, "general power rule"));
}

TEST_CASE("steps: latex is emitted for every step", "[steps]") {
    const Explanation ex = steps_of("sin(x^2)");
    for (const ExplainStep& s : ex.steps) {
        REQUIRE_FALSE(s.plain.empty());
        REQUIRE_FALSE(s.latex.empty());
        REQUIRE(s.latex.find("\\frac{d}{dx}") != std::string::npos);
    }
    REQUIRE_FALSE(ex.result_latex.empty());
}

TEST_CASE("steps: constant and bare variable get a trivial step", "[steps]") {
    const Explanation c = steps_of("5");
    REQUIRE(c.steps.size() == 1);
    REQUIRE(c.steps.front().rule == "constant rule");
    REQUIRE(c.result_plain == "0");
    const Explanation v = steps_of("x");
    REQUIRE(v.steps.size() == 1);
    REQUIRE(v.steps.front().rule == "identity rule");
    REQUIRE(v.result_plain == "1");
}

// --- integral steps --------------------------------------------------------

TEST_CASE("isteps: result always equals the plain integral", "[steps]") {
    for (const char* s : {"x^2", "x^2 + sin(x)", "3*x^2", "2*x*cos(x^2)", "x*sin(x)",
                          "1/(x^2 - 1)", "e^(2*x)", "cos(x)^2", "5", "x"}) {
        check_matches_integrate(s);
    }
}

TEST_CASE("isteps: power rule leaf", "[steps]") {
    const Explanation ex = isteps_of("x^2");
    REQUIRE(has_rule(ex, "power rule"));
    REQUIRE(ex.result_plain == "x^3/3");
    REQUIRE(ex.steps.back().plain == "∫ x^2 dx = x^3/3");
}

TEST_CASE("isteps: linearity splits a sum, innermost-first", "[steps]") {
    const Explanation ex = isteps_of("x^2 + sin(x)");
    REQUIRE(has_rule(ex, "linearity"));
    // Each term is worked before the combining linearity step (recorded last).
    REQUIRE(ex.steps.back().rule == "linearity");
    REQUIRE(ex.steps.size() >= 3);  // ∫x^2, ∫sin(x), and the linearity line
}

TEST_CASE("isteps: constant multiple peels a numeric factor", "[steps]") {
    const Explanation ex = isteps_of("3*x^2");
    REQUIRE(has_rule(ex, "constant multiple"));
    REQUIRE(ex.result_plain == "x^3");
}

TEST_CASE("isteps: u-substitution is reported by the engine", "[steps]") {
    const Explanation ex = isteps_of("2*x*cos(x^2)");
    const bool usub = std::any_of(ex.steps.begin(), ex.steps.end(), [](const ExplainStep& s) {
        return s.rule.find("u-substitution") != std::string::npos;
    });
    REQUIRE(usub);
    REQUIRE(ex.result_plain == to_string(integrate(parse_expression("2*x*cos(x^2)"), "x").antiderivative,
                                         PrintStyle::Plain));
}

TEST_CASE("isteps: integration by parts is reported by the engine", "[steps]") {
    const Explanation ex = isteps_of("x*sin(x)");
    const bool parts = std::any_of(ex.steps.begin(), ex.steps.end(), [](const ExplainStep& s) {
        return s.rule.find("integration by parts") != std::string::npos;
    });
    REQUIRE(parts);
}

TEST_CASE("isteps: latex is emitted for every integral step", "[steps]") {
    const Explanation ex = isteps_of("x^2 + sin(x)");
    for (const ExplainStep& s : ex.steps) {
        REQUIRE_FALSE(s.plain.empty());
        REQUIRE_FALSE(s.latex.empty());
        REQUIRE(s.latex.find("\\int") != std::string::npos);
    }
    REQUIRE_FALSE(ex.result_latex.empty());
}

TEST_CASE("isteps: a non-elementary integral throws, like the plain verb", "[steps]") {
    // e^(x^2) (positive quadratic exponent) has no elementary antiderivative.
    REQUIRE_THROWS_AS(isteps_of("e^(x^2)"), Error);
}
