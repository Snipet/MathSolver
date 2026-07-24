#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <climits>
#include <random>
#include <stdexcept>
#include <vector>

#include "mathsolver/errors.hpp"
#include "mathsolver/expr.hpp"

using namespace mathsolver;

namespace {

Expr x() { return make_sym("x"); }
Expr y() { return make_sym("y"); }
Expr z() { return make_sym("z"); }

} // namespace

// ---------------------------------------------------------------------------
// Leaves and accessors
// ---------------------------------------------------------------------------

TEST_CASE("expr: leaf factories and accessors", "[expr]") {
    Expr n = make_num(Rational(3, 2));
    REQUIRE(n->kind() == Kind::Number);
    REQUIRE(n->number() == Rational(3, 2));
    REQUIRE(n->args().empty());

    Expr s = make_sym("alpha");
    REQUIRE(s->kind() == Kind::Symbol);
    REQUIRE(s->symbol_name() == "alpha");

    Expr pi = make_const(ConstantId::Pi);
    REQUIRE(pi->kind() == Kind::Constant);
    REQUIRE(pi->constant() == ConstantId::Pi);

    Expr f = make_fn(FunctionId::Sin, x());
    REQUIRE(f->kind() == Kind::Function);
    REQUIRE(f->function() == FunctionId::Sin);
    REQUIRE(f->args().size() == 1);
}

TEST_CASE("expr: accessors throw std::logic_error on kind mismatch", "[expr]") {
    Expr n = make_num(1);
    Expr s = make_sym("x");
    REQUIRE_THROWS_AS(n->symbol_name(), std::logic_error);
    REQUIRE_THROWS_AS(n->constant(), std::logic_error);
    REQUIRE_THROWS_AS(n->function(), std::logic_error);
    REQUIRE_THROWS_AS(s->number(), std::logic_error);
    REQUIRE_THROWS_AS(make_const(ConstantId::E)->number(), std::logic_error);
    REQUIRE_THROWS_AS(make_fn(FunctionId::Ln, s)->number(), std::logic_error);
}

TEST_CASE("expr: function names", "[expr]") {
    REQUIRE(function_name(FunctionId::Sin) == "sin");
    REQUIRE(function_name(FunctionId::Asin) == "asin");
    REQUIRE(function_name(FunctionId::Ln) == "ln");
    REQUIRE(function_name(FunctionId::Abs) == "abs");
    REQUIRE(function_from_name("sin") == FunctionId::Sin);
    REQUIRE(function_from_name("arcsin") == FunctionId::Asin);
    REQUIRE(function_from_name("arctan") == FunctionId::Atan);
    REQUIRE(function_from_name("tanh") == FunctionId::Tanh);
    REQUIRE_FALSE(function_from_name("exp").has_value());
    REQUIRE_FALSE(function_from_name("sqrt").has_value());
    REQUIRE_FALSE(function_from_name("log").has_value());
    REQUIRE_FALSE(function_from_name("sec").has_value());
    REQUIRE_FALSE(function_from_name("nope").has_value());
}

// ---------------------------------------------------------------------------
// make_add
// ---------------------------------------------------------------------------

TEST_CASE("expr: make_add canonicalization", "[expr][add]") {
    SECTION("numeric folding") {
        Expr e = make_add({make_num(2), make_num(Rational(1, 2)), make_num(3)});
        INFO(debug_string(e));
        REQUIRE(structurally_equal(e, make_num(Rational(11, 2))));
    }
    SECTION("zero fold is dropped") {
        Expr e = make_add({make_num(2), x(), make_num(-2)});
        INFO(debug_string(e));
        REQUIRE(structurally_equal(e, x()));
    }
    SECTION("numeric fold cancels to 0") {
        Expr e = make_add({make_num(Rational(1, 2)), make_num(Rational(1, 3)),
                           make_num(Rational(-5, 6))});
        INFO(debug_string(e));
        REQUIRE(e->kind() == Kind::Number);
        REQUIRE(e->number().is_zero());
        REQUIRE(structurally_equal(e, make_num(0)));
    }
    SECTION("empty and singleton") {
        REQUIRE(structurally_equal(make_add({}), make_num(0)));
        REQUIRE(structurally_equal(make_add({x()}), x()));
    }
    SECTION("only zeros") {
        REQUIRE(structurally_equal(make_add({make_num(0), make_num(0)}), make_num(0)));
    }
    SECTION("args are sorted: numbers first, then by kind rank") {
        Expr e = make_add({x(), make_num(3), make_const(ConstantId::Pi)});
        REQUIRE(debug_string(e) == "(add 3 pi x)");
    }
    SECTION("flattens nested adds") {
        Expr inner = make_add({y(), z()});
        Expr e = make_add({x(), inner});
        REQUIRE(debug_string(e) == "(add x y z)");
    }
    SECTION("deep flattening") {
        Expr e = make_add({make_num(1),
                           make_add({x(), make_add({y(), make_add({z(), make_num(2)})})})});
        INFO(debug_string(e));
        REQUIRE(e->kind() == Kind::Add);
        REQUIRE(e->args().size() == 4);
        REQUIRE(debug_string(e) == "(add 3 x y z)");
        for (const auto& a : e->args()) {
            REQUIRE(a->kind() != Kind::Add);
        }
    }
    SECTION("duplicate terms are kept (like-term collection is simplify's job)") {
        Expr e = make_add({x(), x()});
        REQUIRE(debug_string(e) == "(add x x)");
    }
}

