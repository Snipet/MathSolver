#pragma once

#include <compare>
#include <string>
#include <string_view>

#include "mathsolver/errors.hpp"

namespace mathsolver {

/// Exact rational number over long long.
///
/// Invariants: den() > 0, gcd(|num()|, den()) == 1, zero is 0/1.
/// Every arithmetic operation is overflow-checked and throws OverflowError
/// rather than wrapping; construction with a zero denominator and division
/// by zero throw DivisionByZeroError.
class Rational {
public:
    Rational() : num_(0), den_(1) {}
    Rational(long long n) : num_(n), den_(1) {} // NOLINT: implicit by design
    Rational(long long n, long long d);          // normalizes; throws on d == 0

    /// Parse a decimal literal exactly: "3.14" -> 157/50, "42" -> 42.
    /// Accepts optional leading '-', digits, optional '.' + digits.
    /// Throws ParseError (span covering the whole string) on malformed input,
    /// OverflowError if the value does not fit.
    static Rational from_decimal_string(std::string_view s);

    long long num() const noexcept { return num_; }
    long long den() const noexcept { return den_; }

    bool is_zero() const noexcept { return num_ == 0; }
    bool is_one() const noexcept { return num_ == 1 && den_ == 1; }
    bool is_integer() const noexcept { return den_ == 1; }
    bool is_negative() const noexcept { return num_ < 0; }

    double to_double() const noexcept;

    /// "5", "-7", "3/2", "-3/2".
    std::string to_string() const;

    /// Integer exponent power; negative exponents invert (0^negative throws
    /// DivisionByZeroError). Overflow-checked.
    Rational pow(long long exponent) const;

    Rational operator-() const;
    friend Rational operator+(const Rational& a, const Rational& b);
    friend Rational operator-(const Rational& a, const Rational& b);
    friend Rational operator*(const Rational& a, const Rational& b);
    friend Rational operator/(const Rational& a, const Rational& b);

    friend bool operator==(const Rational& a, const Rational& b) noexcept = default;
    friend std::strong_ordering operator<=>(const Rational& a, const Rational& b) noexcept;

private:
    long long num_;
    long long den_;
};

} // namespace mathsolver
