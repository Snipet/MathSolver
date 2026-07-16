#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "mathsolver/errors.hpp"
#include "mathsolver/expr.hpp"
#include "mathsolver/parser.hpp"

using namespace mathsolver;

namespace {

Expr x() { return make_sym("x"); }
Expr y() { return make_sym("y"); }
Expr z() { return make_sym("z"); }
Expr num(long long n) { return make_num(n); }
Expr num(long long n, long long d) { return make_num(Rational(n, d)); }

void check_parse(std::string_view src, const Expr& expected) {
    Expr got = parse_expression(src);
    INFO("input:    " << src);
    INFO("got:      " << debug_string(got));
    INFO("expected: " << debug_string(expected));
    REQUIRE(structurally_equal(got, expected));
}

template <typename F>
ParseError capture_error(F&& f) {
    try {
        std::forward<F>(f)();
    } catch (const ParseError& e) {
        return e;
    }
    FAIL("expected ParseError, but nothing was thrown");
    throw std::logic_error("unreachable");
}

bool message_contains(const ParseError& e, std::string_view needle) {
    return std::string_view(e.what()).find(needle) != std::string_view::npos;
}

} // namespace

// ---------------------------------------------------------------------------
// Numbers, symbols, constants
// ---------------------------------------------------------------------------

TEST_CASE("parser: integer and decimal literals", "[parser]") {
    check_parse("42", num(42));
    check_parse("0", num(0));
    check_parse("3.14", num(157, 50)); // exact decimal conversion
    check_parse("0.5", num(1, 2));
}

TEST_CASE("parser: symbols and constants", "[parser]") {
    check_parse("x", x());
    check_parse("pi", make_const(ConstantId::Pi));
    check_parse("\\pi", make_const(ConstantId::Pi));
    check_parse("e", make_const(ConstantId::E)); // e is always Euler's number
}

TEST_CASE("parser: greek letters, backslash and bare", "[parser]") {
    check_parse("\\alpha", make_sym("alpha"));
    check_parse("alpha", make_sym("alpha"));
    check_parse("\\theta", make_sym("theta"));
    check_parse("mu epsilon", make_mul({make_sym("mu"), make_sym("epsilon")}));
}

TEST_CASE("parser: identifier run segmentation", "[parser]") {
    check_parse("xy", make_mul({x(), y()}));
    check_parse("sinx", make_fn(FunctionId::Sin, x()));
    check_parse("foo", make_mul({make_sym("f"), make_sym("o"), make_sym("o")}));
    check_parse("2e3", make_mul({num(2), make_const(ConstantId::E), num(3)})); // no sci notation
    check_parse("pie", make_mul({make_const(ConstantId::Pi), make_const(ConstantId::E)}));
    check_parse("exy", make_mul({make_const(ConstantId::E), x(), y()}));
}

TEST_CASE("parser: subscripts fold into the symbol name", "[parser]") {
    check_parse("x_1", make_sym("x_1"));
    check_parse("x_{12}", make_sym("x_12"));
    check_parse("x_a", make_sym("x_a"));
    check_parse("\\alpha_2", make_sym("alpha_2"));
    check_parse("y_{ab} + 1", make_add({make_sym("y_ab"), num(1)}));
}

TEST_CASE("parser: unbraced subscript takes one letter or one maximal digit run",
          "[parser][subscript]") {
    // x_12 == x_{12}: the digit run is taken whole.
    check_parse("x_12", make_sym("x_12"));
    REQUIRE(structurally_equal(parse_expression("x_12"), parse_expression("x_{12}")));
    check_parse("x_123", make_sym("x_123"));
    // A letter subscript is exactly one letter: x_ab == x_a * b.
    check_parse("x_ab", make_mul({make_sym("x_a"), make_sym("b")}));
    // A digit run stops at the first letter: x_1a == x_1 * a.
    check_parse("x_1a", make_mul({make_sym("x_1"), make_sym("a")}));
    check_parse("x_12y", make_mul({make_sym("x_12"), y()}));
    // Braced subscripts take the whole letter/digit run.
    check_parse("x_{max}", make_sym("x_max"));
    check_parse("x_{a1}", make_sym("x_a1"));
    // The Plain-printer unbraced form for a multi-digit subscript re-lexes
    // to the same symbol (round-trip requirement).
    REQUIRE(structurally_equal(parse_expression("x_12 + x_12"),
                               make_add({make_sym("x_12"), make_sym("x_12")})));
}

