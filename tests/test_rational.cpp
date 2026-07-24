#include <catch2/catch_test_macros.hpp>

#include <climits>
#include <compare>

#include "mathsolver/errors.hpp"
#include "mathsolver/rational.hpp"

using mathsolver::BigInt;
using mathsolver::DivisionByZeroError;
using mathsolver::OverflowError;
using mathsolver::ParseError;
using mathsolver::Rational;

TEST_CASE("rational: construction normalizes", "[rational]") {
    SECTION("default is zero") {
        Rational r;
        REQUIRE(r.num() == 0);
        REQUIRE(r.den() == 1);
        REQUIRE(r.is_zero());
    }
    SECTION("integer constructor") {
        Rational r(7);
        REQUIRE(r.num() == 7);
        REQUIRE(r.den() == 1);
        REQUIRE(r.is_integer());
    }
    SECTION("gcd reduction") {
        Rational r(6, 4);
        REQUIRE(r.num() == 3);
        REQUIRE(r.den() == 2);
    }
    SECTION("sign lives in the numerator") {
        Rational r(3, -6);
        REQUIRE(r.num() == -1);
        REQUIRE(r.den() == 2);
        REQUIRE(r.is_negative());
    }
    SECTION("negative over negative is positive") {
        Rational r(-4, -6);
        REQUIRE(r.num() == 2);
        REQUIRE(r.den() == 3);
        REQUIRE_FALSE(r.is_negative());
    }
    SECTION("zero is always 0/1") {
        Rational r(0, -17);
        REQUIRE(r.num() == 0);
        REQUIRE(r.den() == 1);
    }
    SECTION("zero denominator throws") {
        REQUIRE_THROWS_AS(Rational(1, 0), DivisionByZeroError);
        REQUIRE_THROWS_AS(Rational(0, 0), DivisionByZeroError);
    }
    SECTION("extreme values") {
        Rational min_ok(LLONG_MIN, 1);
        REQUIRE(min_ok.num() == LLONG_MIN);
        REQUIRE(min_ok.den() == 1);
        Rational reduced(LLONG_MIN, LLONG_MIN);
        REQUIRE(reduced.num() == 1);
        REQUIRE(reduced.den() == 1);
        Rational half_min(LLONG_MIN, 2);
        REQUIRE(half_min.num() == LLONG_MIN / 2);
        REQUIRE(half_min.den() == 1);
        // LLONG_MIN / -1 = 2^63 is now exact (arbitrary precision).
        REQUIRE(Rational(LLONG_MIN, -1).num() == BigInt("9223372036854775808"));
        // 1 / LLONG_MIN = -1 / 2^63.
        REQUIRE(Rational(1, LLONG_MIN).num() == BigInt(-1));
        REQUIRE(Rational(1, LLONG_MIN).den() == BigInt("9223372036854775808"));
    }
}

TEST_CASE("rational: predicates", "[rational]") {
    REQUIRE(Rational(0).is_zero());
    REQUIRE_FALSE(Rational(1, 100).is_zero());
    REQUIRE(Rational(1).is_one());
    REQUIRE(Rational(5, 5).is_one());
    REQUIRE_FALSE(Rational(-1).is_one());
    REQUIRE(Rational(8, 4).is_integer());
    REQUIRE_FALSE(Rational(1, 2).is_integer());
    REQUIRE(Rational(-1, 2).is_negative());
    REQUIRE_FALSE(Rational(0).is_negative());
}

