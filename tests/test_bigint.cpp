#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string>

#include "mathsolver/bigint.hpp"
#include "mathsolver/errors.hpp"

using mathsolver::BigInt;

namespace {

// A small deterministic PRNG (xorshift64) so the randomized sweep is
// reproducible across runs and platforms.
struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ULL) {}
    std::uint64_t next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
    // A value in [-2^62, 2^62) so products fit in __int128 for cross-checking.
    long long small() { return static_cast<long long>(next() >> 2) - (1LL << 61); }
};

std::string i128_to_string(__int128 v) {
    if (v == 0) return "0";
    const bool neg = v < 0;
    unsigned __int128 m = neg ? static_cast<unsigned __int128>(0) - static_cast<unsigned __int128>(v)
                              : static_cast<unsigned __int128>(v);
    std::string d;
    while (m != 0) {
        d.push_back(static_cast<char>('0' + static_cast<int>(m % 10)));
        m /= 10;
    }
    if (neg) d.push_back('-');
    return {d.rbegin(), d.rend()};
}

} // namespace

TEST_CASE("BigInt: construction and to_string round-trip") {
    CHECK(BigInt(0).to_string() == "0");
    CHECK(BigInt(1).to_string() == "1");
    CHECK(BigInt(-1).to_string() == "-1");
    CHECK(BigInt(123456789LL).to_string() == "123456789");
    CHECK(BigInt(-987654321LL).to_string() == "-987654321");
    // LLONG_MIN magnitude survives the sign flip.
    CHECK(BigInt(-9223372036854775807LL - 1).to_string() == "-9223372036854775808");
    CHECK(BigInt(9223372036854775807LL).to_string() == "9223372036854775807");
    // String parse round-trips, including a value far past 64 bits.
    const std::string big = "123456789012345678901234567890";
    CHECK(BigInt(big).to_string() == big);
    CHECK(BigInt("-" + big).to_string() == "-" + big);
    CHECK(BigInt("+42").to_string() == "42");
    CHECK(BigInt("0000123").to_string() == "123");
    CHECK(BigInt("-0").to_string() == "0"); // negative zero normalizes
}

TEST_CASE("BigInt: parse rejects malformed input") {
    CHECK_THROWS_AS(BigInt("12a3"), mathsolver::ParseError);
    CHECK_THROWS_AS(BigInt(""), mathsolver::ParseError);
    CHECK_THROWS_AS(BigInt("-"), mathsolver::ParseError);
    CHECK_THROWS_AS(BigInt("1.5"), mathsolver::ParseError);
}

TEST_CASE("BigInt: sign, zero, comparison predicates") {
    CHECK(BigInt(0).is_zero());
    CHECK(BigInt(1).is_one());
    CHECK(!BigInt(-1).is_one());
    CHECK(BigInt(-5).is_negative());
    CHECK(BigInt(0).sign() == 0);
    CHECK(BigInt(7).sign() == 1);
    CHECK(BigInt(-7).sign() == -1);
    CHECK(BigInt(3) < BigInt(5));
    CHECK(BigInt(-5) < BigInt(-3));
    CHECK(BigInt(-1) < BigInt(0));
    CHECK(BigInt(1000000000000LL) > BigInt(999999999999LL));
    CHECK(BigInt(5) == BigInt(5));
    CHECK(-BigInt(5) == BigInt(-5));
    CHECK(BigInt(5).abs() == BigInt(5));
    CHECK(BigInt(-5).abs() == BigInt(5));
}

TEST_CASE("BigInt: to_ll and fits_ll boundaries") {
    CHECK(BigInt(0).to_ll() == 0);
    CHECK(BigInt(9223372036854775807LL).to_ll() == 9223372036854775807LL);
    CHECK(BigInt(-9223372036854775807LL - 1).to_ll() == -9223372036854775807LL - 1);
    CHECK(BigInt(9223372036854775807LL).fits_ll());
    // One past the signed range does not fit.
    const BigInt over = BigInt(9223372036854775807LL) + BigInt(1);
    CHECK(!over.fits_ll());
    CHECK_THROWS_AS(over.to_ll(), mathsolver::OverflowError);
    const BigInt under = BigInt(-9223372036854775807LL - 1) - BigInt(1);
    CHECK(!under.fits_ll());
    CHECK_THROWS_AS(under.to_ll(), mathsolver::OverflowError);
}

TEST_CASE("BigInt: division and modulo by zero throw") {
    CHECK_THROWS_AS(BigInt(5) / BigInt(0), mathsolver::DivisionByZeroError);
    CHECK_THROWS_AS(BigInt(5) % BigInt(0), mathsolver::DivisionByZeroError);
}

TEST_CASE("BigInt: truncated division sign conventions") {
    // C++ semantics: quotient truncates toward zero; remainder takes the
    // dividend's sign.
    CHECK((BigInt(7) / BigInt(3)) == BigInt(2));
    CHECK((BigInt(7) % BigInt(3)) == BigInt(1));
    CHECK((BigInt(-7) / BigInt(3)) == BigInt(-2));
    CHECK((BigInt(-7) % BigInt(3)) == BigInt(-1));
    CHECK((BigInt(7) / BigInt(-3)) == BigInt(-2));
    CHECK((BigInt(7) % BigInt(-3)) == BigInt(1));
    CHECK((BigInt(-7) / BigInt(-3)) == BigInt(2));
    CHECK((BigInt(-7) % BigInt(-3)) == BigInt(-1));
    // Dividend smaller than divisor.
    CHECK((BigInt(2) / BigInt(5)) == BigInt(0));
    CHECK((BigInt(2) % BigInt(5)) == BigInt(2));
}