// ---------------------------------------------------------------------------
// Operators and precedence
// ---------------------------------------------------------------------------

TEST_CASE("parser: add/sub/mul/div rewrites", "[parser]") {
    check_parse("x + y", make_add({x(), y()}));
    check_parse("x - y", make_add({x(), make_mul({num(-1), y()})}));
    check_parse("x * y", make_mul({x(), y()}));
    check_parse("x / y", make_mul({x(), make_pow(y(), num(-1))}));
    check_parse("x \\cdot y", make_mul({x(), y()}));
    check_parse("x \\times y", make_mul({x(), y()}));
    check_parse("x \\div y", make_div(x(), y()));
}

TEST_CASE("parser: unary minus and plus", "[parser]") {
    check_parse("-x", make_mul({num(-1), x()}));
    check_parse("+x", x());
    check_parse("-2", num(-2));
    check_parse("--x", x());
    check_parse("2 - -3", num(5));
}

TEST_CASE("parser: power is right-associative and above unary minus", "[parser]") {
    check_parse("2^3^2", num(512)); // 2^(3^2)
    check_parse("-x^2", make_mul({num(-1), make_pow(x(), num(2))}));
    check_parse("(-x)^2", make_pow(make_mul({num(-1), x()}), num(2)));
    check_parse("2^-3", num(1, 8)); // signed exponent
    check_parse("x^-1", make_pow(x(), num(-1)));
    check_parse("2**3", num(8)); // ** synonym
    check_parse("x^2y", make_mul({make_pow(x(), num(2)), y()}));
}

TEST_CASE("parser: implicit multiplication binds like *", "[parser]") {
    check_parse("2x", make_mul({num(2), x()}));
    check_parse("x y", make_mul({x(), y()}));
    check_parse("2(x+1)", make_mul({num(2), make_add({x(), num(1)})}));
    check_parse("(x+1)(x-2)",
                make_mul({make_add({x(), num(1)}), make_add({x(), num(-2)})}));
    check_parse("2\\pi r", make_mul({num(2), make_const(ConstantId::Pi), make_sym("r")}));
    check_parse("1/2x", make_mul({num(1, 2), x()})); // (1/2)*x, documented
    check_parse("x2y", make_mul({num(2), x(), y()}));
}

TEST_CASE("parser: implicit multiplication never triggers at + or -", "[parser][implicit]") {
    // "2 - 3" is always the subtraction Add(2, -3), never Mul(2, -3).
    check_parse("2 - 3", num(-1));
    check_parse("2 -3", num(-1));
    check_parse("2- 3", num(-1));
    check_parse("2-3", num(-1));
    check_parse("x - y", make_add({x(), make_mul({num(-1), y()})}));
    check_parse("x -y", make_add({x(), make_mul({num(-1), y()})}));
    check_parse("2 + 3", num(5));
    check_parse("2 +3", num(5));
    check_parse("(x+1) - 2", make_add({x(), num(-1)}));
}

TEST_CASE("parser: division shapes land on the canonical folds", "[parser][pow][fold]") {
    // These exact shapes make the printer round-trip invariant satisfiable
    // (DESIGN.md sections 2 and 5).
    SECTION("3/x^2 -> Mul(3, Pow(x, -2))") {
        Expr e = parse_expression("3/x^2");
        INFO(debug_string(e));
        REQUIRE(debug_string(e) == "(mul 3 (pow x -2))");
        REQUIRE(structurally_equal(e, make_mul({num(3), make_pow(x(), num(-2))})));
    }
    SECTION("1/(2*x) -> Mul(1/2, Pow(x, -1))") {
        Expr e = parse_expression("1/(2*x)");
        INFO(debug_string(e));
        REQUIRE(debug_string(e) == "(mul 1/2 (pow x -1))");
        REQUIRE(structurally_equal(e, make_mul({num(1, 2), make_pow(x(), num(-1))})));
    }
    SECTION("1/(2x) matches 1/(2*x)") {
        REQUIRE(structurally_equal(parse_expression("1/(2x)"), parse_expression("1/(2*x)")));
    }
    SECTION("\\frac{3}{x^2} matches 3/x^2") {
        REQUIRE(structurally_equal(parse_expression("\\frac{3}{x^2}"),
                                   parse_expression("3/x^2")));
    }
    SECTION("sec^2 normalizes through the pow-of-pow fold") {
        // \sec^2 x parses through Pow(Pow(cos x, -1), 2) and lands on
        // Pow(cos x, -2).
        Expr e = parse_expression("\\sec^2 x");
        INFO(debug_string(e));
        REQUIRE(structurally_equal(e, make_pow(make_fn(FunctionId::Cos, x()), num(-2))));
    }
}