TEST_CASE("rational: arithmetic", "[rational]") {
    SECTION("addition") {
        REQUIRE(Rational(1, 2) + Rational(1, 3) == Rational(5, 6));
        REQUIRE(Rational(1, 2) + Rational(1, 2) == Rational(1));
        REQUIRE(Rational(1, 2) + Rational(-1, 2) == Rational(0));
    }
    SECTION("subtraction") {
        REQUIRE(Rational(1, 2) - Rational(1, 3) == Rational(1, 6));
        REQUIRE(Rational(1, 3) - Rational(1, 2) == Rational(-1, 6));
        REQUIRE(Rational(5) - Rational(5) == Rational(0));
    }
    SECTION("multiplication") {
        REQUIRE(Rational(2, 3) * Rational(3, 4) == Rational(1, 2));
        REQUIRE(Rational(-2, 3) * Rational(3, 2) == Rational(-1));
        REQUIRE(Rational(0) * Rational(7, 9) == Rational(0));
    }
    SECTION("division") {
        REQUIRE(Rational(1, 2) / Rational(1, 4) == Rational(2));
        REQUIRE(Rational(-3) / Rational(6) == Rational(-1, 2));
    }
    SECTION("division by zero throws") {
        REQUIRE_THROWS_AS(Rational(1) / Rational(0), DivisionByZeroError);
        REQUIRE_THROWS_AS(Rational(0) / Rational(0), DivisionByZeroError);
    }
    SECTION("unary minus") {
        REQUIRE(-Rational(3, 4) == Rational(-3, 4));
        REQUIRE(-Rational(-3, 4) == Rational(3, 4));
        REQUIRE(-Rational(0) == Rational(0));
        // -LLONG_MIN = 2^63 is now exact (arbitrary precision).
        REQUIRE((-Rational(LLONG_MIN)).num() == BigInt("9223372036854775808"));
    }
}

TEST_CASE("rational: arithmetic is exact beyond 64 bits", "[rational]") {
    const Rational max_val(LLONG_MAX);
    // Values that used to overflow are now computed exactly (arbitrary precision).
    REQUIRE((max_val + Rational(1)).num() == BigInt("9223372036854775808"));       // 2^63
    REQUIRE((max_val * Rational(2)).num() == BigInt("18446744073709551614"));      // 2^64-2
    REQUIRE((Rational(LLONG_MIN) - Rational(1)).num() == BigInt("-9223372036854775809"));
    REQUIRE((Rational(LLONG_MIN) * Rational(-1)).num() == BigInt("9223372036854775808"));
    // Numerator * numerator far past 64 bits.
    const Rational prod = Rational(LLONG_MAX, 2) * Rational(LLONG_MAX, 3);
    REQUIRE(prod.num() == BigInt("85070591730234615847396907784232501249"));
    REQUIRE(prod.den() == BigInt(6));
    // An addition whose lcm blows up past 64 bits.
    const Rational sum = Rational(1, LLONG_MAX) + Rational(1, LLONG_MAX - 1);
    REQUIRE(sum.num() == BigInt("18446744073709551613"));
    REQUIRE(sum.den() == BigInt("85070591730234615838173535747377725442"));
}

TEST_CASE("rational: gcd reduction avoids spurious overflow", "[rational]") {
    // Same denominator: no lcm blow-up.
    REQUIRE(Rational(1, LLONG_MAX) + Rational(2, LLONG_MAX) == Rational(3, LLONG_MAX));
    // Cross-cancellation in multiplication.
    REQUIRE(Rational(3, 7) * Rational(7, 3) == Rational(1));
    REQUIRE(Rational(1, 3) * Rational(3, LLONG_MAX) == Rational(1, LLONG_MAX));
    REQUIRE(Rational(LLONG_MAX, 2) * Rational(2, LLONG_MAX) == Rational(1));
}

TEST_CASE("rational: add/sub combine in 128 bits, representable results never throw",
          "[rational][overflow]") {
    // The numerator combination exceeds 64 bits before the final gcd
    // reduction, but the reduced result fits exactly.
    REQUIRE(Rational(LLONG_MAX, 2) + Rational(LLONG_MAX, 2) == Rational(LLONG_MAX));
    REQUIRE(Rational(LLONG_MIN, 2) + Rational(LLONG_MIN, 2) == Rational(LLONG_MIN));
    REQUIRE(Rational(-1, 2) - Rational(LLONG_MAX, 2) == Rational(LLONG_MIN / 2));
    REQUIRE(Rational(LLONG_MAX, 2) - Rational(LLONG_MAX, 2) == Rational(0));
    REQUIRE(Rational(LLONG_MAX, 2) + Rational(1, 2) == Rational(LLONG_MAX / 2 + 1));
    // Results past 64 bits are now exact.
    REQUIRE((Rational(LLONG_MAX, 2) + Rational(LLONG_MAX, 2) + Rational(1)).num() ==
            BigInt("9223372036854775808")); // 2^63
    // (2^63-1)/2 - (-2^63)/2 = (2^64-1)/2, now exact.
    const Rational d = Rational(LLONG_MAX, 2) - Rational(LLONG_MIN, 2);
    REQUIRE(d.num() == BigInt("18446744073709551615"));
    REQUIRE(d.den() == BigInt(2));
}

