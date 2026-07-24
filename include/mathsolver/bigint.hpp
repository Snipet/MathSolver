#pragma once

#include <cstdint>
#include <compare>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mathsolver/errors.hpp"

namespace mathsolver {

/// Arbitrary-precision signed integer.
///
/// Sign-magnitude: `neg_` is true only when the magnitude is nonzero; `mag_`
/// holds base-2^32 limbs little-endian with no trailing zero limbs (zero is the
/// empty magnitude). Arithmetic grows as needed and never throws OverflowError;
/// division and modulo by zero throw DivisionByZeroError.
class BigInt {
public:
    BigInt() = default;
    BigInt(long long v);                        // NOLINT: implicit by design
    explicit BigInt(std::string_view decimal);  // "-123", "42"; throws ParseError

    bool is_zero() const noexcept { return mag_.empty(); }
    bool is_negative() const noexcept { return neg_; }
    bool is_one() const noexcept { return !neg_ && mag_.size() == 1 && mag_[0] == 1; }
    /// -1, 0, or +1.
    int sign() const noexcept { return mag_.empty() ? 0 : (neg_ ? -1 : 1); }

    BigInt abs() const;
    BigInt operator-() const;

    /// Fits a signed 64-bit int (so to_ll() will not throw).
    bool fits_ll() const noexcept;
    /// Value as long long; throws OverflowError if out of [LLONG_MIN, LLONG_MAX].
    long long to_ll() const;
    /// Nearest double (may be +/-inf for values beyond double's range).
    double to_double() const noexcept;
    /// Decimal, with a leading '-' when negative ("0" for zero).
    std::string to_string() const;

    /// base^exponent (0^0 == 1). Grows without bound.
    BigInt pow(unsigned long long exponent) const;

    /// Truncated-toward-zero quotient and remainder: a == q*b + r, with r
    /// taking the sign of the dividend (C++ semantics) and |r| < |b|. Throws
    /// DivisionByZeroError when b == 0.
    static std::pair<BigInt, BigInt> divmod(const BigInt& a, const BigInt& b);

    /// Greatest common divisor, always non-negative; gcd(0, 0) == 0.
    static BigInt gcd(BigInt a, BigInt b);

    BigInt& operator+=(const BigInt& o);
    BigInt& operator-=(const BigInt& o);
    BigInt& operator*=(const BigInt& o);
    BigInt& operator/=(const BigInt& o);
    BigInt& operator%=(const BigInt& o);

    friend BigInt operator+(const BigInt& a, const BigInt& b);
    friend BigInt operator-(const BigInt& a, const BigInt& b);
    friend BigInt operator*(const BigInt& a, const BigInt& b);
    friend BigInt operator/(const BigInt& a, const BigInt& b);
    friend BigInt operator%(const BigInt& a, const BigInt& b);

    friend bool operator==(const BigInt& a, const BigInt& b) noexcept;
    friend std::strong_ordering operator<=>(const BigInt& a, const BigInt& b) noexcept;

    /// Hash over the sign + limbs (for Expr number-node hashing).
    std::size_t hash() const noexcept;

private:
    bool neg_ = false;
    std::vector<std::uint32_t> mag_;

    void trim() noexcept;              // drop trailing zero limbs; fix sign of zero
    static int cmp_mag(const std::vector<std::uint32_t>& a,
                       const std::vector<std::uint32_t>& b) noexcept;
    static std::vector<std::uint32_t> add_mag(const std::vector<std::uint32_t>& a,
                                              const std::vector<std::uint32_t>& b);
    // Requires |a| >= |b|.
    static std::vector<std::uint32_t> sub_mag(const std::vector<std::uint32_t>& a,
                                              const std::vector<std::uint32_t>& b);
    static std::vector<std::uint32_t> mul_mag(const std::vector<std::uint32_t>& a,
                                              const std::vector<std::uint32_t>& b);
    // Magnitude divmod: returns {quotient, remainder} of |a| / |b| (b != 0).
    static std::pair<std::vector<std::uint32_t>, std::vector<std::uint32_t>>
    divmod_mag(const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b);
};

} // namespace mathsolver

template <>
struct std::hash<mathsolver::BigInt> {
    std::size_t operator()(const mathsolver::BigInt& v) const noexcept { return v.hash(); }
};