// ---------------------------------------------------------------------------
// make_mul
// ---------------------------------------------------------------------------

TEST_CASE("expr: make_mul canonicalization", "[expr][mul]") {
    SECTION("literal 0 factor annihilates") {
        Expr e = make_mul({x(), make_num(0), y()});
        INFO(debug_string(e));
        REQUIRE(structurally_equal(e, make_num(0)));
    }
    SECTION("literal 0 inside a nested mul argument annihilates too") {
        // Built by hand through the factory: {x, 0*y} folds the inner first,
        // but even {x, num 0} in a flattened position must give 0.
        Expr e = make_mul({make_mul({x(), y()}), make_num(0)});
        REQUIRE(structurally_equal(e, make_num(0)));
    }
    SECTION("numeric folding") {
        Expr e = make_mul({make_num(2), make_num(Rational(3, 4)), x()});
        INFO(debug_string(e));
        REQUIRE(debug_string(e) == "(mul 3/2 x)");
    }
    SECTION("ones are dropped") {
        Expr e = make_mul({make_num(1), x(), make_num(1)});
        REQUIRE(structurally_equal(e, x()));
    }
    SECTION("numeric fold to exactly 1 disappears") {
        Expr e = make_mul({make_num(Rational(2, 3)), make_num(Rational(3, 2)), x(), y()});
        REQUIRE(debug_string(e) == "(mul x y)");
    }
    SECTION("empty and singleton") {
        REQUIRE(structurally_equal(make_mul({}), make_num(1)));
        REQUIRE(structurally_equal(make_mul({x()}), x()));
    }
    SECTION("all-numeric product") {
        Expr e = make_mul({make_num(6), make_num(7)});
        REQUIRE(structurally_equal(e, make_num(42)));
    }
    SECTION("flattens nested muls deeply") {
        Expr e = make_mul({make_num(2),
                           make_mul({x(), make_mul({y(), make_num(3), make_mul({z()})})})});
        INFO(debug_string(e));
        REQUIRE(e->kind() == Kind::Mul);
        REQUIRE(debug_string(e) == "(mul 6 x y z)");
        for (const auto& a : e->args()) {
            REQUIRE(a->kind() != Kind::Mul);
        }
    }
    SECTION("a Mul never directly nests an Add-free Mul") {
        Expr e = make_mul({make_mul({x(), y()}), make_mul({z(), make_num(5)})});
        REQUIRE(debug_string(e) == "(mul 5 x y z)");
    }
}

TEST_CASE("expr: make_add/make_mul are deterministic under input order", "[expr][order]") {
    std::vector<Expr> pieces = {
        make_num(Rational(7, 3)),
        x(),
        y(),
        make_const(ConstantId::Pi),
        make_const(ConstantId::E),
        make_pow(x(), make_num(2)),
        make_fn(FunctionId::Sin, x()),
        make_fn(FunctionId::Cos, y()),
        make_mul({x(), y()}),
    };
    Expr add_ref = make_add(pieces);
    Expr mul_ref = make_mul(pieces);
    std::mt19937 rng(20260715);
    for (int trial = 0; trial < 25; ++trial) {
        std::vector<Expr> shuffled = pieces;
        std::shuffle(shuffled.begin(), shuffled.end(), rng);
        Expr a = make_add(shuffled);
        Expr m = make_mul(shuffled);
        INFO("trial " << trial << ": " << debug_string(a) << " / " << debug_string(m));
        REQUIRE(debug_string(a) == debug_string(add_ref));
        REQUIRE(debug_string(m) == debug_string(mul_ref));
        REQUIRE(structurally_equal(a, add_ref));
        REQUIRE(structurally_equal(m, mul_ref));
    }
}

// ---------------------------------------------------------------------------
// make_pow
// ---------------------------------------------------------------------------