TEST_CASE("parser: braces and brackets as grouping", "[parser]") {
    check_parse("{x+1}", make_add({x(), num(1)}));
    check_parse("[x+1]", make_add({x(), num(1)}));
    check_parse("{x+1}^2", make_pow(make_add({x(), num(1)}), num(2)));
    check_parse("((x))", x());
}

TEST_CASE("parser: spacing commands are ignored", "[parser]") {
    check_parse("x \\, y", make_mul({x(), y()}));
    check_parse("x\\;y", make_mul({x(), y()}));
    check_parse("x\\!y", make_mul({x(), y()}));
    check_parse("x\\:y", make_mul({x(), y()}));
    check_parse("x \\quad + \\qquad y", make_add({x(), y()}));
}

// ---------------------------------------------------------------------------
// LaTeX structures
// ---------------------------------------------------------------------------

TEST_CASE("parser: \\frac", "[parser]") {
    check_parse("\\frac{x}{y}", make_div(x(), y()));
    check_parse("\\frac{1}{2}", num(1, 2));
    check_parse("\\frac{\\frac{1}{2}}{3}", num(1, 6)); // nested
    check_parse("\\frac{x+1}{2}", make_div(make_add({x(), num(1)}), num(2)));
    check_parse("\\frac{1}{2}x", make_mul({num(1, 2), x()}));
}

TEST_CASE("parser: sqrt forms", "[parser]") {
    check_parse("sqrt(4)", num(2)); // exact fold in make_pow
    check_parse("\\sqrt{x}", make_pow(x(), num(1, 2)));
    check_parse("sqrt x", make_pow(x(), num(1, 2)));
    check_parse("\\sqrt[3]{8}", num(2));
    check_parse("\\sqrt[3]{x}", make_pow(x(), num(1, 3)));
    check_parse("\\sqrt[n]{x}", make_pow(x(), make_div(num(1), make_sym("n"))));
}

TEST_CASE("parser: \\left ... \\right pairs", "[parser]") {
    check_parse("\\left( x+1 \\right)", make_add({x(), num(1)}));
    check_parse("\\left[ x \\right]", x());
    check_parse("\\left( \\left[ x \\right] \\right)", x());
    check_parse("\\left( x+1 \\right)^2", make_pow(make_add({x(), num(1)}), num(2)));
}

// ---------------------------------------------------------------------------
// Functions and their rewrites
// ---------------------------------------------------------------------------

TEST_CASE("parser: function call syntax variants", "[parser]") {
    Expr sinx = make_fn(FunctionId::Sin, x());
    check_parse("sin(x)", sinx);
    check_parse("\\sin{x}", sinx);
    check_parse("\\sin x", sinx);
    check_parse("abs(x)", make_fn(FunctionId::Abs, x()));
    check_parse("ln(x)", make_fn(FunctionId::Ln, x()));
    check_parse("arcsin(x)", make_fn(FunctionId::Asin, x()));
    check_parse("\\arctan{x}", make_fn(FunctionId::Atan, x()));
    check_parse("atan(x)", make_fn(FunctionId::Atan, x()));
    check_parse("tanh(x)", make_fn(FunctionId::Tanh, x()));
    check_parse("\\cosh x", make_fn(FunctionId::Cosh, x()));
}

TEST_CASE("parser: exp/sec/csc/cot/log rewrites", "[parser]") {
    check_parse("exp(x)", make_pow(make_const(ConstantId::E), x()));
    check_parse("\\exp x", make_pow(make_const(ConstantId::E), x()));
    check_parse("sec(x)", make_pow(make_fn(FunctionId::Cos, x()), num(-1)));
    check_parse("csc(x)", make_pow(make_fn(FunctionId::Sin, x()), num(-1)));
    check_parse("cot(x)", make_pow(make_fn(FunctionId::Tan, x()), num(-1)));
    // plain log is base 10: ln(x) / ln(10)
    Expr log10x = make_div(make_fn(FunctionId::Ln, x()), make_fn(FunctionId::Ln, num(10)));
    check_parse("log(x)", log10x);
    check_parse("\\log x", log10x);
}

