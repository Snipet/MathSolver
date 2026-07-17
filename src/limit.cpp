// Limits (limit.hpp).
//
// Strategy ladder, each rung falling through to the next:
//   1. direct substitution (continuous at the point)
//   2. L'Hôpital iteration on quotient terms that are exactly 0/0 at the
//      point (both parts checked symbolically)
//   3. numeric extrapolation approaching the point geometrically, which
//      also classifies divergence and left/right disagreement
// Infinity reduces to a one-sided limit at 0 via x = 1/u.

#include "mathsolver/limit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>
#include <optional>
#include <vector>

#include "mathsolver/derivative.hpp"
#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/printer.hpp"
#include "mathsolver/simplify.hpp"

namespace mathsolver {

namespace {

bool is_zero(const Expr& e) {
    return e->kind() == Kind::Number && e->number().is_zero();
}

std::vector<Expr> factors_of(const Expr& e) {
    if (e->kind() == Kind::Mul) {
        return e->args();
    }
    return {e};
}

Expr product(std::vector<Expr> fs) {
    if (fs.empty()) {
        return make_num(1);
    }
    if (fs.size() == 1) {
        return fs.front();
    }
    return simplify(make_mul(std::move(fs)));
}

/// Best 64-bit rational approximation of a double via its decimal spelling
/// (mirrors the solver's numeric-root representation).
Rational rational_from_double(double x) {
    if (!std::isfinite(x)) {
        return Rational(0);
    }
    const double ax = std::abs(x);
    int decimals = 15;
    if (ax >= 1.0) {
        decimals = std::clamp(
            15 - static_cast<int>(std::floor(std::log10(ax))) - 1, 0, 15);
    }
    for (int d = decimals; d >= 0; --d) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.*f", d, x);
        try {
            return Rational::from_decimal_string(buf);
        } catch (const Error&) {
        }
    }
    return Rational(0);
}

/// simplify(substitute(...)) that reports failure instead of throwing
/// (constant folding throws on 1/0, ln(0), ...).
std::optional<Expr> try_substitute(const Expr& e, const std::string& var,
                                   const Expr& point) {
    try {
        return simplify(substitute(e, var, point));
    } catch (const Error&) {
        return std::nullopt;
    }
}

/// The expression is a constant (no free symbols beyond parameters — for the
/// limit's purposes: it no longer contains the limit variable).
bool settled(const Expr& e, const std::string& var) {
    return !contains_symbol(e, var);
}

/// Split a term into numerator/denominator by negative-integer powers.
void split_quotient(const Expr& term, const std::string& var, Expr& num,
                    Expr& den) {
    std::vector<Expr> nf;
    std::vector<Expr> df;
    for (const Expr& f : factors_of(term)) {
        if (f->kind() == Kind::Pow && f->arg(1)->kind() == Kind::Number) {
            const Rational e = f->arg(1)->number();
            if (e.is_integer() && e.num() < 0 && contains_symbol(f->arg(0), var)) {
                df.push_back(e.num() == -1
                                 ? f->arg(0)
                                 : make_pow(f->arg(0), make_num(-e.num())));
                continue;
            }
        }
        nf.push_back(f);
    }
    num = product(std::move(nf));
    den = product(std::move(df));
}

/// Bindings for every free symbol of e except var, seeded with fixed
/// pseudo-random constants so parameterized expressions stay evaluable
/// (the review found unbound parameters silently disabling every guard).
Bindings param_seed(const Expr& e, const std::string& var) {
    Bindings b;
    double v = 1.3173;
    for (const std::string& s : free_symbols(e)) {
        if (s != var) {
            b[s] = v;
            v += 0.8317;
        }
    }
    return b;
}

/// Numeric value of f at var = x with parameters seeded, or nullopt
/// (domain error / non-finite).
std::optional<double> value_at(const Expr& f, const std::string& var, double x) {
    try {
        Bindings b = param_seed(f, var);
        b[var] = x;
        const double v = evaluate(f, b);
        if (std::isfinite(v)) {
            return v;
        }
    } catch (const Error&) {
    }
    return std::nullopt;
}

/// Numeric value of a var-free expression with parameters seeded.
std::optional<double> settled_value(const Expr& e) {
    try {
        const double v = evaluate(e, param_seed(e, ""));
        if (std::isfinite(v)) {
            return v;
        }
    } catch (const Error&) {
    }
    return std::nullopt;
}

/// Numeric fallback: approach point from one side (s = +1 right, -1 left)
/// with geometrically shrinking steps and look at the tail behaviour.
LimitResult numeric_side(const Expr& f, const std::string& var, double point,
                         int s) {
    LimitResult r;
    std::vector<std::pair<double, double>> vals; // (h, f(p + s h))
    for (int k = 3; k <= 12; ++k) {
        const double h = std::pow(10.0, -k);
        const auto v = value_at(f, var, point + s * h);
        if (v) {
            vals.push_back({h, *v});
        }
    }
    if (vals.size() < 4) {
        r.status = LimitResult::Status::Unsolved;
        return r;
    }
    // Convergence: accept the FIRST consecutive triple that agrees — the
    // deepest samples are often dominated by floating-point noise (e.g.
    // (1+h)^(1/h) loses ln(1+h) precision long before h = 1e-12). Guards
    // (all found necessary by adversarial review):
    //   - a zero plateau after much larger samples is catastrophic
    //     cancellation ((1-cos h)/h^4 collapses to exact 0), not a limit;
    //   - an off-decade confirmation sample rejects grid-aliased
    //     oscillations;
    //   - a value too large for the exact Rational range must not silently
    //     round-trip through 0.
    for (std::size_t i = 2; i < vals.size(); ++i) {
        const double a = vals[i - 2].second;
        const double b = vals[i - 1].second;
        const double c = vals[i].second;
        const double scale = 1.0 + std::abs(c);
        if (std::abs(c - b) >= 1e-6 * scale || std::abs(b - a) >= 1e-5 * scale) {
            continue;
        }
        if (std::abs(c) <= 1e-8) {
            double peak = 0.0;
            for (std::size_t j = 0; j + 2 < i; ++j) {
                peak = std::max(peak, std::abs(vals[j].second));
            }
            if (peak > 1e3) {
                continue; // cancellation plateau, not convergence
            }
        }
        const auto confirm =
            value_at(f, var, point + s * vals[i].first / 3.7);
        if (!confirm || std::abs(*confirm - c) > 1e-5 * scale) {
            continue; // aliasing or not actually converged
        }
        if (std::abs(c) > 1e-8) {
            const Rational rc = rational_from_double(c);
            if (rc.is_zero()) {
                r.status = LimitResult::Status::Unsolved;
                r.warnings.push_back(
                    "the limit magnitude exceeds the exact numeric range");
                return r;
            }
            r.value = make_num(rc);
        } else {
            r.value = make_num(0); // snap h-scale noise around a true 0
        }
        r.status = LimitResult::Status::Numeric;
        r.method = "numeric extrapolation";
        return r;
    }
    // Divergence: consistently growing magnitude with a stable sign. A
    // growing magnitude with flipping sign (x sin x at infinity) is NOT a
    // divergence to signed infinity — report Unsolved, never "unsigned inf"
    // from samples alone.
    const double a = vals[vals.size() - 3].second;
    const double b = vals[vals.size() - 2].second;
    const double c = vals.back().second;
    if (std::abs(c) > 1e8 && std::abs(c) > 2.0 * std::abs(a) &&
        std::abs(b) > std::abs(a) &&
        ((b > 0 && c > 0 && a > 0) || (b < 0 && c < 0 && a < 0))) {
        r.status = LimitResult::Status::Diverges;
        r.sign = c > 0 ? 1 : -1;
        r.method = "numeric divergence";
        return r;
    }
    r.status = LimitResult::Status::Unsolved;
    return r;
}

LimitResult one_sided(const Expr& f, const std::string& var, const Expr& point,
                      int s);

/// L'Hôpital on an exact 0/0 quotient; empty result when not applicable.
/// `s` (±1) is the approach side, used to sign a proven blow-up.
std::optional<LimitResult> lhopital(const Expr& f, const std::string& var,
                                    const Expr& point, int s) {
    Expr num, den;
    split_quotient(f, var, num, den);
    if (is_zero(simplify(make_sub(den, make_num(1))))) {
        return std::nullopt; // no denominator -> not a quotient
    }
    for (int iter = 0; iter < 8; ++iter) {
        const auto n0 = try_substitute(num, var, point);
        const auto d0 = try_substitute(den, var, point);
        if (!n0 || !d0 || !settled(*n0, var) || !settled(*d0, var)) {
            return std::nullopt; // not evaluable -> other machinery
        }
        if (is_zero(*n0) && is_zero(*d0)) {
            num = simplify(differentiate(num, var));
            den = simplify(differentiate(den, var));
            continue;
        }
        if (!is_zero(*d0)) {
            LimitResult r;
            r.status = LimitResult::Status::Exact;
            r.value = simplify(make_div(*n0, *d0));
            r.method = iter == 0 ? "substitution" : "l'hopital";
            return r;
        }
        // n0 != 0, d0 == 0: a proven one-sided blow-up. Do NOT hand this to
        // the numeric fallback (cancellation plateaus there turned proven
        // infinities into "0"); sign it from a moderate-step sample of f.
        LimitResult r;
        r.status = LimitResult::Status::Diverges;
        r.method = "l'hopital (denominator vanishes)";
        try {
            const double p = evaluate(point, Bindings{});
            const auto sample = value_at(f, var, p + s * 1e-3);
            r.sign = !sample ? 0 : *sample > 0 ? 1 : *sample < 0 ? -1 : 0;
        } catch (const Error&) {
            r.sign = 0;
        }
        return r;
    }
    return std::nullopt;
}

LimitResult one_sided(const Expr& f, const std::string& var, const Expr& point,
                      int s) {
    std::optional<double> p_num;
    try {
        p_num = evaluate(point, Bindings{});
    } catch (const Error&) {
    }

    // Trust ladder for a candidate exact value (all steps demanded by the
    // adversarial review):
    //   a. the value itself must evaluate (with parameters seeded) — an
    //      Exact "ln(0)" is not an answer;
    //   b. if f is DEFINED at the point (evaluates finite there), the
    //      substituted value must match f(p) itself — this both certifies
    //      continuity-by-evaluation and protects boundary-layer functions
    //      (x/(x + 1e-15)) from being vetoed by a too-coarse probe;
    //   c. if f is undefined at the point, a nearby sample must agree —
    //      simplify's 0·x → 0 rule swallows 0·(1/0) products (abs(x)/x and
    //      its L'Hôpital derivative both substitute to a false 0), and the
    //      comparison is relative so small-magnitude jumps still veto.
    //   A probe that fails on a genuine domain edge does not veto.
    const auto trusted_exact = [&](const Expr& value_expr) -> bool {
        const auto v = settled_value(value_expr);
        if (!v) {
            return false; // unevaluable value (ln(0)-style): reject
        }
        if (!p_num) {
            return true; // symbolic point: accept on faith (documented)
        }
        if (const auto fp = value_at(f, var, *p_num)) {
            return std::abs(*fp - *v) <= 1e-9 * (1.0 + std::abs(*v));
        }
        const auto near = value_at(f, var, *p_num + s * 1e-6);
        if (!near) {
            return true; // domain edge: nothing to compare against
        }
        return std::abs(*near - *v) <=
               1e-3 * (std::abs(*v) + std::abs(*near)) + 1e-12;
    };

    // 1. Direct substitution.
    const auto direct = try_substitute(f, var, point);
    if (direct && settled(*direct, var) && trusted_exact(*direct)) {
        LimitResult r;
        r.status = LimitResult::Status::Exact;
        r.value = *direct;
        r.method = "substitution";
        return r;
    }

    // 2. L'Hôpital on 0/0 quotients (its proven blow-ups pass through).
    if (const auto lh = lhopital(simplify(f), var, point, s)) {
        if (lh->status != LimitResult::Status::Exact ||
            trusted_exact(lh->value)) {
            return *lh;
        }
    }

    // 3. Numeric extrapolation.
    if (!p_num) {
        LimitResult r;
        r.status = LimitResult::Status::Unsolved;
        r.warnings.push_back("the limit point must be numeric");
        return r;
    }
    return numeric_side(f, var, *p_num, s);
}

std::string describe(const LimitResult& r) {
    switch (r.status) {
        case LimitResult::Status::Exact:
        case LimitResult::Status::Numeric:
            return to_string(r.value, PrintStyle::Plain);
        case LimitResult::Status::Diverges:
            return r.sign > 0 ? "+inf" : r.sign < 0 ? "-inf" : "inf (unsigned)";
        default:
            return "?";
    }
}

} // namespace