TEST_CASE("expr: make_pow identities", "[expr][pow]") {
    SECTION("0^0 == 1 by convention") {
        REQUIRE(structurally_equal(make_pow(make_num(0), make_num(0)), make_num(1)));
    }
    SECTION("e == 0 -> 1 for any base") {
        REQUIRE(structurally_equal(make_pow(x(), make_num(0)), make_num(1)));
        REQUIRE(structurally_equal(make_pow(make_const(ConstantId::Pi), make_num(0)),
                                   make_num(1)));
    }
    SECTION("e == 1 -> base") {
        REQUIRE(structurally_equal(make_pow(x(), make_num(1)), x()));
        Expr sum = make_add({x(), y()});
        REQUIRE(structurally_equal(make_pow(sum, make_num(1)), sum));
    }
    SECTION("base 1 -> 1") {
        REQUIRE(structurally_equal(make_pow(make_num(1), x()), make_num(1)));
        REQUIRE(structurally_equal(make_pow(make_num(1), make_num(-5)), make_num(1)));
    }
    SECTION("base 0 with positive rational exponent -> 0") {
        REQUIRE(structurally_equal(make_pow(make_num(0), make_num(3)), make_num(0)));
        REQUIRE(structurally_equal(make_pow(make_num(0), make_num(Rational(1, 2))),
                                   make_num(0)));
    }
    SECTION("base 0 with negative exponent throws") {
        REQUIRE_THROWS_AS(make_pow(make_num(0), make_num(-1)), DivisionByZeroError);
        REQUIRE_THROWS_AS(make_pow(make_num(0), make_num(Rational(-1, 2))),
                          DivisionByZeroError);
    }
    SECTION("base 0 with symbolic exponent stays symbolic") {
        Expr e = make_pow(make_num(0), x());
        REQUIRE(e->kind() == Kind::Pow);
        REQUIRE(debug_string(e) == "(pow 0 x)");
    }
}

TEST_CASE("expr: make_pow exact numeric folding", "[expr][pow]") {
    SECTION("integer exponents fold exactly") {
        REQUIRE(structurally_equal(make_pow(make_num(2), make_num(10)), make_num(1024)));
        REQUIRE(structurally_equal(make_pow(make_num(2), make_num(-1)),
                                   make_num(Rational(1, 2))));
        REQUIRE(structurally_equal(make_pow(make_num(Rational(2, 3)), make_num(-2)),
                                   make_num(Rational(9, 4))));
        REQUIRE(structurally_equal(make_pow(make_num(-3), make_num(3)), make_num(-27)));
    }
    SECTION("large integer exponents fold exactly") {
        // 2^64 is now computed exactly (arbitrary precision), not thrown.
        REQUIRE(structurally_equal(make_pow(make_num(2), make_num(64)),
                                   make_num(Rational(BigInt("18446744073709551616")))));
    }
    SECTION("pow(4, 1/2) -> 2") {
        Expr e = make_pow(make_num(4), make_num(Rational(1, 2)));
        INFO(debug_string(e));
        REQUIRE(structurally_equal(e, make_num(2)));
    }
    SECTION("pow(8/27, 1/3) -> 2/3") {
        Expr e = make_pow(make_num(Rational(8, 27)), make_num(Rational(1, 3)));
        INFO(debug_string(e));
        REQUIRE(structurally_equal(e, make_num(Rational(2, 3))));
    }
    SECTION("pow(2, 1/2) stays symbolic") {
        Expr e = make_pow(make_num(2), make_num(Rational(1, 2)));
        REQUIRE(e->kind() == Kind::Pow);
        REQUIRE(debug_string(e) == "(pow 2 1/2)");
    }
    SECTION("no floating-point false positives near perfect powers") {
        // 4503599627370496 = 2^52; neighbours have doubles-only "roots".
        REQUIRE(structurally_equal(
            make_pow(make_num(4503599627370496LL), make_num(Rational(1, 2))),
            make_num(67108864)));
        Expr off = make_pow(make_num(4503599627370497LL), make_num(Rational(1, 2)));
        REQUIRE(off->kind() == Kind::Pow);
        Expr off2 = make_pow(make_num(4503599627370495LL), make_num(Rational(1, 2)));
        REQUIRE(off2->kind() == Kind::Pow);
        // Large cube root, exact.
        REQUIRE(structurally_equal(
            make_pow(make_num(2097151LL * 2097151LL * 2097151LL), make_num(Rational(1, 3))),
            make_num(2097151)));
    }
    SECTION("negative base with odd root folds, even root stays") {
        REQUIRE(structurally_equal(make_pow(make_num(-8), make_num(Rational(1, 3))),
                                   make_num(-2)));
        REQUIRE(structurally_equal(make_pow(make_num(-8), make_num(Rational(2, 3))),
                                   make_num(4)));
        REQUIRE(structurally_equal(make_pow(make_num(-8), make_num(Rational(-1, 3))),
                                   make_num(Rational(-1, 2))));
        REQUIRE(structurally_equal(make_pow(make_num(Rational(-27, 8)),
                                            make_num(Rational(1, 3))),
                                   make_num(Rational(-3, 2))));
        // Even root index of a negative base never folds.
        Expr e = make_pow(make_num(-4), make_num(Rational(1, 2)));
        REQUIRE(e->kind() == Kind::Pow);
        Expr f = make_pow(make_num(-8), make_num(Rational(1, 2)));
        REQUIRE(f->kind() == Kind::Pow);
        Expr g = make_pow(make_num(-16), make_num(Rational(3, 4)));
        REQUIRE(g->kind() == Kind::Pow);
    }
    SECTION("rational exponent with numerator > 1") {
        REQUIRE(structurally_equal(make_pow(make_num(4), make_num(Rational(3, 2))),
                                   make_num(8)));
        REQUIRE(structurally_equal(make_pow(make_num(Rational(8, 27)),
                                            make_num(Rational(2, 3))),
                                   make_num(Rational(4, 9))));
    }
    SECTION("negative rational exponent") {
        REQUIRE(structurally_equal(make_pow(make_num(4), make_num(Rational(-1, 2))),
                                   make_num(Rational(1, 2))));
        REQUIRE(structurally_equal(make_pow(make_num(Rational(8, 27)),
                                            make_num(Rational(-1, 3))),
                                   make_num(Rational(3, 2))));
    }
    SECTION("partial roots stay symbolic (no half-folding)") {
        // 8 = 2^3: square root is 2*sqrt(2), not rational -> symbolic.
        Expr e = make_pow(make_num(8), make_num(Rational(1, 2)));
        REQUIRE(e->kind() == Kind::Pow);
        // 4/3: numerator is a square, denominator is not.
        Expr f = make_pow(make_num(Rational(4, 3)), make_num(Rational(1, 2)));
        REQUIRE(f->kind() == Kind::Pow);
    }
    SECTION("symbolic base or exponent stays a Pow node") {
        REQUIRE(make_pow(x(), make_num(2))->kind() == Kind::Pow);
        REQUIRE(make_pow(make_num(2), x())->kind() == Kind::Pow);
        REQUIRE(make_pow(make_const(ConstantId::E), x())->kind() == Kind::Pow);
    }
    SECTION("rational exponent with an exact root folds exactly past 64 bits") {
        // pow(4, 101/2) = (4^(1/2))^101 = 2^101, now exact (arbitrary precision).
        Expr e = make_pow(make_num(4), make_num(Rational(101, 2)));
        REQUIRE(structurally_equal(
            e, make_num(Rational(BigInt("2535301200456458802993406410752")))));
        // pow(4, -101/2) = 1 / 2^101.
        Expr f = make_pow(make_num(4), make_num(Rational(-101, 2)));
        REQUIRE(structurally_equal(
            f, make_num(Rational(BigInt(1), BigInt("2535301200456458802993406410752")))));
        // The integer-exponent path also folds exactly: 4^101 = 2^202.
        REQUIRE(structurally_equal(
            make_pow(make_num(4), make_num(101)),
            make_num(Rational(BigInt(
                "6427752177035961102167848369364650410088811975131171341205504")))));
    }
}

