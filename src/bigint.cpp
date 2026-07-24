#include "mathsolver/bigint.hpp"

#include <algorithm>
#include <climits>
#include <cstddef>

namespace mathsolver {

namespace {
constexpr std::uint64_t BASE = 1ULL << 32; // limb base 2^32
} // namespace

// --- construction ----------------------------------------------------------

BigInt::BigInt(long long v) {
    neg_ = v < 0;
    unsigned long long m =
        neg_ ? (0ULL - static_cast<unsigned long long>(v)) : static_cast<unsigned long long>(v);
    while (m != 0) {
        mag_.push_back(static_cast<std::uint32_t>(m & 0xFFFFFFFFULL));
        m >>= 32;
    }
    if (mag_.empty()) neg_ = false;
}

BigInt::BigInt(std::string_view s) {
    std::size_t i = 0;
    bool negative = false;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) {
        negative = s[i] == '-';
        ++i;
    }
    if (i == s.size()) throw ParseError("malformed integer literal '" + std::string(s) + "'", 0, s.size());
    for (std::size_t k = i; k < s.size(); ++k) {
        if (s[k] < '0' || s[k] > '9')
            throw ParseError("malformed integer literal '" + std::string(s) + "'", 0, s.size());
    }
    // Fold in chunks of 9 decimal digits: value = value * 10^len + chunk.
    BigInt acc;
    std::size_t p = i;
    while (p < s.size()) {
        const std::size_t take = std::min<std::size_t>(9, s.size() - p);
        std::uint32_t chunk = 0;
        std::uint32_t scale = 1;
        for (std::size_t k = 0; k < take; ++k) {
            chunk = chunk * 10 + static_cast<std::uint32_t>(s[p + k] - '0');
            scale *= 10;
        }
        acc = acc * BigInt(static_cast<long long>(scale)) + BigInt(static_cast<long long>(chunk));
        p += take;
    }
    mag_ = std::move(acc.mag_);
    neg_ = !mag_.empty() && negative;
}

// --- helpers ---------------------------------------------------------------

void BigInt::trim() noexcept {
    while (!mag_.empty() && mag_.back() == 0) mag_.pop_back();
    if (mag_.empty()) neg_ = false;
}

int BigInt::cmp_mag(const std::vector<std::uint32_t>& a,
                    const std::vector<std::uint32_t>& b) noexcept {
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    for (std::size_t i = a.size(); i-- > 0;) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

std::vector<std::uint32_t> BigInt::add_mag(const std::vector<std::uint32_t>& a,
                                           const std::vector<std::uint32_t>& b) {
    const std::vector<std::uint32_t>& big = a.size() >= b.size() ? a : b;
    const std::vector<std::uint32_t>& small = a.size() >= b.size() ? b : a;
    std::vector<std::uint32_t> out;
    out.reserve(big.size() + 1);
    std::uint64_t carry = 0;
    for (std::size_t i = 0; i < big.size(); ++i) {
        std::uint64_t sum = carry + big[i] + (i < small.size() ? small[i] : 0);
        out.push_back(static_cast<std::uint32_t>(sum & 0xFFFFFFFFULL));
        carry = sum >> 32;
    }
    if (carry != 0) out.push_back(static_cast<std::uint32_t>(carry));
    return out;
}

std::vector<std::uint32_t> BigInt::sub_mag(const std::vector<std::uint32_t>& a,
                                           const std::vector<std::uint32_t>& b) {
    // Precondition: |a| >= |b|.
    std::vector<std::uint32_t> out;
    out.reserve(a.size());
    std::int64_t borrow = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        std::int64_t diff =
            static_cast<std::int64_t>(a[i]) - (i < b.size() ? static_cast<std::int64_t>(b[i]) : 0) - borrow;
        if (diff < 0) {
            diff += static_cast<std::int64_t>(BASE);
            borrow = 1;
        } else {
            borrow = 0;
        }
        out.push_back(static_cast<std::uint32_t>(diff));
    }
    while (!out.empty() && out.back() == 0) out.pop_back();
    return out;
}