TEST_CASE("parser: \\log with explicit base", "[parser]") {
    Expr log2x = make_div(make_fn(FunctionId::Ln, x()), make_fn(FunctionId::Ln, num(2)));
    check_parse("\\log_2 x", log2x);
    check_parse("\\log_{2} x", log2x);
    check_parse("\\log_{2}(x)", log2x);
    check_parse("log_2(x)", log2x);
    check_parse("\\log_{x+1} y",
                make_div(make_fn(FunctionId::Ln, y()),
                         make_fn(FunctionId::Ln, make_add({x(), num(1)}))));
}

TEST_CASE("parser: bare function argument is the tight factor sequence", "[parser]") {
    // Worked examples from DESIGN.md section 4 — must match exactly.
    check_parse("\\sin 2x", make_fn(FunctionId::Sin, make_mul({num(2), x()})));
    check_parse("\\sin x \\cos y",
                make_mul({make_fn(FunctionId::Sin, x()), make_fn(FunctionId::Cos, y())}));
    check_parse("\\sin x + 1", make_add({make_fn(FunctionId::Sin, x()), num(1)}));

    check_parse("\\sin 2 x", make_fn(FunctionId::Sin, make_mul({num(2), x()})));
    check_parse("\\sin x y", make_fn(FunctionId::Sin, make_mul({x(), y()})));
    check_parse("\\sin x^2", make_fn(FunctionId::Sin, make_pow(x(), num(2))));
    check_parse("\\sin 2\\pi x",
                make_fn(FunctionId::Sin, make_mul({num(2), make_const(ConstantId::Pi), x()})));
    // A group right after the function name is exactly the argument.
    check_parse("\\sin(x) y", make_mul({make_fn(FunctionId::Sin, x()), y()}));
    check_parse("\\sin \\frac{1}{2}", make_fn(FunctionId::Sin, num(1, 2)));
    check_parse("\\sin \\sqrt{x}", make_fn(FunctionId::Sin, make_pow(x(), num(1, 2))));
    // Stops at * and /.
    check_parse("\\sin x * y", make_mul({make_fn(FunctionId::Sin, x()), y()}));
    check_parse("\\sin x / y", make_div(make_fn(FunctionId::Sin, x()), y()));
    check_parse("\\cos x - 1", make_sub(make_fn(FunctionId::Cos, x()), num(1)));
}

TEST_CASE("parser: sin^n and sin^-1 notations", "[parser]") {
    check_parse("\\sin^2 x", make_pow(make_fn(FunctionId::Sin, x()), num(2)));
    check_parse("\\sin^{2} x", make_pow(make_fn(FunctionId::Sin, x()), num(2)));
    check_parse("\\sin^{-1} x", make_fn(FunctionId::Asin, x()));
    check_parse("sin^-1 x", make_fn(FunctionId::Asin, x()));
    check_parse("\\cos^{-1}{x}", make_fn(FunctionId::Acos, x()));
    check_parse("\\tan^{-1}(x)", make_fn(FunctionId::Atan, x()));
    // Non-invertible names keep the power.
    check_parse("\\sinh^{-1} x", make_pow(make_fn(FunctionId::Sinh, x()), num(-1)));
    // sin(x)^2 via postfix on a grouped call.
    check_parse("sin(x)^2", make_pow(make_fn(FunctionId::Sin, x()), num(2)));
}

TEST_CASE("parser: e^x and numeric folding through factories", "[parser]") {
    check_parse("e^x", make_pow(make_const(ConstantId::E), x()));
    check_parse("1 + 2 * 3", num(7));
    check_parse("(2 + 3)^2", num(25));
}

// ---------------------------------------------------------------------------
// Equations and top-level API
// ---------------------------------------------------------------------------

TEST_CASE("parser: parse_input returns expression or equation", "[parser]") {
    auto v1 = parse_input("x + 1");
    REQUIRE(std::holds_alternative<Expr>(v1));
    REQUIRE(structurally_equal(std::get<Expr>(v1), make_add({x(), num(1)})));

    auto v2 = parse_input("x^2 = 4");
    REQUIRE(std::holds_alternative<Equation>(v2));
    const auto& eq = std::get<Equation>(v2);
    REQUIRE(structurally_equal(eq.lhs, make_pow(x(), num(2))));
    REQUIRE(structurally_equal(eq.rhs, num(4)));
}