TEST_CASE("expr: make_pow structural folds (pow of pow)", "[expr][pow][fold]") {
    SECTION("(u^r)^s combines when r and s are Numbers and s is integer") {
        Expr e = make_pow(make_pow(x(), make_num(2)), make_num(3));
        INFO(debug_string(e));
        REQUIRE(structurally_equal(e, make_pow(x(), make_num(6))));
    }
    SECTION("(x^2)^-1 -> x^-2 (printer round-trip shape)") {
        Expr e = make_pow(make_pow(x(), make_num(2)), make_num(-1));
        INFO(debug_string(e));
        REQUIRE(debug_string(e) == "(pow x -2)");
    }
    SECTION("(u^(1/2))^2 -> u (documented domain extension)") {
        Expr e = make_pow(make_pow(x(), make_num(Rational(1, 2))), make_num(2));
        INFO(debug_string(e));
        REQUIRE(structurally_equal(e, x()));
    }
    SECTION("rational inner exponent times integer outer exponent") {
        Expr e = make_pow(make_pow(x(), make_num(Rational(2, 3))), make_num(3));
        INFO(debug_string(e));
        REQUIRE(structurally_equal(e, make_pow(x(), make_num(2))));
    }
    SECTION("no fold when the outer exponent is not an integer Number") {
        Expr frac = make_pow(make_pow(x(), make_num(2)), make_num(Rational(1, 3)));
        REQUIRE(debug_string(frac) == "(pow (pow x 2) 1/3)");
        Expr sym = make_pow(make_pow(x(), make_num(2)), y());
        REQUIRE(debug_string(sym) == "(pow (pow x 2) y)");
    }
    SECTION("no fold when the inner exponent is not a Number") {
        Expr e = make_pow(make_pow(x(), y()), make_num(2));
        REQUIRE(debug_string(e) == "(pow (pow x y) 2)");
    }
}

