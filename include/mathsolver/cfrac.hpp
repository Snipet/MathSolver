#pragma once

#include <string>
#include <utility>
#include <vector>

namespace mathsolver {

/// A continued-fraction expansion [a0; a1, a2, …] together with its
/// convergents (the successive best rational approximations p/q).
struct CFrac {
    enum class Kind {
        Rational, ///< finite expansion of an exact rational
        Surd,     ///< periodic expansion of sqrt(n), n a non-square integer
        Numeric,  ///< truncated expansion of an irrational, via double
    };

    Kind kind = Kind::Rational;
    /// Partial quotients a0, a1, …. For a Surd these are a0 followed by one
    /// full period (which then repeats forever).
    std::vector<long long> terms;
    /// Index into `terms` where the repeating period begins, or -1 if none
    /// (Rational / Numeric). For a Surd this is always 1.
    int period_start = -1;
    /// Convergents p/q in order (reduced; each is the best rational
    /// approximation with denominator up to q). Truncated before any 64-bit
    /// overflow, so this may be shorter than `terms`.
    std::vector<std::pair<long long, long long>> convergents;
    /// True when the expansion is mathematically exact (Rational and Surd).
    bool exact = true;
};

/// Continued fraction of the exact rational p/q (q != 0). Finite; the last
/// convergent equals p/q.
CFrac cf_rational(long long p, long long q);

/// Continued fraction of sqrt(n) for n >= 1. A perfect square degenerates to
/// the single-term Rational [sqrt(n)]; otherwise the expansion is periodic.
CFrac cf_sqrt(long long n);

/// Continued fraction of an arbitrary real via double precision, to at most
/// `max_terms` partial quotients (stopping early once the remainder is
/// negligible or precision is exhausted). Never exact.
CFrac cf_numeric(double x, int max_terms = 20);

/// Render the partial quotients as "[a0; a1, a2, …]". A periodic Surd shows
/// its period once in parentheses, e.g. "[1; (2)]" for sqrt(2), "[2; (1, 4)]"
/// for sqrt(6).
std::string format_cfrac(const CFrac& cf);

/// Render the partial quotients as LaTeX, overlining the repeating period,
/// e.g. "[1; \\overline{2}]".
std::string format_cfrac_latex(const CFrac& cf);

} // namespace mathsolver
