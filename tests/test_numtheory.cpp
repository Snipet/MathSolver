// Integer number-theory tests: gcd/lcm, deterministic primality, factorization
// (trial division + Pollard's rho), divisors, Euler's totient, and Euclidean
// mod — checked against closed forms and known values across the int64 range.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <climits>
#include <string>
#include <vector>

#include "mathsolver/errors.hpp"
#include "mathsolver/numtheory.hpp"

using namespace mathsolver;
using Catch::Matchers::ContainsSubstring;

namespace {

// Reconstruct |n| from its factorization, and confirm every base is prime and
// listed once in ascending order.
long long product_of(const std::vector<PrimePower>& f) {
    long long p = 1;
    long long last = 0;
    for (const PrimePower& pp : f) {
        CHECK(pp.prime > last); // strictly ascending, deduplicated
        CHECK(pp.exponent >= 1);
        CHECK(is_prime(pp.prime));
        last = pp.prime;
        for (int e = 0; e < pp.exponent; ++e) p *= pp.prime;
    }
    return p;
}

} // namespace

TEST_CASE("gcd and lcm over the integers") {
    CHECK(int_gcd(48, 36) == 12);
    CHECK(int_gcd(1071, 462) == 21);
    CHECK(int_gcd(-48, 36) == 12); // magnitude only
    CHECK(int_gcd(17, 0) == 17);
    CHECK(int_gcd(0, 0) == 0);
    CHECK(int_gcd(13, 7) == 1); // coprime

    CHECK(int_lcm(4, 6) == 12);
    CHECK(int_lcm(21, 6) == 42);
    CHECK(int_lcm(-4, 6) == 12);
    CHECK(int_lcm(5, 0) == 0);
    // gcd * lcm == |a * b|.
    CHECK(int_gcd(84, 30) * int_lcm(84, 30) == 84 * 30);
    // Overflow is reported, not wrapped.
    CHECK_THROWS_AS(int_lcm(3037000500LL, 3037000501LL), OverflowError);
}

TEST_CASE("deterministic primality across the range") {
    CHECK_FALSE(is_prime(0));
    CHECK_FALSE(is_prime(1));
    CHECK(is_prime(2));
    CHECK(is_prime(3));
    CHECK_FALSE(is_prime(4));
    CHECK(is_prime(97));
    CHECK_FALSE(is_prime(91)); // 7 * 13
    CHECK_FALSE(is_prime(-7)); // negatives are not prime
    // Carmichael numbers (fool naive Fermat tests) are correctly composite.
    CHECK_FALSE(is_prime(561));
    CHECK_FALSE(is_prime(41041));
    // Large primes and their neighbours.
    CHECK(is_prime(1000003));
    CHECK_FALSE(is_prime(1000005));
    CHECK(is_prime(2147483647));          // 2^31 - 1 (Mersenne)
    CHECK(is_prime(9223372036854775783LL)); // largest prime < 2^63
    CHECK_FALSE(is_prime(9223372036854775807LL)); // 2^63 - 1 is composite
}

TEST_CASE("prime factorization reconstructs the input") {
    CHECK(factorize(1).empty());
    CHECK(factorize(-1).empty());

    auto f360 = factorize(360);
    CHECK(product_of(f360) == 360);
    REQUIRE(f360.size() == 3);
    CHECK(f360[0].prime == 2);
    CHECK(f360[0].exponent == 3);
    CHECK(f360[1].prime == 3);
    CHECK(f360[1].exponent == 2);
    CHECK(f360[2].prime == 5);
    CHECK(f360[2].exponent == 1);

    // A prime factors to itself^1; negatives use the magnitude.
    auto f97 = factorize(97);
    REQUIRE(f97.size() == 1);
    CHECK(f97[0].prime == 97);
    CHECK(f97[0].exponent == 1);
    CHECK(product_of(factorize(-84)) == 84);

    // A large semiprime exercises Pollard's rho (beyond trial division).
    const long long semiprime = 1000003LL * 1000033LL;
    auto fs = factorize(semiprime);
    CHECK(product_of(fs) == semiprime);
    REQUIRE(fs.size() == 2);
    CHECK(fs[0].prime == 1000003);
    CHECK(fs[1].prime == 1000033);

    // A perfect prime power.
    auto f1024 = factorize(1024);
    REQUIRE(f1024.size() == 1);
    CHECK(f1024[0].prime == 2);
    CHECK(f1024[0].exponent == 10);
}

TEST_CASE("format_factorization renders exponents and separators") {
    CHECK(format_factorization(factorize(360), " * ") == "2^3 * 3^2 * 5");
    CHECK(format_factorization(factorize(97), " * ") == "97");
    CHECK(format_factorization({}, " * ") == "1");
    CHECK(format_factorization(factorize(12), " · ") == "2^2 · 3");
}

TEST_CASE("divisors are complete, sorted, and multiplicative in count") {
    CHECK(divisors(1) == std::vector<long long>{1});
    CHECK(divisors(28) == std::vector<long long>{1, 2, 4, 7, 14, 28}); // perfect number
    CHECK(divisors(-12) == std::vector<long long>{1, 2, 3, 4, 6, 12});
    CHECK(divisors(97) == std::vector<long long>{1, 97});
    // d(360) = (3+1)(2+1)(1+1) = 24.
    CHECK(divisors(360).size() == 24);
}

TEST_CASE("Euler's totient") {
    CHECK(euler_totient(1) == 1);
    CHECK(euler_totient(9) == 6);   // 1,2,4,5,7,8
    CHECK(euler_totient(10) == 4);  // 1,3,7,9
    CHECK(euler_totient(97) == 96); // prime p → p-1
    CHECK(euler_totient(36) == 12);
    // Sum of totients over divisors of n equals n (Gauss).
    long long s = 0;
    for (long long d : divisors(36)) s += euler_totient(d);
    CHECK(s == 36);
    CHECK_THROWS_AS(euler_totient(0), EvalError);
}

TEST_CASE("next/prev prime and Euclidean mod") {
    CHECK(next_prime(0) == 2);
    CHECK(next_prime(7) == 11);
    CHECK(next_prime(100) == 101);
    CHECK(prev_prime(100) == 97);
    CHECK(prev_prime(2) == 0); // none below 2

    CHECK(int_mod(17, 5) == 2);
    CHECK(int_mod(-1, 5) == 4);   // result stays in [0, |m|)
    CHECK(int_mod(-7, 3) == 2);
    CHECK(int_mod(7, -3) == 1);   // sign of m does not affect the residue range
    CHECK_THROWS_AS(int_mod(1, 0), DivisionByZeroError);
}