TEST_CASE("expr: make_pow structural folds (number factor out of a Mul)", "[expr][pow][fold]") {
    SECTION("(2*x)^-1 -> (1/2)*x^-1 (printer round-trip shape)") {
        Expr e = make_pow(make_mul({make_num(2), x()}), make_num(-1));
        INFO(debug_string(e));
        REQUIRE(debug_string(e) == "(mul 1/2 (pow x -1))");
        REQUIRE(structurally_equal(
            e, make_mul({make_num(Rational(1, 2)), make_pow(x(), make_num(-1))})));
    }
    SECTION("(3*x*y)^2 -> 9*(x*y)^2") {
        Expr e = make_pow(make_mul({make_num(3), x(), y()}), make_num(2));
        INFO(debug_string(e));
        REQUIRE(structurally_equal(
            e, make_mul({make_num(9), make_pow(make_mul({x(), y()}), make_num(2))})));
    }
    SECTION("negative coefficient") {
        Expr e = make_pow(make_mul({make_num(-2), x()}), make_num(3));
        INFO(debug_string(e));
        REQUIRE(structurally_equal(
            e, make_mul({make_num(-8), make_pow(x(), make_num(3))})));
    }
    SECTION("no fold without a Number factor") {
        Expr e = make_pow(make_mul({x(), y()}), make_num(2));
        REQUIRE(debug_string(e) == "(pow (mul x y) 2)");
    }
    SECTION("no fold for non-integer or symbolic exponents") {
        Expr half = make_pow(make_mul({make_num(2), x()}), make_num(Rational(1, 2)));
        REQUIRE(debug_string(half) == "(pow (mul 2 x) 1/2)");
        Expr sym = make_pow(make_mul({make_num(2), x()}), y());
        REQUIRE(debug_string(sym) == "(pow (mul 2 x) y)");
    }
    SECTION("combines with the pow-of-pow fold through nesting") {
        // (2*x^2)^-1 -> 1/2 * x^-2
        Expr e = make_pow(make_mul({make_num(2), make_pow(x(), make_num(2))}), make_num(-1));
        INFO(debug_string(e));
        REQUIRE(debug_string(e) == "(mul 1/2 (pow x -2))");
    }
}

TEST_CASE("expr: numeric folding is order-independent (128-bit accumulation)",
          "[expr][add][mul][order]") {
    SECTION("make_add({LLONG_MAX, 1, -1}) folds to LLONG_MAX in every order") {
        std::vector<std::vector<long long>> orders = {
            {LLONG_MAX, 1, -1}, {LLONG_MAX, -1, 1}, {1, LLONG_MAX, -1},
            {1, -1, LLONG_MAX}, {-1, LLONG_MAX, 1}, {-1, 1, LLONG_MAX},
        };
        for (const auto& order : orders) {
            std::vector<Expr> terms;
            for (long long v : order) terms.push_back(make_num(v));
            Expr e = make_add(terms);
            INFO(debug_string(e));
            REQUIRE(structurally_equal(e, make_num(LLONG_MAX)));
        }
    }
    SECTION("make_mul({2^62, 4, 1/8}) folds to 2^61 in every order") {
        const Expr big = make_num(1LL << 62);
        const Expr four = make_num(4);
        const Expr eighth = make_num(Rational(1, 8));
        std::vector<std::vector<Expr>> orders = {
            {big, four, eighth}, {big, eighth, four}, {four, big, eighth},
            {four, eighth, big}, {eighth, big, four}, {eighth, four, big},
        };
        for (const auto& order : orders) {
            Expr e = make_mul(order);
            INFO(debug_string(e));
            REQUIRE(structurally_equal(e, make_num(1LL << 61)));
        }
    }
    SECTION("large intermediate rationals cancel without spurious overflow") {
        // max/2 + max/2 + x -> max + x regardless of order.
        Expr half_max = make_num(Rational(LLONG_MAX, 2));
        Expr e = make_add({half_max, x(), half_max});
        INFO(debug_string(e));
        REQUIRE(structurally_equal(e, make_add({make_num(LLONG_MAX), x()})));
    }
    SECTION("results past 64 bits fold exactly (arbitrary precision)") {
        REQUIRE(structurally_equal(make_add({make_num(LLONG_MAX), make_num(1)}),
                                   make_num(Rational(BigInt("9223372036854775808"))))); // 2^63
        REQUIRE(structurally_equal(make_mul({make_num(1LL << 62), make_num(4)}),
                                   make_num(Rational(BigInt("18446744073709551616"))))); // 2^64
    }
    SECTION("a zero factor still annihilates before any folding") {
        Expr e = make_mul({make_num(LLONG_MAX), make_num(LLONG_MAX), make_num(0)});
        REQUIRE(structurally_equal(e, make_num(0)));
    }
}

