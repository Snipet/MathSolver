#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "mathsolver/expr.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/printer.hpp"
#include "mathsolver/rational.hpp"

using namespace mathsolver;

namespace {

Expr x() { return make_sym("x"); }
Expr y() { return make_sym("y"); }
Expr z() { return make_sym("z"); }
Expr num(long long n) { return make_num(n); }
Expr num(long long n, long long d) { return make_num(Rational(n, d)); }
Expr e_() { return make_const(ConstantId::E); }
Expr pi_() { return make_const(ConstantId::Pi); }

std::string plain(const Expr& e) { return to_string(e, PrintStyle::Plain); }
std::string latex(const Expr& e) { return to_string(e, PrintStyle::LaTeX); }

void check_round_trip(const Expr& e) {
    const std::string p = to_string(e, PrintStyle::Plain);
    const std::string l = to_string(e, PrintStyle::LaTeX);
    INFO("expr:           " << debug_string(e));
    INFO("plain:          " << p);
    INFO("latex:          " << l);
    const Expr from_plain = parse_expression(p);
    INFO("reparsed plain: " << debug_string(from_plain));
    CHECK(structurally_equal(from_plain, e));
    const Expr from_latex = parse_expression(l);
    INFO("reparsed latex: " << debug_string(from_latex));
    CHECK(structurally_equal(from_latex, e));
}

} // namespace

// ---------------------------------------------------------------------------
// Leaves
// ---------------------------------------------------------------------------

TEST_CASE("printer: numbers", "[printer]") {
    CHECK(plain(num(5)) == "5");
    CHECK(plain(num(0)) == "0");
    CHECK(plain(num(-7)) == "-7");
    CHECK(plain(num(3, 2)) == "3/2");
    CHECK(plain(num(-3, 2)) == "-3/2");
    CHECK(latex(num(5)) == "5");
    CHECK(latex(num(-7)) == "-7");
    CHECK(latex(num(3, 2)) == "\\frac{3}{2}");
    CHECK(latex(num(-3, 2)) == "-\\frac{3}{2}");
}

TEST_CASE("printer: symbols and constants", "[printer]") {
    CHECK(plain(x()) == "x");
    CHECK(plain(make_sym("alpha")) == "alpha");
    CHECK(plain(pi_()) == "pi");
    CHECK(plain(e_()) == "e");
    CHECK(latex(x()) == "x");
    CHECK(latex(make_sym("alpha")) == "\\alpha");
    CHECK(latex(make_sym("theta")) == "\\theta");
    CHECK(latex(pi_()) == "\\pi");
    CHECK(latex(e_()) == "e");
}

TEST_CASE("printer: subscripted symbols", "[printer]") {
    // Plain keeps the unbraced form where it re-lexes to one symbol...
    CHECK(plain(make_sym("x_12")) == "x_12");
    CHECK(plain(make_sym("x_a")) == "x_a");
    // ...and braces the forms that would not (x_max would re-lex as x_m*a*x).
    CHECK(plain(make_sym("x_max")) == "x_{max}");
    CHECK(plain(make_sym("x_a1")) == "x_{a1}");
    CHECK(latex(make_sym("x_12")) == "x_{12}");
    CHECK(latex(make_sym("x_a")) == "x_{a}");
    CHECK(latex(make_sym("alpha_2")) == "\\alpha_{2}");
}

// ---------------------------------------------------------------------------
// Sums: subtraction rendering and display ordering
// ---------------------------------------------------------------------------

TEST_CASE("printer: add renders subtraction", "[printer]") {
    CHECK(plain(make_add({x(), make_mul({num(-2), y()})})) == "x - 2*y");
    CHECK(plain(make_add({x(), num(-2)})) == "x - 2");
    CHECK(plain(make_add({x(), num(-3, 2)})) == "x - 3/2");
    CHECK(plain(make_sub(num(1), make_mul({num(2), x()}))) == "-2*x + 1");
    CHECK(latex(make_add({x(), make_mul({num(-2), y()})})) == "x - 2y");
    CHECK(latex(make_add({x(), num(-3, 2)})) == "x - \\frac{3}{2}");
}

