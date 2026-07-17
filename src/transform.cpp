// Symbolic Laplace / inverse-Laplace transforms (transform.hpp).

#include "mathsolver/transform.hpp"

#include <optional>
#include <vector>

#include "mathsolver/derivative.hpp"
#include "mathsolver/errors.hpp"
#include "mathsolver/printer.hpp"
#include "mathsolver/simplify.hpp"

namespace mathsolver {

namespace {

bool is_e(const Expr& e) {
    return e->kind() == Kind::Constant && e->constant() == ConstantId::E;
}

bool is_zero(const Expr& e) {
    return e->kind() == Kind::Number && e->number().is_zero();
}

std::optional<Rational> as_num(const Expr& e) {
    if (e->kind() == Kind::Number) {
        return e->number();
    }
    return std::nullopt;
}

Expr num(long long n) { return make_num(n); }
Expr powi(const Expr& b, long long e) { return make_pow(b, make_num(e)); }

std::vector<Expr> factors_of(const Expr& e) {
    if (e->kind() == Kind::Mul) {
        return e->args();
    }
    return {e};
}

Expr product(std::vector<Expr> fs) {
    if (fs.empty()) {
        return num(1);
    }
    if (fs.size() == 1) {
        return fs.front();
    }
    return simplify(make_mul(std::move(fs)));
}

/// The coefficient k of a homogeneous-linear argument arg = k * v (k free of
/// v), or nullopt when arg is not of that form.
std::optional<Expr> linear_rate(const Expr& arg, const std::string& v) {
    if (!is_zero(simplify(substitute(arg, v, num(0))))) {
        return std::nullopt; // nonzero constant term -> not homogeneous
    }
    const Expr k = simplify(differentiate(arg, v));
    if (contains_symbol(k, v)) {
        return std::nullopt; // not linear
    }
    return k;
}

// --- forward transform ------------------------------------------------------

/// Transform of a pure t-function g (no leading constant, no bare t^n
/// factors): sin/cos/sinh/cosh and 1, with e^{a t} factors applied as the
/// s-shift G(s - a). Throws for anything else.
Expr laplace_core(const Expr& g_in, const Expr& s, const std::string& t) {
    const Expr g = simplify(g_in);
    const std::string& S = s->symbol_name();

    // Pull out exponential factors (base e), accumulating the total rate.
    Expr rate_sum = num(0);
    bool has_exp = false;
    std::vector<Expr> rest;
    for (const Expr& f : factors_of(g)) {
        if (f->kind() == Kind::Pow && is_e(f->arg(0))) {
            const auto r = linear_rate(f->arg(1), t);
            if (r) {
                rate_sum = simplify(make_add({rate_sum, *r}));
                has_exp = true;
                continue;
            }
        }
        rest.push_back(f);
    }
    if (has_exp) {
        const Expr inner = laplace_core(product(rest), s, t);
        // G(s - a).
        return simplify(substitute(inner, S, make_sub(s, rate_sum)));
    }

    if (is_zero(simplify(make_sub(g, num(1))))) {
        return make_div(num(1), s); // L{1} = 1/s
    }
    if (g->kind() == Kind::Function) {
        const FunctionId id = g->function();
        const auto b = linear_rate(g->arg(0), t);
        if (b) {
            const Expr s2 = powi(s, 2);
            const Expr b2 = simplify(powi(*b, 2));
            switch (id) {
                case FunctionId::Sin:
                    return make_div(*b, make_add({s2, b2}));
                case FunctionId::Cos:
                    return make_div(s, make_add({s2, b2}));
                case FunctionId::Sinh:
                    return make_div(*b, make_sub(s2, b2));
                case FunctionId::Cosh:
                    return make_div(s, make_sub(s2, b2));
                default:
                    break;
            }
        }
    }
    throw Error(std::string("no Laplace transform rule for '") +
                to_string(g, PrintStyle::Plain) + "'");
}

/// Transform of a single product term.
Expr laplace_term(const Expr& term_in, const Expr& s, const std::string& t) {
    const Expr term = simplify(term_in);
    if (!contains_symbol(term, t)) {
        return simplify(make_div(term, s)); // constant c -> c/s
    }
    std::vector<Expr> coef;
    std::vector<Expr> core;
    long long n = 0; // power of t
    for (const Expr& f : factors_of(term)) {
        if (!contains_symbol(f, t)) {
            coef.push_back(f);
        } else if (f->kind() == Kind::Symbol && f->symbol_name() == t) {
            n += 1;
        } else if (f->kind() == Kind::Pow && f->arg(0)->kind() == Kind::Symbol &&
                   f->arg(0)->symbol_name() == t) {
            const auto k = as_num(f->arg(1));
            if (k && k->is_integer() && k->num() > 0) {
                n += k->num();
            } else {
                core.push_back(f);
            }
        } else {
            core.push_back(f);
        }
    }
    Expr F = laplace_core(product(core), s, t);
    for (long long i = 0; i < n; ++i) {
        // L{t^n g} = (-1)^n d^n/ds^n G(s).
        F = simplify(make_neg(differentiate(F, s->symbol_name())));
    }
    return simplify(make_mul({product(coef), F}));
}

// --- inverse transform ------------------------------------------------------

/// Split a term into numerator and (base, power) of a single denominator
/// factor: term == numerator * base^(-power). Returns false when the term has
/// no negative-power factor (a pure polynomial: no inverse as a function).
bool split_rational(const Expr& term, const std::string& s, Expr& numerator,
                    Expr& base, long long& power) {
    std::vector<Expr> num_factors;
    bool found = false;
    for (const Expr& f : factors_of(term)) {
        if (!found && f->kind() == Kind::Pow) {
            const auto e = as_num(f->arg(1));
            if (e && e->is_integer() && e->num() < 0 &&
                contains_symbol(f->arg(0), s)) {
                base = f->arg(0);
                power = -e->num();
                found = true;
                continue;
            }
        }
        num_factors.push_back(f);
    }
    numerator = product(num_factors);
    return found;
}

/// t^{m}/m! as an expression.
Expr t_over_factorial(const std::string& t, long long m) {
    long long fact = 1;
    for (long long i = 2; i <= m; ++i) {
        fact *= i;
    }
    return make_div(m == 0 ? num(1) : powi(make_sym(t), m), num(fact));
}

Expr inverse_term(const Expr& term_in, const Expr& s, const std::string& t) {
    const Expr term = simplify(term_in);
    if (!contains_symbol(term, s->symbol_name())) {
        // A constant in s inverts to c·δ(t); not representable as a function.
        throw Error("inverse transform of a constant is an impulse δ(t), which "
                    "is not an ordinary function");
    }
    Expr numerator, base;
    long long power = 0;
    if (!split_rational(term, s->symbol_name(), numerator, base, power)) {
        throw Error("inverse transform expects a strictly proper rational term "
                    "(a numerator over a power of s, (s - a), or a quadratic)");
    }
    const std::string& S = s->symbol_name();
    if (contains_symbol(numerator, S) &&
        !(base->kind() == Kind::Add || base->kind() == Kind::Symbol)) {
        throw Error("unsupported inverse-transform numerator");
    }

    // Denominator is a power of a linear factor: c / (s - a)^power.
    // Detect linear base: d/ds(base) is a nonzero constant.
    const Expr dbase = simplify(differentiate(base, S));
    const bool base_linear = !contains_symbol(dbase, S) && !is_zero(dbase);
    if (base_linear) {
        const Expr lead = dbase;                       // base = lead*s - lead*a
        const Expr a = simplify(make_div(make_neg(simplify(substitute(base, S, num(0)))),
                                         lead)); // root a of base
        // Normalize base to (s - a): divide the whole term by lead^power.
        // numerator may be a polynomial in s; expand in powers of (s - a) via
        // repeated evaluation is overkill — support constant or degree-≤ (power-1)
        // by splitting numerator = Σ c_j (s-a)^j (Taylor coefficients at s = a).
        Expr result = num(0);
        Expr deriv = simplify(expand(numerator));
        long long fact = 1;
        for (long long j = 0; j < power; ++j) {
            if (j > 0) {
                fact *= j;
                deriv = simplify(differentiate(deriv, S));
            }
            const Expr cj = simplify(make_div(substitute(deriv, S, a), num(fact)));
            if (is_zero(cj)) {
                continue;
            }
            // c_j (s-a)^j / (lead^power (s-a)^power) -> c_j/lead^power ·
            //   t^{power-1-j}/(power-1-j)! · e^{a t}
            const long long m = power - 1 - j;
            if (m < 0) {
                throw Error("inverse transform: numerator degree too high "
                            "(improper term)");
            }
            const Expr coef = simplify(make_div(cj, powi(lead, power)));
            Expr piece = simplify(make_mul({coef, t_over_factorial(t, m)}));
            if (!is_zero(a)) {
                piece = simplify(make_mul(
                    {piece, make_pow(make_const(ConstantId::E),
                                     make_mul({a, make_sym(t)}))}));
            }
            result = simplify(make_add({result, piece}));
        }
        return result;
    }

    // Irreducible quadratic denominator, power 1: (α s + β)/((s - p)^2 + w^2).
    if (power == 1 && base->kind() == Kind::Add) {
        // Coefficients of base = A s^2 + B s + C by Taylor at 0.
        const Expr b0 = simplify(substitute(base, S, num(0)));
        const Expr d1 = simplify(differentiate(base, S));
        const Expr B = simplify(substitute(d1, S, num(0)));
        const Expr d2 = simplify(differentiate(d1, S));
        const Expr An = simplify(make_div(d2, num(2))); // leading coeff A
        // Genuine quadratic: A is a nonzero constant, B and C free of s.
        if (!contains_symbol(An, S) && !is_zero(An) && !contains_symbol(B, S) &&
            !contains_symbol(b0, S)) {
            // Complete the square: base = A[(s + B/2A)^2 + (C/A - B^2/4A^2)].
            const Expr p = simplify(make_div(make_neg(B), make_mul({num(2), An})));
            const Expr w2 =
                simplify(make_sub(make_div(b0, An),
                                  make_div(powi(B, 2), make_mul({num(4), powi(An, 2)}))));
            const auto w2n = as_num(w2);
            if (w2n && w2n->to_double() > 0) {
                const Expr w = simplify(make_pow(w2, make_div(num(1), num(2))));
                // numerator α s + β.
                const Expr alpha = simplify(differentiate(numerator, S));
                if (contains_symbol(alpha, S)) {
                    throw Error("inverse transform: numerator over a quadratic "
                                "must be linear in s");
                }
                const Expr beta = simplify(substitute(numerator, S, num(0)));
                // base = A[(s - a)^2 + w^2] with a = -B/(2A) = p (the shift).
                const Expr a = p;
                // Write α s + β = α (s - a) + (β + α a).
                const Expr cos_c = simplify(make_div(alpha, An));
                const Expr sin_c = simplify(make_div(
                    make_div(make_add({beta, make_mul({alpha, a})}), An), w));
                const Expr wt = make_mul({w, make_sym(t)});
                Expr body = simplify(make_add(
                    {make_mul({cos_c, make_fn(FunctionId::Cos, wt)}),
                     make_mul({sin_c, make_fn(FunctionId::Sin, wt)})}));
                if (!is_zero(a)) {
                    body = simplify(make_mul(
                        {make_pow(make_const(ConstantId::E),
                                  make_mul({a, make_sym(t)})),
                         body}));
                }
                return body;
            }
        }
    }
    throw Error("no inverse Laplace rule for '" +
                to_string(term, PrintStyle::Plain) + "'");
}

} // namespace

Expr laplace(const Expr& f, std::string_view time) {
    const std::string t{time};
    if (t == "s") {
        throw Error("the time variable cannot be 's' (the result is in s); "
                    "use a different name, e.g. laplace f, t");
    }
    const Expr s = make_sym("s");
    const Expr e = simplify(expand(f));
    Expr result = num(0);
    const std::vector<Expr> terms =
        e->kind() == Kind::Add ? e->args() : std::vector<Expr>{e};
    for (const Expr& term : terms) {
        result = simplify(make_add({result, laplace_term(term, s, t)}));
    }
    return simplify(result);
}

Expr inverse_laplace(const Expr& F, std::string_view svar) {
    const std::string s{svar};
    if (s == "t") {
        throw Error("the frequency variable cannot be 't' (the result is in t); "
                    "use a different name, e.g. ilaplace F, s");
    }
    const Expr sy = make_sym(s);
    const std::string t = "t";
    // Expand so a sum of partial fractions splits into invertible terms; keep
    // denominators intact (expand is polynomial-only, so (s-a)^-2 survives).
    const Expr e = simplify(F);
    Expr result = num(0);
    const std::vector<Expr> terms =
        e->kind() == Kind::Add ? e->args() : std::vector<Expr>{e};
    for (const Expr& term : terms) {
        result = simplify(make_add({result, inverse_term(term, sy, t)}));
    }
    return simplify(result);
}

} // namespace mathsolver