TEST_CASE("rational: division cross-cancels before assembling the quotient",
          "[rational][overflow]") {
    // The old implementation built the unreduced reciprocal of the divisor
    // first, so any divisor with numerator LLONG_MIN threw spuriously.
    REQUIRE(Rational(2) / Rational(LLONG_MIN) == Rational(-1, 1LL << 62));
    REQUIRE(Rational(LLONG_MIN) / Rational(LLONG_MIN) == Rational(1));
    REQUIRE(Rational(LLONG_MIN) / Rational(2) == Rational(LLONG_MIN / 2));
    REQUIRE(Rational(LLONG_MIN) / Rational(1) == Rational(LLONG_MIN));
    REQUIRE(Rational(LLONG_MIN, 3) / Rational(2, 3) == Rational(LLONG_MIN / 2));
    REQUIRE(Rational(0) / Rational(LLONG_MIN) == Rational(0));
    REQUIRE(Rational(4, LLONG_MAX) / Rational(2, LLONG_MAX) == Rational(2));
    // Results past 64 bits are now exact.
    REQUIRE((Rational(1) / Rational(LLONG_MIN)).den() == BigInt("9223372036854775808"));
    REQUIRE((Rational(LLONG_MIN) / Rational(-1)).num() == BigInt("9223372036854775808"));
    // (2^63-1) / (1/(2^63-1)) = (2^63-1)^2, now exact.
    REQUIRE((Rational(LLONG_MAX) / Rational(1, LLONG_MAX)).num() ==
            BigInt("85070591730234615847396907784232501249"));
}

TEST_CASE("rational: comparison", "[rational]") {
    REQUIRE(Rational(1, 3) < Rational(1, 2));
    REQUIRE(Rational(-1, 2) < Rational(-1, 3));
    REQUIRE(Rational(2, 4) == Rational(1, 2));
    REQUIRE(Rational(7, 3) > Rational(2));
    REQUIRE(Rational(-5) < Rational(0));
    REQUIRE(Rational(0) <= Rational(0));

    SECTION("comparison never overflows (128-bit cross multiply)") {
        REQUIRE(Rational(LLONG_MAX, 2) > Rational(LLONG_MAX - 1, 2));
        REQUIRE(Rational(LLONG_MAX, 3) < Rational(LLONG_MAX, 2));
        REQUIRE(Rational(LLONG_MIN, 3) < Rational(LLONG_MAX, 3));
        REQUIRE(Rational(LLONG_MAX - 1, LLONG_MAX) < Rational(1));
        REQUIRE((Rational(LLONG_MIN, 7) <=> Rational(LLONG_MIN, 7)) ==
                std::strong_ordering::equal);
    }
}