TEST_CASE("printer: add display ordering is descending total degree", "[printer]") {
    Expr poly = make_add({num(3), make_mul({num(2), x()}), make_pow(x(), num(2))});
    CHECK(plain(poly) == "x^2 + 2*x + 3");
    CHECK(latex(poly) == "x^{2} + 2x + 3");
    // Numbers (degree 0) sort last; e^2 counts as degree 2.
    CHECK(plain(make_add({num(-1), make_pow(e_(), num(2))})) == "e^2 - 1");
    // Equal-degree terms keep the canonical arg order (Constant < Symbol),
    // so the pinned subtraction example x - 2*y holds (see test above).
    CHECK(plain(make_add({x(), pi_(), num(1)})) == "pi + x + 1");
    CHECK(plain(make_add({x(), y()})) == "x + y");
    // A negative leading term keeps its minus sign.
    Expr t = make_add({num(1), make_mul({num(-1), make_pow(make_fn(FunctionId::Tanh, x()),
                                                           num(2))})});
    CHECK(plain(t) == "-tanh(x)^2 + 1");
}

// ---------------------------------------------------------------------------
// Products and division reconstruction
// ---------------------------------------------------------------------------

TEST_CASE("printer: basic products", "[printer]") {
    CHECK(plain(make_mul({num(2), pi_(), make_sym("r")})) == "2*pi*r");
    CHECK(plain(make_mul({num(-1), x()})) == "-x");
    CHECK(plain(make_mul({num(-3), x()})) == "-3*x");
    CHECK(plain(make_mul({num(3, 2), x()})) == "3*x/2");
    // Mul args stay in canonical order (Symbol < Add); the Add factor is
    // parenthesized wherever it lands.
    CHECK(plain(make_mul({make_add({x(), num(1)}), y()})) == "y*(x + 1)");
    CHECK(plain(make_mul({make_add({x(), num(1)}), make_add({x(), num(-2)})})) ==
          "(x - 2)*(x + 1)");
    CHECK(latex(make_mul({num(2), x()})) == "2x");
    CHECK(latex(make_mul({num(2), pi_(), make_sym("r")})) == "2\\pi r");
    CHECK(latex(make_mul({num(-1), x()})) == "-x");
}

TEST_CASE("printer: division rendering (plain)", "[printer]") {
    CHECK(plain(make_mul({num(3, 2), make_pow(x(), num(-1))})) == "3/(2*x)");
    CHECK(plain(make_mul({num(3), make_pow(x(), num(-2))})) == "3/x^2");
    CHECK(plain(make_mul({num(1, 2), make_pow(x(), num(-1))})) == "1/(2*x)");
    CHECK(plain(make_pow(x(), num(-1))) == "1/x");
    CHECK(plain(make_pow(x(), num(-2))) == "1/x^2");
    CHECK(plain(make_mul({num(-1), make_sym("b"), make_pow(make_sym("a"), num(-1))})) == "-b/a");
    CHECK(plain(make_mul({num(3), make_pow(make_add({x(), num(1)}), num(-1))})) == "3/(x + 1)");
    CHECK(plain(make_pow(make_mul({x(), y()}), num(-1))) == "1/(x*y)");
}

TEST_CASE("printer: division rendering (latex)", "[printer]") {
    CHECK(latex(make_mul({num(3, 2), make_pow(x(), num(-1))})) == "\\frac{3}{2x}");
    CHECK(latex(make_mul({num(3), make_pow(x(), num(-2))})) == "\\frac{3}{x^{2}}");
    CHECK(latex(make_pow(x(), num(-1))) == "\\frac{1}{x}");
    CHECK(latex(make_mul({make_add({x(), num(1)}), make_pow(y(), num(-1))})) ==
          "\\frac{x + 1}{y}");
    CHECK(latex(make_mul({num(1, 2), make_pow(make_add({x(), num(1)}), num(-1))})) ==
          "\\frac{1}{2\\left(x + 1\\right)}");
}