LimitResult limit(const Expr& f, std::string_view var, const Expr& point,
                  int direction) {
    const std::string v{var};
    if (v.empty()) {
        throw Error("limit needs a variable");
    }
    const Expr p = simplify(point);
    if (contains_symbol(p, v)) {
        throw Error("the limit point must not depend on the limit variable");
    }
    if (direction != 0) {
        return one_sided(f, v, p, direction);
    }
    // Two-sided: the sides must agree. An Exact result by substitution or
    // L'Hôpital already covers both sides; only compare when a side went
    // numeric or divergent.
    const LimitResult right = one_sided(f, v, p, +1);
    if (right.status == LimitResult::Status::Exact) {
        return right;
    }
    const LimitResult left = one_sided(f, v, p, -1);
    const auto same_numeric = [&]() {
        if (right.status != left.status) {
            return false;
        }
        if (right.status == LimitResult::Status::Numeric) {
            const double a = evaluate(right.value, Bindings{});
            const double b = evaluate(left.value, Bindings{});
            return std::abs(a - b) < 1e-6 * (1.0 + std::abs(a));
        }
        if (right.status == LimitResult::Status::Diverges) {
            return right.sign == left.sign && right.sign != 0;
        }
        return false;
    };
    if (same_numeric()) {
        return right;
    }
    if (right.status == LimitResult::Status::Unsolved &&
        left.status == LimitResult::Status::Unsolved) {
        return right;
    }
    LimitResult r;
    r.status = LimitResult::Status::DoesNotExist;
    r.warnings.push_back(std::format("left limit: {}", describe(left)));
    r.warnings.push_back(std::format("right limit: {}", describe(right)));
    r.method = "one-sided comparison";
    return r;
}