std::vector<std::uint32_t> BigInt::mul_mag(const std::vector<std::uint32_t>& a,
                                           const std::vector<std::uint32_t>& b) {
    if (a.empty() || b.empty()) return {};
    std::vector<std::uint32_t> out(a.size() + b.size(), 0);
    for (std::size_t i = 0; i < a.size(); ++i) {
        std::uint64_t carry = 0;
        const std::uint64_t ai = a[i];
        for (std::size_t j = 0; j < b.size(); ++j) {
            std::uint64_t cur = out[i + j] + ai * b[j] + carry;
            out[i + j] = static_cast<std::uint32_t>(cur & 0xFFFFFFFFULL);
            carry = cur >> 32;
        }
        out[i + b.size()] += static_cast<std::uint32_t>(carry);
    }
    while (!out.empty() && out.back() == 0) out.pop_back();
    return out;
}

std::pair<std::vector<std::uint32_t>, std::vector<std::uint32_t>>
BigInt::divmod_mag(const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b) {
    // Precondition: b is nonzero (no trailing zero limbs).
    if (cmp_mag(a, b) < 0) return {{}, a};
    if (b.size() == 1) {
        // Single-limb divisor: linear long division.
        const std::uint64_t d = b[0];
        std::vector<std::uint32_t> q(a.size(), 0);
        std::uint64_t rem = 0;
        for (std::size_t i = a.size(); i-- > 0;) {
            const std::uint64_t cur = (rem << 32) | a[i];
            q[i] = static_cast<std::uint32_t>(cur / d);
            rem = cur % d;
        }
        while (!q.empty() && q.back() == 0) q.pop_back();
        std::vector<std::uint32_t> r;
        if (rem != 0) r.push_back(static_cast<std::uint32_t>(rem));
        return {q, r};
    }

    // Knuth Algorithm D (TAOCP 4.3.1), base 2^32.
    const int n = static_cast<int>(b.size());
    const int m = static_cast<int>(a.size()) - n;
    const int s = __builtin_clz(b.back()); // 0..31; b.back() != 0

    // Normalize so vn.back() has its top bit set.
    std::vector<std::uint32_t> vn(b.size());
    if (s == 0) {
        vn = b;
    } else {
        std::uint32_t carry = 0;
        for (std::size_t i = 0; i < b.size(); ++i) {
            const std::uint64_t cur = (static_cast<std::uint64_t>(b[i]) << s) | carry;
            vn[i] = static_cast<std::uint32_t>(cur & 0xFFFFFFFFULL);
            carry = static_cast<std::uint32_t>(cur >> 32);
        }
        // No new limb: b.back() had s leading zeros.
    }
    std::vector<std::uint32_t> un(a.size() + 1, 0);
    if (s == 0) {
        std::copy(a.begin(), a.end(), un.begin());
    } else {
        std::uint32_t carry = 0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            const std::uint64_t cur = (static_cast<std::uint64_t>(a[i]) << s) | carry;
            un[i] = static_cast<std::uint32_t>(cur & 0xFFFFFFFFULL);
            carry = static_cast<std::uint32_t>(cur >> 32);
        }
        un[a.size()] = carry;
    }

    std::vector<std::uint32_t> q(m + 1, 0);
    for (int j = m; j >= 0; --j) {
        const std::uint64_t top =
            (static_cast<std::uint64_t>(un[j + n]) << 32) | un[j + n - 1];
        std::uint64_t qhat = top / vn[n - 1];
        std::uint64_t rhat = top % vn[n - 1];
        while (qhat >= BASE ||
               qhat * vn[n - 2] > ((rhat << 32) | un[j + n - 2])) {
            --qhat;
            rhat += vn[n - 1];
            if (rhat >= BASE) break;
        }
        // Multiply and subtract.
        std::uint64_t carry = 0;
        std::int64_t borrow = 0;
        for (int i = 0; i < n; ++i) {
            const std::uint64_t p = qhat * vn[i] + carry;
            carry = p >> 32;
            const std::int64_t sub =
                static_cast<std::int64_t>(un[j + i]) - static_cast<std::int64_t>(p & 0xFFFFFFFFULL) - borrow;
            un[j + i] = static_cast<std::uint32_t>(sub);
            borrow = sub < 0 ? 1 : 0;
        }
        const std::int64_t sub =
            static_cast<std::int64_t>(un[j + n]) - static_cast<std::int64_t>(carry) - borrow;
        un[j + n] = static_cast<std::uint32_t>(sub);
        if (sub < 0) {
            // qhat was one too large: add the divisor back.
            --qhat;
            std::uint64_t c = 0;
            for (int i = 0; i < n; ++i) {
                const std::uint64_t t = static_cast<std::uint64_t>(un[j + i]) + vn[i] + c;
                un[j + i] = static_cast<std::uint32_t>(t & 0xFFFFFFFFULL);
                c = t >> 32;
            }
            un[j + n] = static_cast<std::uint32_t>(un[j + n] + c);
        }
        q[j] = static_cast<std::uint32_t>(qhat);
    }
    while (!q.empty() && q.back() == 0) q.pop_back();

    // Denormalize the remainder (low n limbs of un) by shifting right s bits.
    std::vector<std::uint32_t> r(un.begin(), un.begin() + n);
    if (s != 0) {
        std::uint32_t carry = 0;
        for (int i = n - 1; i >= 0; --i) {
            const std::uint32_t cur = r[i];
            r[i] = (cur >> s) | (carry << (32 - s));
            carry = cur & ((1u << s) - 1);
        }
    }
    while (!r.empty() && r.back() == 0) r.pop_back();
    return {q, r};
}