TEST_CASE("printer: sqrt reconstruction", "[printer]") {
    CHECK(plain(make_sqrt(x())) == "sqrt(x)");
    CHECK(plain(make_pow(x(), num(-1, 2))) == "1/sqrt(x)");
    CHECK(plain(make_mul({num(3), make_pow(x(), num(-1, 2))})) == "3/sqrt(x)");
    CHECK(plain(make_sqrt(make_add({x(), num(1)}))) == "sqrt(x + 1)");
    CHECK(latex(make_sqrt(x())) == "\\sqrt{x}");
    CHECK(latex(make_pow(x(), num(-1, 2))) == "\\frac{1}{\\sqrt{x}}");
}

TEST_CASE("printer: e^x rule wins over sqrt and division", "[printer]") {
    CHECK(plain(make_exp(x())) == "e^x");
    CHECK(plain(make_pow(e_(), num(1, 2))) == "e^(1/2)");
    CHECK(plain(make_pow(e_(), num(-2))) == "e^(-2)");
    CHECK(plain(make_mul({num(3), make_pow(e_(), num(-2))})) == "3*e^(-2)");
    CHECK(latex(make_exp(x())) == "e^{x}");
    CHECK(latex(make_pow(e_(), num(1, 2))) == "e^{\\frac{1}{2}}");
    CHECK(latex(make_pow(e_(), num(-2))) == "e^{-2}");
}

// ---------------------------------------------------------------------------
// Parenthesization
// ---------------------------------------------------------------------------

TEST_CASE("printer: pow parenthesization", "[printer]") {
    CHECK(plain(make_pow(x(), make_pow(y(), z()))) == "x^(y^z)");
    CHECK(plain(make_pow(make_pow(x(), y()), z())) == "(x^y)^z");
    CHECK(plain(make_pow(make_add({x(), num(1)}), num(2))) == "(x + 1)^2");
    CHECK(plain(make_pow(make_mul({x(), y()}), num(2))) == "(x*y)^2");
    CHECK(plain(make_pow(num(-2), x())) == "(-2)^x");
    CHECK(plain(make_pow(num(1, 2), x())) == "(1/2)^x");
    CHECK(plain(make_pow(x(), num(2, 3))) == "x^(2/3)");
    CHECK(plain(make_pow(x(), make_add({y(), num(1)}))) == "x^(y + 1)");
    CHECK(plain(make_pow(make_fn(FunctionId::Sin, x()), num(2))) == "sin(x)^2");
    CHECK(latex(make_pow(x(), num(10))) == "x^{10}");
    CHECK(latex(make_pow(make_add({x(), num(1)}), num(2))) == "\\left(x + 1\\right)^{2}");
    CHECK(latex(make_pow(num(-2), x())) == "\\left(-2\\right)^{x}");
    CHECK(latex(make_pow(x(), make_add({y(), num(1)}))) == "x^{y + 1}");
}

TEST_CASE("printer: negated sums are parenthesized", "[printer]") {
    Expr e = make_mul({num(-1), make_add({x(), y()})});
    CHECK(plain(e) == "-(x + y)");
    CHECK(latex(e) == "-\\left(x + y\\right)");
    CHECK(plain(make_add({z(), e})) == "z - (x + y)");
}

// ---------------------------------------------------------------------------
// Functions
// ---------------------------------------------------------------------------

TEST_CASE("printer: functions", "[printer]") {
    CHECK(plain(make_fn(FunctionId::Sin, x())) == "sin(x)");
    CHECK(plain(make_fn(FunctionId::Asin, x())) == "asin(x)");
    CHECK(plain(make_fn(FunctionId::Ln, x())) == "ln(x)");
    CHECK(plain(make_fn(FunctionId::Abs, x())) == "abs(x)");
    CHECK(plain(make_fn(FunctionId::Sin, make_mul({num(2), x()}))) == "sin(2*x)");
    CHECK(latex(make_fn(FunctionId::Sin, x())) == "\\sin\\left(x\\right)");
    CHECK(latex(make_fn(FunctionId::Cos, x())) == "\\cos\\left(x\\right)");
    CHECK(latex(make_fn(FunctionId::Asin, x())) == "\\arcsin\\left(x\\right)");
    CHECK(latex(make_fn(FunctionId::Atan, x())) == "\\arctan\\left(x\\right)");
    CHECK(latex(make_fn(FunctionId::Abs, x())) == "\\left|x\\right|");
    CHECK(latex(make_fn(FunctionId::Ln, x())) == "\\ln\\left(x\\right)");
}