TEST_CASE("expr: make_fn does not auto-evaluate", "[expr][fn]") {
    Expr e = make_fn(FunctionId::Sin, make_num(0));
    REQUIRE(e->kind() == Kind::Function);
    REQUIRE(debug_string(e) == "(sin 0)");
    REQUIRE(debug_string(make_fn(FunctionId::Ln, make_num(1))) == "(ln 1)");
}

// ---------------------------------------------------------------------------
// Sugar factories and operators
// ---------------------------------------------------------------------------

TEST_CASE("expr: sugar factories reduce to the canonical node set", "[expr][sugar]") {
    REQUIRE(debug_string(make_neg(x())) == "(mul -1 x)");
    REQUIRE(debug_string(make_sub(x(), y())) == "(add x (mul -1 y))");
    REQUIRE(debug_string(make_div(x(), y())) == "(mul x (pow y -1))");
    REQUIRE(debug_string(make_sqrt(x())) == "(pow x 1/2)");
    REQUIRE(debug_string(make_exp(x())) == "(pow e x)");
    SECTION("sugar on numbers folds") {
        REQUIRE(structurally_equal(make_neg(make_num(5)), make_num(-5)));
        REQUIRE(structurally_equal(make_sub(make_num(5), make_num(7)), make_num(-2)));
        REQUIRE(structurally_equal(make_div(make_num(1), make_num(2)),
                                   make_num(Rational(1, 2))));
        REQUIRE(structurally_equal(make_sqrt(make_num(9)), make_num(3)));
        REQUIRE_THROWS_AS(make_div(x(), make_num(0)), DivisionByZeroError);
    }
    SECTION("convenience operators forward to the factories") {
        REQUIRE(debug_string(x() + y()) == "(add x y)");
        REQUIRE(debug_string(x() - y()) == "(add x (mul -1 y))");
        REQUIRE(debug_string(x() * y()) == "(mul x y)");
        REQUIRE(debug_string(x() / y()) == "(mul x (pow y -1))");
        REQUIRE(debug_string(-x()) == "(mul -1 x)");
        REQUIRE(structurally_equal(make_num(2) + make_num(3), make_num(5)));
        REQUIRE(structurally_equal(make_num(2) * make_num(3), make_num(6)));
    }
}

// ---------------------------------------------------------------------------
// compare_expr
// ---------------------------------------------------------------------------

TEST_CASE("expr: compare_expr kind ranking", "[expr][compare]") {
    // Number < Constant < Symbol < Pow < Function < Mul < Add
    std::vector<Expr> ranked = {
        make_num(100),
        make_const(ConstantId::Pi),
        make_sym("a"),
        make_pow(x(), make_num(2)),
        make_fn(FunctionId::Sin, x()),
        make_mul({x(), y()}),
        make_add({x(), y()}),
    };
    for (std::size_t i = 0; i < ranked.size(); ++i) {
        for (std::size_t j = 0; j < ranked.size(); ++j) {
            INFO(debug_string(ranked[i]) << " vs " << debug_string(ranked[j]));
            if (i < j) REQUIRE(compare_expr(ranked[i], ranked[j]) == -1);
            if (i > j) REQUIRE(compare_expr(ranked[i], ranked[j]) == 1);
            if (i == j) REQUIRE(compare_expr(ranked[i], ranked[j]) == 0);
        }
    }
}

