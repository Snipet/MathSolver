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

/// Numeric value of f at var = x, or nullopt (domain error / non-finite).
std::optional<double> value_at(const Expr& f, const std::string& var, double x) {
    try {
        const double v = evaluate(f, Bindings{{var, x}});
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
    std::vector<double> vals;
    for (int k = 3; k <= 12; ++k) {
        const double h = std::pow(10.0, -k);
        const auto v = value_at(f, var, point + s * h);
        if (v) {
            vals.push_back(*v);
        }
    }
    if (vals.size() < 4) {
        r.status = LimitResult::Status::Unsolved;
        return r;
    }
    // Convergence: accept the FIRST consecutive triple that agrees — the
    // deepest samples are often dominated by floating-point noise (e.g.
    // (1+h)^(1/h) loses ln(1+h) precision long before h = 1e-12).
    for (std::size_t i = 2; i < vals.size(); ++i) {
        const double a = vals[i - 2];
        const double b = vals[i - 1];
        const double c = vals[i];
        const double scale = 1.0 + std::abs(c);
        if (std::abs(c - b) < 1e-6 * scale && std::abs(b - a) < 1e-5 * scale) {
            r.status = LimitResult::Status::Numeric;
            // Snap h-scale noise around a true 0 to exactly zero.
            r.value = std::abs(c) > 1e-8 ? make_num(rational_from_double(c))
                                         : make_num(0);
            r.method = "numeric extrapolation";
            return r;
        }
    }
    // Divergence: consistently growing magnitude with a stable sign.
    const double a = vals[vals.size() - 3];
    const double b = vals[vals.size() - 2];
    const double c = vals.back();
    if (std::abs(c) > 1e8 && std::abs(c) > 2.0 * std::abs(a) &&
        std::abs(b) > std::abs(a)) {
        r.status = LimitResult::Status::Diverges;
        r.sign = (b > 0 && c > 0) ? 1 : (b < 0 && c < 0) ? -1 : 0;
        r.method = "numeric divergence";
        return r;
    }
    r.status = LimitResult::Status::Unsolved;
    return r;
}

LimitResult one_sided(const Expr& f, const std::string& var, const Expr& point,
                      int s);

/// L'Hôpital on an exact 0/0 quotient; empty result when not applicable.
std::optional<LimitResult> lhopital(const Expr& f, const std::string& var,
                                    const Expr& point) {
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
        // n0 != 0, d0 == 0: one-sided blow-up; let numerics find the sign.
        return std::nullopt;
    }
    return std::nullopt;
}

LimitResult one_sided(const Expr& f, const std::string& var, const Expr& point,
                      int s) {
    // Numeric probe guard for the exact stages: simplify's 0·x → 0 rule can
    // silently swallow a 0·(1/0) product (abs(x)/x at 0 substitutes to 0,
    // and its L'Hôpital derivative x/abs(x) does the same), so a nearby
    // sample of the ORIGINAL expression must agree before an exact value is
    // trusted. A probe that cannot be evaluated (symbolic point, parameters,
    // domain edge) does not veto.
    const auto probe_agrees = [&](const Expr& value_expr) -> bool {
        try {
            const double p = evaluate(point, Bindings{});
            const double v = evaluate(value_expr, Bindings{});
            const auto near = value_at(f, var, p + (s == 0 ? 1 : s) * 1e-6);
            return !near || std::abs(*near - v) <= 1e-3 * (1.0 + std::abs(v));
        } catch (const Error&) {
            return true;
        }
    };

    // 1. Direct substitution.
    const auto direct = try_substitute(f, var, point);
    if (direct && settled(*direct, var) && probe_agrees(*direct)) {
        LimitResult r;
        r.status = LimitResult::Status::Exact;
        r.value = *direct;
        r.method = "substitution";
        return r;
    }

    // 2. L'Hôpital on 0/0 quotients.
    if (const auto lh = lhopital(simplify(f), var, point)) {
        if (probe_agrees(lh->value)) {
            return *lh;
        }
    }

    // 3. Numeric extrapolation.
    double p = 0.0;
    try {
        p = evaluate(point, Bindings{});
    } catch (const Error&) {
        LimitResult r;
        r.status = LimitResult::Status::Unsolved;
        r.warnings.push_back("the limit point must be numeric");
        return r;
    }
    return numeric_side(f, var, p, s);
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