// ---------------------------------------------------------------------------
// LaTeX juxtaposition rules
// ---------------------------------------------------------------------------

TEST_CASE("printer: latex digit-boundary cdot rule", "[printer]") {
    CHECK(latex(make_mul({num(2), make_pow(num(10), x())})) == "2 \\cdot 10^{x}");
    CHECK(plain(make_mul({num(2), make_pow(num(10), x())})) == "2*10^x");
    // No \cdot when the boundary is not digit|digit.
    CHECK(latex(make_mul({num(2), make_pow(x(), num(2))})) == "2x^{2}");
}

TEST_CASE("printer: latex letter boundaries do not merge into known names", "[printer]") {
    // "ln" would re-lex as the ln function; "l n" must not.
    Expr e = make_mul({make_sym("l"), make_sym("n")});
    CHECK(latex(e) == "l n");
    check_round_trip(e);
    // "i" is the imaginary unit as of v0.6 — the letter-boundary concern
    // stands: "i p" must re-lex as i (the constant) times p, never "ip".
    Expr pi_ish = make_mul({make_const(ConstantId::I), make_sym("p")});
    check_round_trip(pi_ish);
}

// ---------------------------------------------------------------------------
// Equations
// ---------------------------------------------------------------------------

TEST_CASE("printer: equations", "[printer]") {
    Equation eq{x(), num(2)};
    CHECK(to_string(eq) == "x = 2");
    CHECK(to_string(eq, PrintStyle::LaTeX) == "x = 2");
    Equation eq2{make_pow(x(), num(2)), make_mul({num(1, 2), y()})};
    CHECK(to_string(eq2) == "x^2 = y/2");
    CHECK(to_string(eq2, PrintStyle::LaTeX) == "x^{2} = \\frac{y}{2}");

    Equation reparsed = parse_equation(to_string(eq2));
    CHECK(structurally_equal(reparsed.lhs, eq2.lhs));
    CHECK(structurally_equal(reparsed.rhs, eq2.rhs));
    Equation reparsed_latex = parse_equation(to_string(eq2, PrintStyle::LaTeX));
    CHECK(structurally_equal(reparsed_latex.lhs, eq2.lhs));
    CHECK(structurally_equal(reparsed_latex.rhs, eq2.rhs));
}

// ---------------------------------------------------------------------------
// THE key property: parse(to_string(e, style)) is structurally_equal to e
// ---------------------------------------------------------------------------