TEST_CASE("BigInt: arithmetic cross-checked against __int128 (exhaustive edges)") {
    // Values chosen to exercise limb boundaries (2^32, 2^64) and signs.
    const std::array<long long, 13> vals = {0LL,
                                            1LL,
                                            -1LL,
                                            2LL,
                                            -2LL,
                                            4294967295LL,       // 2^32 - 1
                                            4294967296LL,       // 2^32
                                            4294967297LL,       // 2^32 + 1
                                            -4294967296LL,      // -2^32
                                            1000000000LL,
                                            999999999999LL,
                                            123456789012LL,
                                            -98765432109LL};
    for (long long a : vals) {
        for (long long b : vals) {
            const BigInt ba(a);
            const BigInt bb(b);
            CHECK((ba + bb).to_string() == i128_to_string(static_cast<__int128>(a) + b));
            CHECK((ba - bb).to_string() == i128_to_string(static_cast<__int128>(a) - b));
            CHECK((ba * bb).to_string() == i128_to_string(static_cast<__int128>(a) * b));
            if (b != 0) {
                CHECK((ba / bb).to_string() == i128_to_string(static_cast<__int128>(a) / b));
                CHECK((ba % bb).to_string() == i128_to_string(static_cast<__int128>(a) % b));
            }
            const bool lt = ba < bb;
            CHECK(lt == (a < b));
            CHECK((ba == bb) == (a == b));
        }
    }
}

TEST_CASE("BigInt: randomized arithmetic vs __int128") {
    Rng rng(0xC0FFEEULL);
    for (int iter = 0; iter < 20000; ++iter) {
        const long long a = rng.small();
        const long long b = rng.small();
        const BigInt ba(a);
        const BigInt bb(b);
        REQUIRE((ba + bb).to_string() == i128_to_string(static_cast<__int128>(a) + b));
        REQUIRE((ba - bb).to_string() == i128_to_string(static_cast<__int128>(a) - b));
        REQUIRE((ba * bb).to_string() == i128_to_string(static_cast<__int128>(a) * b));
        if (b != 0) {
            REQUIRE((ba / bb).to_string() == i128_to_string(static_cast<__int128>(a) / b));
            REQUIRE((ba % bb).to_string() == i128_to_string(static_cast<__int128>(a) % b));
            // The division identity a == q*b + r must hold exactly.
            REQUIRE((ba / bb) * bb + (ba % bb) == ba);
        }
        REQUIRE((ba < bb) == (a < b));
    }
}

TEST_CASE("BigInt: multi-limb division identity (big / big)") {
    Rng rng(0x1234567ULL);
    for (int iter = 0; iter < 4000; ++iter) {
        // Build multi-limb operands by combining several small factors.
        BigInt a = BigInt(rng.small()) * BigInt(rng.small()) * BigInt(rng.small());
        BigInt b = BigInt(rng.small()) * BigInt(rng.small());
        if (b.is_zero()) continue;
        const BigInt q = a / b;
        const BigInt r = a % b;
        REQUIRE(q * b + r == a);
        REQUIRE(r.abs() < b.abs()); // |remainder| < |divisor|
    }
}

TEST_CASE("BigInt: pow and known big values") {
    CHECK(BigInt(2).pow(10).to_string() == "1024");
    CHECK(BigInt(2).pow(64).to_string() == "18446744073709551616");
    CHECK(BigInt(10).pow(0).to_string() == "1");
    CHECK(BigInt(10).pow(100).to_string() ==
          "1" + std::string(100, '0'));
    CHECK(BigInt(-3).pow(3).to_string() == "-27");
    CHECK(BigInt(-3).pow(4).to_string() == "81");

    // 50! computed by folding — a value far beyond int64.
    BigInt fact(1);
    for (long long k = 2; k <= 50; ++k) fact *= BigInt(k);
    CHECK(fact.to_string() ==
          "30414093201713378043612608166064768844377641568960512000000000000");
    // (50!)/(49!) recovers 50 exactly.
    BigInt fact49(1);
    for (long long k = 2; k <= 49; ++k) fact49 *= BigInt(k);
    CHECK((fact / fact49) == BigInt(50));
    CHECK((fact % fact49) == BigInt(0));
}

TEST_CASE("BigInt: gcd") {
    CHECK(BigInt::gcd(BigInt(0), BigInt(0)) == BigInt(0));
    CHECK(BigInt::gcd(BigInt(12), BigInt(0)) == BigInt(12));
    CHECK(BigInt::gcd(BigInt(1071), BigInt(462)) == BigInt(21));
    CHECK(BigInt::gcd(BigInt(-1071), BigInt(462)) == BigInt(21)); // always non-negative
    // gcd of two big values with a known common factor.
    const BigInt f = BigInt("1000000000000000000000000000057"); // (prime-ish factor)
    const BigInt a = f * BigInt(6);
    const BigInt b = f * BigInt(10);
    CHECK(BigInt::gcd(a, b) == f * BigInt(2));
}

TEST_CASE("BigInt: to_double") {
    CHECK(BigInt(0).to_double() == 0.0);
    CHECK(BigInt(1024).to_double() == 1024.0);
    CHECK(BigInt(-2048).to_double() == -2048.0);
    CHECK(BigInt(9007199254740992LL).to_double() == 9007199254740992.0); // 2^53
}