// --- unary / conversions ---------------------------------------------------

BigInt BigInt::abs() const {
    BigInt r = *this;
    r.neg_ = false;
    return r;
}

BigInt BigInt::operator-() const {
    BigInt r = *this;
    if (!r.mag_.empty()) r.neg_ = !r.neg_;
    return r;
}

bool BigInt::fits_ll() const noexcept {
    if (mag_.size() > 2) return false;
    std::uint64_t m = 0;
    for (std::size_t i = mag_.size(); i-- > 0;) m = (m << 32) | mag_[i];
    const std::uint64_t limit = neg_ ? (1ULL << 63) : ((1ULL << 63) - 1);
    return m <= limit;
}

long long BigInt::to_ll() const {
    if (!fits_ll()) throw OverflowError("value does not fit in a 64-bit integer");
    std::uint64_t m = 0;
    for (std::size_t i = mag_.size(); i-- > 0;) m = (m << 32) | mag_[i];
    if (neg_) {
        if (m == (1ULL << 63)) return LLONG_MIN;
        return -static_cast<long long>(m);
    }
    return static_cast<long long>(m);
}

double BigInt::to_double() const noexcept {
    double d = 0.0;
    for (std::size_t i = mag_.size(); i-- > 0;) {
        d = d * static_cast<double>(BASE) + static_cast<double>(mag_[i]);
    }
    return neg_ ? -d : d;
}

std::string BigInt::to_string() const {
    if (mag_.empty()) return "0";
    // Repeatedly divide the magnitude by 1e9, collecting 9-digit groups.
    std::vector<std::uint32_t> cur = mag_;
    std::string digits;
    const std::uint64_t chunk = 1000000000ULL;
    while (!cur.empty()) {
        std::uint64_t rem = 0;
        for (std::size_t i = cur.size(); i-- > 0;) {
            const std::uint64_t v = (rem << 32) | cur[i];
            cur[i] = static_cast<std::uint32_t>(v / chunk);
            rem = v % chunk;
        }
        while (!cur.empty() && cur.back() == 0) cur.pop_back();
        // Emit this group (zero-padded to 9 unless it is the most significant).
        for (int k = 0; k < 9; ++k) {
            digits.push_back(static_cast<char>('0' + rem % 10));
            rem /= 10;
        }
    }
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
    std::string out;
    if (neg_) out.push_back('-');
    out.append(digits.rbegin(), digits.rend());
    return out;
}