LimitResult limit_at_infinity(const Expr& f, std::string_view var,
                              bool positive) {
    const std::string v{var};
    if (v.empty()) {
        throw Error("limit needs a variable");
    }

    // Rational functions (including plain polynomials) are exact by degree
    // analysis of numerator and denominator leading terms.
    {
        Expr num, den;
        split_quotient(simplify(f), v, num, den);
        const auto nc = polynomial_coefficients(num, v);
        const auto dc = polynomial_coefficients(den, v);
        if (nc && dc && !nc->empty() && !dc->empty()) {
            const std::size_t dn = nc->size() - 1;
            const std::size_t dd = dc->size() - 1;
            LimitResult r;
            if (dn < dd) {
                r.status = LimitResult::Status::Exact;
                r.value = make_num(0);
            } else if (dn == dd) {
                r.status = LimitResult::Status::Exact;
                r.value = simplify(make_div(nc->back(), dc->back()));
            } else {
                r.status = LimitResult::Status::Diverges;
                try {
                    const double lead = evaluate(
                        simplify(make_div(nc->back(), dc->back())), Bindings{});
                    const int parity =
                        (!positive && (dn - dd) % 2 == 1) ? -1 : 1;
                    r.sign = (lead > 0 ? 1 : lead < 0 ? -1 : 0) * parity;
                } catch (const Error&) {
                    r.sign = 0; // symbolic leading coefficient
                }
            }
            r.method = "rational degree analysis";
            return r;
        }
    }

    // General case: x -> +inf is u -> 0+ of f(1/u); x -> -inf is u -> 0-.
    // Use a fresh variable name that cannot collide with the input's symbols.
    const std::string u = "__limu";
    const Expr fu = simplify(
        expand(substitute(f, v, make_div(make_num(1), make_sym(u)))));
    LimitResult r = one_sided(fu, u, make_num(0), positive ? +1 : -1);
    r.method = r.method.empty() ? "x = 1/u reduction"
                                : "x = 1/u reduction, " + r.method;
    return r;
}

} // namespace mathsolver