TEST_CASE("rational: pow", "[rational]") {
    REQUIRE(Rational(2).pow(10) == Rational(1024));
    REQUIRE(Rational(2, 3).pow(3) == Rational(8, 27));
    REQUIRE(Rational(-2).pow(3) == Rational(-8));
    REQUIRE(Rational(-2).pow(2) == Rational(4));

    SECTION("exponent zero gives one") {
        REQUIRE(Rational(7, 5).pow(0) == Rational(1));
        REQUIRE(Rational(0).pow(0) == Rational(1));
    }
    SECTION("negative exponents invert") {
        REQUIRE(Rational(2).pow(-1) == Rational(1, 2));
        REQUIRE(Rational(2, 3).pow(-2) == Rational(9, 4));
        REQUIRE(Rational(-2, 3).pow(-3) == Rational(-27, 8));
    }
    SECTION("zero base") {
        REQUIRE(Rational(0).pow(5) == Rational(0));
        REQUIRE_THROWS_AS(Rational(0).pow(-1), DivisionByZeroError);
        REQUIRE_THROWS_AS(Rational(0).pow(-7), DivisionByZeroError);
    }
    SECTION("large powers are exact") {
        REQUIRE(Rational(2).pow(63).num() == BigInt("9223372036854775808"));   // 2^63
        REQUIRE(Rational(10).pow(19).num() == BigInt("10000000000000000000")); // 10^19
        REQUIRE(Rational(1, 2).pow(-63).num() == BigInt("9223372036854775808"));
        REQUIRE(Rational(2).pow(62) == Rational(1LL << 62));
    }
    SECTION("one and minus one never overflow") {
        REQUIRE(Rational(1).pow(1'000'000'000) == Rational(1));
        REQUIRE(Rational(-1).pow(1'000'000'001) == Rational(-1));
        REQUIRE(Rational(-1).pow(-1'000'000'000) == Rational(1));
    }
}

TEST_CASE("rational: from_decimal_string parses exactly", "[rational]") {
    REQUIRE(Rational::from_decimal_string("3.14") == Rational(157, 50));
    REQUIRE(Rational::from_decimal_string("42") == Rational(42));
    REQUIRE(Rational::from_decimal_string("-42") == Rational(-42));
    REQUIRE(Rational::from_decimal_string("0") == Rational(0));
    REQUIRE(Rational::from_decimal_string("-0.5") == Rational(-1, 2));
    REQUIRE(Rational::from_decimal_string("0.1") == Rational(1, 10));
    REQUIRE(Rational::from_decimal_string("0.125") == Rational(1, 8));
    REQUIRE(Rational::from_decimal_string("007.500") == Rational(15, 2));
    REQUIRE(Rational::from_decimal_string("-0.0") == Rational(0));
    // Trailing fractional zeros must not overflow.
    REQUIRE(Rational::from_decimal_string("0.50000000000000000000000000") == Rational(1, 2));
    REQUIRE(Rational::from_decimal_string("9223372036854775807") == Rational(LLONG_MAX));
    REQUIRE(Rational::from_decimal_string("-9223372036854775808") == Rational(LLONG_MIN));
}

TEST_CASE("rational: from_decimal_string rejects malformed input", "[rational]") {
    for (const char* bad : {"", "-", ".", "3.", ".5", "-.5", "3.1.4", "1e5", "2e-3", " 3",
                            "3 ", "+3", "--3", "3-", "a", "12a", "0x10", "1,5"}) {
        INFO("input: '" << bad << "'");
        REQUIRE_THROWS_AS(Rational::from_decimal_string(bad), ParseError);
    }
    SECTION("ParseError spans the whole string") {
        try {
            Rational::from_decimal_string("3.1.4");
            FAIL("expected ParseError");
        } catch (const ParseError& e) {
            REQUIRE(e.begin() == 0);
            REQUIRE(e.end() == 5);
        }
    }
}

TEST_CASE("rational: from_decimal_string is exact beyond 64 bits", "[rational]") {
    REQUIRE(Rational::from_decimal_string("99999999999999999999").num() ==
            BigInt("99999999999999999999"));
    REQUIRE(Rational::from_decimal_string("9223372036854775808").num() ==
            BigInt("9223372036854775808")); // 2^63
    REQUIRE(Rational::from_decimal_string("-9223372036854775809").num() ==
            BigInt("-9223372036854775809"));
    // Denominator 10^19 is now exact.
    const Rational tiny = Rational::from_decimal_string("0.0000000000000000001");
    REQUIRE(tiny.num() == BigInt(1));
    REQUIRE(tiny.den() == BigInt("10000000000000000000")); // 10^19
}

TEST_CASE("rational: to_string", "[rational]") {
    REQUIRE(Rational(5).to_string() == "5");
    REQUIRE(Rational(-7).to_string() == "-7");
    REQUIRE(Rational(3, 2).to_string() == "3/2");
    REQUIRE(Rational(-3, 2).to_string() == "-3/2");
    REQUIRE(Rational(0).to_string() == "0");
    REQUIRE(Rational(4, 2).to_string() == "2");
}

TEST_CASE("rational: to_double", "[rational]") {
    REQUIRE(Rational(1, 2).to_double() == 0.5);
    REQUIRE(Rational(-3, 4).to_double() == -0.75);
    REQUIRE(Rational(0).to_double() == 0.0);
    REQUIRE(Rational(1, 3).to_double() > 0.333333);
    REQUIRE(Rational(1, 3).to_double() < 0.333334);
}
