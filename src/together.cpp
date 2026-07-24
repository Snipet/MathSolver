// Combine a sum of fractions over a common denominator (together.hpp /
// docs/proposals/together.md). The companion of `cancel`: where `cancel`
// reduces one fraction, `together` merges an additive expression of fractions
// into a single N/D over the least common denominator.
//
// Unlike `cancel` this needs no polynomial GCD or division — the LCD is the
// product of each distinct denominator base raised to the maximum power seen
// across terms — so it is fully multivariate and imposes no single-symbol or
// rational-coefficient restriction. Bases are compared structurally; the
// denominator is left factored. Value-preserving wherever every original
// denominator is nonzero (formal cancellation, same doctrine as x/x -> 1),
// and idempotent. Any 64-bit OverflowError anywhere is caught and the
// simplified input is returned unchanged (never throws).

#include <utility>
#include <vector>

#include "mathsolver/errors.hpp"
#include "mathsolver/expr.hpp"
#include "mathsolver/simplify.hpp"

namespace mathsolver {

namespace {

/// One distinct denominator base and the largest power it appears to.
struct BasePower {
    Expr base;
    long long exp;
};

/// A term after splitting: its numerator and its denominator factors.
struct SplitTerm {
    Expr numerator;
    std::vector<BasePower> denom;  // each exp > 0
};

/// Add (base, exp) to a factor list, merging into an existing structurally
/// equal base by summing exponents (a term written base^-1 * base^-2).
void add_factor(std::vector<BasePower>& factors, const Expr& base, long long exp) {
    for (BasePower& f : factors) {
        if (structurally_equal(f.base, base)) {
            f.exp += exp;
            return;
        }
    }
    factors.push_back({base, exp});
}

/// Split one additive term into numerator and denominator factors, by the
/// sign of integer Number Pow exponents (cancel §4.1 / the integrator split).
SplitTerm split_term(const Expr& term) {
    std::vector<Expr> num_factors;
    std::vector<BasePower> denom;
    const auto classify = [&](const Expr& f) {
        if (f->kind() == Kind::Pow && f->arg(1)->kind() == Kind::Number) {
            const Rational& r = f->arg(1)->number();
            if (r.is_integer() && r.is_negative()) {
                add_factor(denom, f->arg(0), (-r.num()).to_ll());
                return;
            }
        }
        num_factors.push_back(f);
    };
    if (term->kind() == Kind::Mul) {
        for (const Expr& f : term->args()) classify(f);
    } else {
        classify(term);
    }
    return {make_mul(std::move(num_factors)), std::move(denom)};
}

/// baseᵢ^expᵢ for a whole factor list.
Expr build_product(const std::vector<BasePower>& factors) {
    std::vector<Expr> parts;
    parts.reserve(factors.size());
    for (const BasePower& f : factors)
        parts.push_back(make_pow(f.base, make_num(f.exp)));
    return make_mul(std::move(parts));  // empty -> 1
}

Expr together_simplified(const Expr& s) {
    // Additive terms (a non-Add is its own single term).
    std::vector<Expr> terms;
    if (s->kind() == Kind::Add) {
        for (const Expr& t : s->args()) terms.push_back(t);
    } else {
        terms.push_back(s);
    }

    std::vector<SplitTerm> split;
    split.reserve(terms.size());
    std::vector<BasePower> lcd;  // distinct bases at their maximum power
    for (const Expr& t : terms) {
        SplitTerm st = split_term(t);
        for (const BasePower& d : st.denom) {
            bool merged = false;
            for (BasePower& l : lcd) {
                if (structurally_equal(l.base, d.base)) {
                    if (d.exp > l.exp) l.exp = d.exp;
                    merged = true;
                    break;
                }
            }
            if (!merged) lcd.push_back({d.base, d.exp});
        }
        split.push_back(std::move(st));
    }

    if (lcd.empty()) return s;  // no symbolic denominator — nothing to combine

    const Expr D = build_product(lcd);

    // Each term's numerator scaled by D / (its denominator): for every LCD
    // base, raise it to (lcd_exp - term_exp) >= 0.
    std::vector<Expr> scaled;
    scaled.reserve(split.size());
    for (const SplitTerm& st : split) {
        std::vector<BasePower> mult;
        for (const BasePower& l : lcd) {
            long long term_exp = 0;
            for (const BasePower& d : st.denom) {
                if (structurally_equal(d.base, l.base)) {
                    term_exp = d.exp;
                    break;
                }
            }
            const long long e = l.exp - term_exp;  // >= 0 by construction
            if (e != 0) mult.push_back({l.base, e});
        }
        scaled.push_back(make_mul({st.numerator, build_product(mult)}));
    }

    const Expr N = simplify(make_add(std::move(scaled)));
    // A whole-fraction reduction: N == D exactly means the value is 1.
    // (together does not otherwise cancel — that is `cancel`, §7 — but leaving
    // (x+1)/(x+1) unreduced would be a poor single-fraction result.)
    if (structurally_equal(N, D)) return make_num(1);
    // Build N/D with the denominator left *grouped* (Pow of the factored Mul),
    // not via a top-level simplify: simplify would distribute Pow(x*y,-1) into
    // x^-1·y^-1, which the printer renders as `y^(-1)*(x+y)/x` instead of the
    // clean `(x+y)/(x*y)` a single fraction is meant to read as.
    return make_div(N, D);
}

}  // namespace

Expr together(const Expr& e) {
    const Expr s = simplify(e);
    try {
        return together_simplified(s);
    } catch (const OverflowError&) {
        return s;
    }
}

Equation together(const Equation& eq) {
    return Equation{together(eq.lhs), together(eq.rhs)};
}

}  // namespace mathsolver