TEST_CASE("parser: parse_equation", "[parser]") {
    Equation eq = parse_equation("2x + 1 = 5");
    REQUIRE(structurally_equal(eq.lhs, make_add({make_mul({num(2), x()}), num(1)})));
    REQUIRE(structurally_equal(eq.rhs, num(5)));

    REQUIRE_THROWS_AS(parse_equation("x + 1"), ParseError); // no '='
    REQUIRE_THROWS_AS(parse_equation("x = y = z"), ParseError);
}

TEST_CASE("parser: parse_expression rejects equations", "[parser]") {
    REQUIRE_THROWS_AS(parse_expression("x = 1"), ParseError);
}

// ---------------------------------------------------------------------------
// Errors: messages and exact spans
// ---------------------------------------------------------------------------

TEST_CASE("parser: empty input", "[parser][error]") {
    ParseError e = capture_error([] { parse_expression(""); });
    CHECK(message_contains(e, "unexpected end of input"));
    CHECK(e.begin() == 0);
    CHECK(e.end() == 0);
    REQUIRE_THROWS_AS(parse_expression("   "), ParseError);
    REQUIRE_THROWS_AS(parse_input(""), ParseError);
}

TEST_CASE("parser: function with no argument", "[parser][error]") {
    ParseError e = capture_error([] { parse_expression("sin +"); });
    CHECK(message_contains(e, "sin"));
    CHECK(e.begin() == 0);
    CHECK(e.end() == 3);
    REQUIRE_THROWS_AS(parse_expression("x sin"), ParseError);
    REQUIRE_THROWS_AS(parse_expression("\\sin \\cos x"), ParseError); // stop token first
}

TEST_CASE("parser: unclosed parenthesis points at the opener", "[parser][error]") {
    ParseError e = capture_error([] { parse_expression("(x"); });
    CHECK(message_contains(e, "missing ')'"));
    CHECK(e.begin() == 0);
    CHECK(e.end() == 1);
    REQUIRE_THROWS_AS(parse_expression("{x"), ParseError);
    REQUIRE_THROWS_AS(parse_expression("[x"), ParseError);
}

TEST_CASE("parser: dangling subscript", "[parser][error]") {
    ParseError e = capture_error([] { parse_expression("x_"); });
    CHECK(message_contains(e, "subscript"));
    CHECK(e.begin() == 1);
    CHECK(e.end() == 2);
    REQUIRE_THROWS_AS(parse_expression("x_{12"), ParseError);
    REQUIRE_THROWS_AS(parse_expression("2_1"), ParseError);   // subscript on a number
    REQUIRE_THROWS_AS(parse_expression("(x)_1"), ParseError); // subscript on ')'
}

TEST_CASE("parser: unknown command span covers the command", "[parser][error]") {
    ParseError e = capture_error([] { parse_expression("\\fraq{1}{2} + x"); });
    CHECK(message_contains(e, "unknown command"));
    CHECK(message_contains(e, "\\fraq"));
    CHECK(e.begin() == 0);
    CHECK(e.end() == 5);

    ParseError e2 = capture_error([] { parse_expression("1 + \\foo"); });
    CHECK(e2.begin() == 4);
    CHECK(e2.end() == 8);
    REQUIRE_THROWS_AS(parse_expression("\\asin x"), ParseError); // not a LaTeX command
}

TEST_CASE("parser: malformed number literal", "[parser][error]") {
    ParseError e = capture_error([] { parse_expression("1..2"); });
    CHECK(message_contains(e, "number"));
    CHECK(e.begin() == 0);
    CHECK(e.end() == 2);
    REQUIRE_THROWS_AS(parse_expression("1."), ParseError);
    REQUIRE_THROWS_AS(parse_expression(".5"), ParseError); // '.' cannot start a literal
}

TEST_CASE("parser: second top-level '=' is rejected", "[parser][error]") {
    ParseError e = capture_error([] { parse_input("x = y = z"); });
    CHECK(message_contains(e, "'='"));
    CHECK(e.begin() == 6);
    CHECK(e.end() == 7);
}