TEST_CASE("expr: compare_expr tie-breaks", "[expr][compare]") {
    SECTION("numbers by rational value") {
        REQUIRE(compare_expr(make_num(Rational(1, 3)), make_num(Rational(1, 2))) == -1);
        REQUIRE(compare_expr(make_num(-5), make_num(0)) == -1);
        REQUIRE(compare_expr(make_num(Rational(2, 4)), make_num(Rational(1, 2))) == 0);
    }
    SECTION("constants by id") {
        REQUIRE(compare_expr(make_const(ConstantId::Pi), make_const(ConstantId::E)) == -1);
        REQUIRE(compare_expr(make_const(ConstantId::E), make_const(ConstantId::E)) == 0);
    }
    SECTION("symbols lexicographic") {
        REQUIRE(compare_expr(make_sym("a"), make_sym("b")) == -1);
        REQUIRE(compare_expr(make_sym("x"), make_sym("x")) == 0);
        REQUIRE(compare_expr(make_sym("x_1"), make_sym("x_2")) == -1);
        REQUIRE(compare_expr(make_sym("y"), make_sym("x")) == 1);
    }
    SECTION("functions by (id, arg)") {
        REQUIRE(compare_expr(make_fn(FunctionId::Sin, y()), make_fn(FunctionId::Cos, x())) ==
                -1); // Sin precedes Cos in the enum
        REQUIRE(compare_expr(make_fn(FunctionId::Sin, x()), make_fn(FunctionId::Sin, y())) ==
                -1);
        REQUIRE(compare_expr(make_fn(FunctionId::Sin, x()), make_fn(FunctionId::Sin, x())) ==
                0);
    }
    SECTION("pow by (base, exponent)") {
        REQUIRE(compare_expr(make_pow(x(), make_num(2)), make_pow(y(), make_num(2))) == -1);
        REQUIRE(compare_expr(make_pow(x(), make_num(2)), make_pow(x(), make_num(3))) == -1);
        REQUIRE(compare_expr(make_pow(x(), make_num(2)), make_pow(x(), make_num(2))) == 0);
    }
    SECTION("add/mul element-wise, shorter first on prefix ties") {
        Expr ab = make_add({make_sym("a"), make_sym("b")});
        Expr abc = make_add({make_sym("a"), make_sym("b"), make_sym("c")});
        Expr ac = make_add({make_sym("a"), make_sym("c")});
        REQUIRE(compare_expr(ab, abc) == -1);
        REQUIRE(compare_expr(abc, ab) == 1);
        REQUIRE(compare_expr(ab, ac) == -1);
        REQUIRE(compare_expr(abc, ac) == -1); // b < c decides before length
    }
    SECTION("antisymmetry over a corpus") {
        std::vector<Expr> corpus = {
            make_num(0), make_num(Rational(-3, 2)), make_const(ConstantId::E), x(), y(),
            make_pow(x(), y()), make_fn(FunctionId::Abs, x()), make_mul({x(), y()}),
            make_add({x(), make_num(1)}),
        };
        for (const auto& a : corpus) {
            for (const auto& b : corpus) {
                REQUIRE(compare_expr(a, b) == -compare_expr(b, a));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// structurally_equal / hash_expr
// ---------------------------------------------------------------------------

TEST_CASE("expr: structurally_equal and hash consistency", "[expr][hash]") {
    std::vector<Expr> corpus = {
        make_num(0),
        make_num(1),
        make_num(-1),
        make_num(Rational(1, 2)),
        make_num(Rational(-3, 7)),
        make_const(ConstantId::Pi),
        make_const(ConstantId::E),
        x(),
        y(),
        make_sym("x_1"),
        make_pow(x(), make_num(2)),
        make_pow(x(), make_num(3)),
        make_pow(y(), make_num(2)),
        make_pow(make_num(2), x()),
        make_fn(FunctionId::Sin, x()),
        make_fn(FunctionId::Cos, x()),
        make_fn(FunctionId::Sin, y()),
        make_fn(FunctionId::Ln, make_add({x(), make_num(1)})),
        make_mul({x(), y()}),
        make_mul({make_num(2), x()}),
        make_add({x(), y()}),
        make_add({x(), y(), z()}),
        make_add({make_mul({make_num(3), x()}), make_num(2)}),
        make_sqrt(x()),
        make_exp(x()),
    };
    SECTION("equality is reflexive and hash agrees on equals") {
        for (const auto& e : corpus) {
            REQUIRE(structurally_equal(e, e));
            REQUIRE(hash_expr(e) == hash_expr(e));
        }
    }
    SECTION("independently built duplicates are equal with equal hashes") {
        for (const auto& e : corpus) {
            // Rebuild through substitute with a name that does not occur.
            Expr copy = substitute(e, "no_such_symbol", make_num(99));
            INFO(debug_string(e));
            REQUIRE(structurally_equal(e, copy));
            REQUIRE(hash_expr(e) == hash_expr(copy));
        }
        Expr a = make_add({make_mul({make_num(3), x()}), make_num(2)});
        Expr b = make_add({make_num(2), make_mul({x(), make_num(3)})});
        REQUIRE(structurally_equal(a, b));
        REQUIRE(hash_expr(a) == hash_expr(b));
    }
    SECTION("distinct corpus members are not equal, and compare consistently") {
        for (std::size_t i = 0; i < corpus.size(); ++i) {
            for (std::size_t j = 0; j < corpus.size(); ++j) {
                const bool eq = structurally_equal(corpus[i], corpus[j]);
                INFO(debug_string(corpus[i]) << " vs " << debug_string(corpus[j]));
                REQUIRE(eq == (compare_expr(corpus[i], corpus[j]) == 0));
                if (i != j) REQUIRE_FALSE(eq);
                if (eq) REQUIRE(hash_expr(corpus[i]) == hash_expr(corpus[j]));
            }
        }
    }
    SECTION("hashes separate at least some structurally different pairs") {
        // Not a strict requirement for every pair, but the corpus should not
        // collapse to a single bucket.
        std::set<std::size_t> hashes;
        for (const auto& e : corpus) {
            hashes.insert(hash_expr(e));
        }
        REQUIRE(hashes.size() > corpus.size() / 2);
    }
}

// ---------------------------------------------------------------------------
// contains_symbol / free_symbols
// ---------------------------------------------------------------------------

TEST_CASE("expr: contains_symbol and free_symbols", "[expr][symbols]") {
    Expr e = make_add({make_mul({make_num(2), x(), make_fn(FunctionId::Sin, y())}),
                       make_pow(z(), make_num(2)), make_num(5)});
    REQUIRE(contains_symbol(e, "x"));
    REQUIRE(contains_symbol(e, "y"));
    REQUIRE(contains_symbol(e, "z"));
    REQUIRE_FALSE(contains_symbol(e, "w"));
    REQUIRE(free_symbols(e) == std::set<std::string>{"x", "y", "z"});
    REQUIRE(free_symbols(make_num(3)).empty());
    REQUIRE(free_symbols(make_const(ConstantId::Pi)).empty());
    REQUIRE(free_symbols(make_sym("alpha")) == std::set<std::string>{"alpha"});
    SECTION("name matching is exact") {
        REQUIRE_FALSE(contains_symbol(make_sym("x_1"), "x"));
        REQUIRE(contains_symbol(make_sym("x_1"), "x_1"));
    }
}

// ---------------------------------------------------------------------------
// substitute
// ---------------------------------------------------------------------------

TEST_CASE("expr: substitute rebuilds canonically", "[expr][substitute]") {
    SECTION("simple replacement") {
        Expr e = substitute(x(), "x", make_num(3));
        REQUIRE(structurally_equal(e, make_num(3)));
    }
    SECTION("non-matching symbols are untouched") {
        Expr e = make_add({x(), y()});
        Expr r = substitute(e, "q", make_num(0));
        REQUIRE(structurally_equal(r, e));
    }
    SECTION("numeric folding happens through the factories") {
        // x + 3x with x = 2 -> 2 + 6 -> 8
        Expr e = make_add({x(), make_mul({make_num(3), x()})});
        Expr r = substitute(e, "x", make_num(2));
        INFO(debug_string(r));
        REQUIRE(structurally_equal(r, make_num(8)));
    }
    SECTION("pow folds after substitution") {
        Expr e = make_pow(x(), make_num(2));
        REQUIRE(structurally_equal(substitute(e, "x", make_num(3)), make_num(9)));
        Expr sq = make_sqrt(x());
        REQUIRE(structurally_equal(substitute(sq, "x", make_num(4)), make_num(2)));
    }
    SECTION("mul with a zero substitution collapses") {
        Expr e = make_mul({x(), y(), make_num(7)});
        REQUIRE(structurally_equal(substitute(e, "y", make_num(0)), make_num(0)));
    }
    SECTION("substituting a symbol re-sorts") {
        Expr e = make_add({make_sym("b"), make_sym("d")});
        Expr r = substitute(e, "d", make_sym("a"));
        REQUIRE(debug_string(r) == "(add a b)");
    }
    SECTION("substitution flattens newly created nesting") {
        // (x + y) with x -> (a + b) must flatten.
        Expr e = make_add({x(), y()});
        Expr r = substitute(e, "x", make_add({make_sym("a"), make_sym("b")}));
        REQUIRE(debug_string(r) == "(add a b y)");
    }
    SECTION("replacement inside functions and pow") {
        Expr e = make_fn(FunctionId::Sin, make_pow(x(), make_num(2)));
        Expr r = substitute(e, "x", make_add({y(), make_num(1)}));
        REQUIRE(debug_string(r) == "(sin (pow (add 1 y) 2))");
    }
    SECTION("replacement can itself contain the symbol (single pass)") {
        Expr r = substitute(x(), "x", make_add({x(), make_num(1)}));
        REQUIRE(debug_string(r) == "(add 1 x)");
    }
    SECTION("substituting into a division can raise DivisionByZeroError") {
        Expr e = make_div(make_num(1), x()); // (mul 1 (pow x -1)) -> (pow x -1)
        REQUIRE_THROWS_AS(substitute(e, "x", make_num(0)), DivisionByZeroError);
    }
}

// ---------------------------------------------------------------------------
// debug_string
// ---------------------------------------------------------------------------

TEST_CASE("expr: debug_string exact format", "[expr][debug]") {
    REQUIRE(debug_string(make_num(2)) == "2");
    REQUIRE(debug_string(make_num(Rational(3, 2))) == "3/2");
    REQUIRE(debug_string(make_num(Rational(-3, 2))) == "-3/2");
    REQUIRE(debug_string(x()) == "x");
    REQUIRE(debug_string(make_const(ConstantId::Pi)) == "pi");
    REQUIRE(debug_string(make_const(ConstantId::E)) == "e");
    REQUIRE(debug_string(make_fn(FunctionId::Sin, x())) == "(sin x)");
    REQUIRE(debug_string(make_pow(x(), make_num(2))) == "(pow x 2)");
    REQUIRE(debug_string(make_add({make_num(2), make_mul({make_num(3), x()})})) ==
            "(add 2 (mul 3 x))");
    REQUIRE(debug_string(make_fn(FunctionId::Abs, make_add({x(), make_num(-1)}))) ==
            "(abs (add -1 x))");
}
