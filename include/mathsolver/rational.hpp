#pragma once

#include <compare>
#include <string>
#include <string_view>

#include "mathsolver/bigint.hpp"
#include "mathsolver/errors.hpp"

namespace mathsolver {

/// Exact rational number over arbitrary-precision integers (BigInt).
///
/// Invariants: den() > 0, gcd(|num()|, den()) == 1, zero is 0/1. Arithmetic is
/// exact and never overflows (the magnitudes grow as needed); construction with
/// a zero denominator and division by zero throw DivisionByZeroError.
class Rational {
public:
    Rational() : num_(0), den_(1) {}
    Rational(long long n) : num_(n), den_(1) {}       // NOLINT: implicit by design
    Rational(BigInt n) : num_(std::move(n)), den_(1) {} // NOLINT: implicit by design
    Rational(BigInt n, BigInt d);                      // normalizes; throws on d == 0
    Rational(long long n, long long d) : Rational(BigInt(n), BigInt(d)) {}

    /// Parse a decimal literal exactly: "3.14" -> 157/50, "42" -> 42.
    /// Accepts optional leading '-', digits, optional '.' + digits.
    /// Throws ParseError (span covering the whole string) on malformed input.
    static Rational from_decimal_string(std::string_view s);

    const BigInt& num() const noexcept { return num_; }
    const BigInt& den() const noexcept { return den_; }

    bool is_zero() const noexcept { return num_.is_zero(); }
    bool is_one() const noexcept { return num_.is_one() && den_.is_one(); }
    bool is_integer() const noexcept { return den_.is_one(); }
    bool is_negative() const noexcept { return num_.is_negative(); }

    double to_double() const noexcept;

    /// "5", "-7", "3/2", "-3/2".
    std::string to_string() const;

    /// Integer exponent power; negative exponents invert (0^negative throws
    /// DivisionByZeroError).
    Rational pow(long long exponent) const;

    Rational operator-() const;
    friend Rational operator+(const Rational& a, const Rational& b);
    friend Rational operator-(const Rational& a, const Rational& b);
    friend Rational operator*(const Rational& a, const Rational& b);
    friend Rational operator/(const Rational& a, const Rational& b);

    friend bool operator==(const Rational& a, const Rational& b) noexcept = default;
    friend std::strong_ordering operator<=>(const Rational& a, const Rational& b) noexcept;

private:
    BigInt num_;
    BigInt den_;
};

} // namespace mathsolver