TEST_CASE("parser: comma inside parentheses", "[parser][error]") {
    ParseError e = capture_error([] { parse_expression("(1, 2)"); });
    CHECK(message_contains(e, "','"));
    CHECK(e.begin() == 2);
    CHECK(e.end() == 3);
    REQUIRE_THROWS_AS(parse_expression("sin(1, 2)"), ParseError);
    REQUIRE_THROWS_AS(parse_expression("1, 2"), ParseError);
}

TEST_CASE("parser: mismatched and unmatched left/right", "[parser][error]") {
    ParseError e = capture_error([] { parse_expression("\\left( x \\right]"); });
    CHECK(message_contains(e, "\\right"));
    CHECK(e.begin() == 15);
    CHECK(e.end() == 16);
    REQUIRE_THROWS_AS(parse_expression("\\left( x"), ParseError);   // missing \right
    REQUIRE_THROWS_AS(parse_expression("x \\right)"), ParseError);  // \right without \left
    REQUIRE_THROWS_AS(parse_expression("\\left x \\right x"), ParseError);
}

TEST_CASE("parser: assorted malformed input", "[parser][error]") {
    REQUIRE_THROWS_AS(parse_expression(")"), ParseError);
    REQUIRE_THROWS_AS(parse_expression("x +"), ParseError);
    REQUIRE_THROWS_AS(parse_expression("x ) y"), ParseError);
    REQUIRE_THROWS_AS(parse_expression("* x"), ParseError);
    REQUIRE_THROWS_AS(parse_expression("x ^"), ParseError);
    REQUIRE_THROWS_AS(parse_expression("x @ y"), ParseError); // unexpected character
    REQUIRE_THROWS_AS(parse_expression("\\frac 1 2"), ParseError);
    REQUIRE_THROWS_AS(parse_expression("\\frac{1}"), ParseError);
    REQUIRE_THROWS_AS(parse_expression("\\frac{1}{2"), ParseError);
    REQUIRE_THROWS_AS(parse_expression("\\sqrt[3{x}"), ParseError);
    REQUIRE_THROWS_AS(parse_expression("\\log_ +"), ParseError);
    REQUIRE_THROWS_AS(parse_input("x ="), ParseError);
    REQUIRE_THROWS_AS(parse_input("= x"), ParseError);
}

TEST_CASE("parser: deeply nested input throws instead of overflowing the stack",
          "[parser][error]") {
    // Field reproducer: 7000 nested parens crashed the process with SIGSEGV
    // before the recursion-depth guard existed. It must now throw a clean
    // ParseError well before the native call stack is exhausted.
    const std::string deep = std::string(7000, '(') + "x" + std::string(7000, ')');
    ParseError e = capture_error([&] { parse_expression(deep); });
    CHECK(message_contains(e, "too deeply nested"));

    // The guard sits in parse_unary, so it also bounds unary-sign chains and
    // right-associative power towers, which recurse without nesting parens.
    REQUIRE_THROWS_AS(parse_expression("1" + std::string(7000, '-') + "x"), ParseError);
    std::string tower;
    for (int i = 0; i < 5000; ++i) {
        tower += "2^";
    }
    tower += "2";
    REQUIRE_THROWS_AS(parse_expression(tower), ParseError);

    // Modest nesting well under the limit still parses fine.
    REQUIRE_NOTHROW(parse_expression(std::string(64, '(') + "x" + std::string(64, ')')));
}

TEST_CASE("parser: unexpected non-ASCII byte reports the whole character",
          "[parser][error]") {
    // A bare greek letter 'α' (U+03B1 = 0xCE 0xB1) is not a valid token. The
    // error must cover BOTH bytes of the UTF-8 sequence (not a lone 0xCE byte)
    // and spell it out as a hex escape so the message is itself valid UTF-8.
    ParseError e = capture_error([] { parse_expression("\xCE\xB1"); });
    CHECK(message_contains(e, "\\xCE\\xB1"));
    CHECK(e.begin() == 0);
    CHECK(e.end() == 2); // span covers the full two-byte character

    // A plain ASCII unexpected byte still spans a single byte, printed as-is.
    ParseError ascii = capture_error([] { parse_expression("@"); });
    CHECK(message_contains(ascii, "'@'"));
    CHECK(ascii.begin() == 0);
    CHECK(ascii.end() == 1);
}