TEST_CASE("printer: round-trip corpus, both styles", "[printer][roundtrip]") {
    const std::vector<Expr> corpus = {
        // Leaves
        num(42),
        num(-7),
        num(3, 2),
        num(-3, 2),
        x(),
        make_sym("alpha"),
        make_sym("x_12"),
        make_sym("alpha_2"),
        make_sym("x_max"),
        pi_(),
        e_(),
        // Sums (incl. subtraction and ordering)
        make_add({x(), num(1)}),
        make_add({make_pow(x(), num(2)), make_mul({num(2), x()}), num(3)}),
        make_sub(x(), y()),
        make_sub(num(1), make_mul({num(2), x()})),
        make_add({x(), pi_(), num(-3, 2)}),
        // Products
        make_mul({num(2), pi_(), make_sym("r")}),
        make_neg(x()),
        make_mul({num(3, 2), x()}),
        make_mul({num(-1, 2), x()}),
        make_mul({make_add({x(), num(1)}), make_add({y(), num(-2)})}),
        make_mul({x(), make_pow(num(2), y())}),
        make_mul({num(-1), make_add({x(), y()})}),
        // Division reconstruction
        make_mul({num(3, 2), make_pow(x(), num(-1))}),
        make_mul({num(3), make_pow(x(), num(-2))}),
        make_mul({num(1, 2), make_pow(x(), num(-1))}),
        make_pow(x(), num(-1)),
        make_mul({num(3), make_pow(make_add({x(), num(1)}), num(-1))}),
        make_pow(make_mul({x(), y()}), num(-1)),
        make_mul({num(-1), make_sym("b"), make_pow(make_sym("a"), num(-1))}),
        // Multiple negative-exponent factors (cannot share one denominator)
        make_mul({x(), make_pow(y(), num(-1)), make_pow(z(), num(-1))}),
        make_mul({make_pow(x(), num(-1)), make_pow(y(), num(-1))}),
        make_mul({num(3, 2), make_pow(x(), num(-1)), make_pow(y(), num(-2))}),
        // Nested fraction: (1 + 1/x) / (1 + 1/y)
        make_div(make_add({num(1), make_pow(x(), num(-1))}),
                 make_add({num(1), make_pow(y(), num(-1))})),
        // sqrt and rational exponents
        make_sqrt(x()),
        make_sqrt(make_add({x(), num(1)})),
        make_pow(x(), num(-1, 2)),
        make_pow(x(), num(2, 3)),
        make_pow(x(), num(-3, 2)),
        make_mul({num(1, 2), make_sqrt(num(3))}),
        make_pow(make_mul({num(2), x()}), num(1, 2)),
        // e^... exception
        make_exp(x()),
        make_exp(make_neg(x())),
        make_pow(e_(), num(1, 2)),
        make_pow(e_(), num(-2)),
        make_mul({num(3, 2), make_pow(e_(), num(-1))}),
        // Pow shapes
        make_pow(num(2), x()),
        make_mul({num(2), make_pow(num(10), x())}),
        make_pow(num(-2), x()),
        make_pow(num(1, 2), x()),
        make_pow(make_add({x(), num(1)}), num(2)),
        make_pow(make_mul({x(), y()}), num(2)),
        make_pow(x(), make_add({y(), num(1)})),
        make_pow(x(), make_pow(y(), z())),
        make_pow(make_pow(x(), y()), z()),
        make_pow(make_pow(x(), num(1, 2)), y()),
        // Every FunctionId, some with composite arguments
        make_fn(FunctionId::Sin, x()),
        make_fn(FunctionId::Cos, make_add({x(), num(1)})),
        make_fn(FunctionId::Tan, make_mul({num(2), x()})),
        make_fn(FunctionId::Asin, x()),
        make_fn(FunctionId::Acos, num(-1, 2)),
        make_fn(FunctionId::Atan, make_sqrt(num(3))),
        make_fn(FunctionId::Sinh, x()),
        make_fn(FunctionId::Cosh, make_neg(x())),
        make_fn(FunctionId::Tanh, x()),
        make_fn(FunctionId::Ln, make_mul({x(), y()})),
        make_fn(FunctionId::Abs, make_sub(x(), num(3))),
        make_pow(make_fn(FunctionId::Sin, x()), num(2)),
        make_pow(make_fn(FunctionId::Cos, x()), num(-2)),
        // Kitchen sink
        make_add({make_mul({num(1, 2), make_pow(make_fn(FunctionId::Sin, x()), num(2))}),
                  make_mul({num(-1, 3), make_pow(make_fn(FunctionId::Cos, y()), num(-1))}),
                  num(5, 7)}),
        make_add({make_mul({num(2), pi_(), make_sym("n")}),
                  make_fn(FunctionId::Asin, num(1, 2))}),
    };
    REQUIRE(corpus.size() >= 30);
    for (const Expr& e : corpus) {
        check_round_trip(e);
    }
}

TEST_CASE("printer: abs renders as LaTeX bars and round-trips") {
    const Expr e = parse_expression("abs(x - 1)");
    const std::string latex = to_string(e, PrintStyle::LaTeX);
    CHECK(latex == "\\left|x - 1\\right|");
    CHECK(structurally_equal(parse_expression(latex), e));
    // Plain style unchanged.
    CHECK(to_string(e, PrintStyle::Plain) == "abs(x - 1)");
}