BigInt BigInt::pow(unsigned long long exponent) const {
    BigInt result(1);
    BigInt base = *this;
    while (exponent > 0) {
        if ((exponent & 1ULL) != 0) result *= base;
        exponent >>= 1;
        if (exponent > 0) base *= base;
    }
    return result;
}

// --- divmod / gcd ----------------------------------------------------------

std::pair<BigInt, BigInt> BigInt::divmod(const BigInt& a, const BigInt& b) {
    if (b.is_zero()) throw DivisionByZeroError("integer division by zero");
    auto [qm, rm] = divmod_mag(a.mag_, b.mag_);
    BigInt q;
    q.mag_ = std::move(qm);
    q.neg_ = a.neg_ != b.neg_;
    q.trim();
    BigInt r;
    r.mag_ = std::move(rm);
    r.neg_ = a.neg_; // remainder takes the dividend's sign (truncated division)
    r.trim();
    return {q, r};
}

BigInt BigInt::gcd(BigInt a, BigInt b) {
    a.neg_ = false;
    b.neg_ = false;
    while (!b.is_zero()) {
        BigInt r = divmod(a, b).second;
        a = std::move(b);
        b = std::move(r);
    }
    return a;
}

// --- arithmetic ------------------------------------------------------------

BigInt operator+(const BigInt& a, const BigInt& b) {
    BigInt out;
    if (a.neg_ == b.neg_) {
        out.mag_ = BigInt::add_mag(a.mag_, b.mag_);
        out.neg_ = a.neg_;
    } else {
        const int c = BigInt::cmp_mag(a.mag_, b.mag_);
        if (c == 0) return BigInt();
        if (c > 0) {
            out.mag_ = BigInt::sub_mag(a.mag_, b.mag_);
            out.neg_ = a.neg_;
        } else {
            out.mag_ = BigInt::sub_mag(b.mag_, a.mag_);
            out.neg_ = b.neg_;
        }
    }
    out.trim();
    return out;
}

BigInt operator-(const BigInt& a, const BigInt& b) { return a + (-b); }

BigInt operator*(const BigInt& a, const BigInt& b) {
    BigInt out;
    out.mag_ = BigInt::mul_mag(a.mag_, b.mag_);
    out.neg_ = a.neg_ != b.neg_;
    out.trim();
    return out;
}

BigInt operator/(const BigInt& a, const BigInt& b) { return BigInt::divmod(a, b).first; }
BigInt operator%(const BigInt& a, const BigInt& b) { return BigInt::divmod(a, b).second; }

BigInt& BigInt::operator+=(const BigInt& o) { return *this = *this + o; }
BigInt& BigInt::operator-=(const BigInt& o) { return *this = *this - o; }
BigInt& BigInt::operator*=(const BigInt& o) { return *this = *this * o; }
BigInt& BigInt::operator/=(const BigInt& o) { return *this = *this / o; }
BigInt& BigInt::operator%=(const BigInt& o) { return *this = *this % o; }

// --- comparison ------------------------------------------------------------

bool operator==(const BigInt& a, const BigInt& b) noexcept {
    return a.neg_ == b.neg_ && a.mag_ == b.mag_;
}

std::strong_ordering operator<=>(const BigInt& a, const BigInt& b) noexcept {
    if (a.neg_ != b.neg_) return a.neg_ ? std::strong_ordering::less : std::strong_ordering::greater;
    const int c = BigInt::cmp_mag(a.mag_, b.mag_);
    const int signed_c = a.neg_ ? -c : c; // both negative flips the magnitude order
    if (signed_c < 0) return std::strong_ordering::less;
    if (signed_c > 0) return std::strong_ordering::greater;
    return std::strong_ordering::equal;
}

std::size_t BigInt::hash() const noexcept {
    std::size_t h = neg_ ? 0x9E3779B97F4A7C15ULL : 0;
    for (std::uint32_t limb : mag_) {
        h ^= limb + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    }
    return h;
}

} // namespace mathsolver
