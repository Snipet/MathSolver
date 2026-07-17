// Taylor series expansion (series.hpp): repeated symbolic differentiation
// with exact evaluation at the center.

#include "mathsolver/series.hpp"

#include <cmath>
#include <format>
#include <string>
#include <vector>

#include "mathsolver/apart.hpp"
#include "mathsolver/derivative.hpp"
#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/printer.hpp"
#include "mathsolver/simplify.hpp"

namespace mathsolver {

Expr series(const Expr& f, std::string_view var, const Expr& center,
            int order) {
    constexpr int k_max_order = 20; // 20! still fits in long long exactly
    const std::string v{var};
    if (v.empty()) {
        throw Error("series needs a variable");
    }
    if (order < 0 || order > k_max_order) {
        throw Error(std::format("series order must be in [0, {}], got {}",
                                k_max_order, order));
    }
    if (contains_symbol(center, v)) {
        throw Error("series: the expansion point must not depend on the "
                    "series variable");
    }

    const Expr c = simplify(center);
    Expr derivative = simplify(f);
    long long factorial = 1;
    std::vector<Expr> terms;
    for (int k = 0; k <= order; ++k) {
        if (k > 0) {
            factorial *= k;
            derivative = simplify(differentiate(derivative, v));
        }
        Expr at_center;
        try {
            at_center = simplify(substitute(derivative, v, c));
        } catch (const Error& e) {
            throw Error(std::format(
                "series: the order-{} derivative is singular at {} ({})", k,
                to_string(c, PrintStyle::Plain), e.what()));
        }
        const Expr coefficient =
            simplify(make_div(at_center, make_num(factorial)));
        if (coefficient->kind() == Kind::Number &&
            coefficient->number().is_zero()) {
            continue;
        }
        if (k == 0) {
            terms.push_back(coefficient);
        } else {
            terms.push_back(make_mul(
                {coefficient,
                 make_pow(simplify(make_sub(make_sym(v), c)), make_num(k))}));
        }
    }
    if (terms.empty()) {
        return make_num(0);
    }
    return simplify(make_add(std::move(terms)));
}

namespace {

/// p(1/u)·u^deg — the reversed polynomial, regular at u = 0 with constant
/// term equal to p's leading coefficient.
Expr reversed_poly(const std::vector<Expr>& coeffs, const std::string& u) {
    std::vector<Expr> terms;
    const std::size_t deg = coeffs.size() - 1;
    for (std::size_t k = 0; k < coeffs.size(); ++k) {
        terms.push_back(make_mul(
            {coeffs[k],
             make_pow(make_sym(u),
                      make_num(static_cast<long long>(deg - k)))}));
    }
    return simplify(make_add(std::move(terms)));
}

/// One proper apart() term num·(base)^-k rewritten as a function of u = 1/x
/// that is REGULAR at u = 0:  u^{k·m - deg(num)} · revnum(u) / revbase(u)^k.
/// (Direct substitution leaves nested 1/u fractions that read as singular.)
Expr proper_term_in_u(const Expr& term, const std::string& v,
                      const std::string& u) {
    std::vector<Expr> num_factors;
    Expr base;
    long long k = 0;
    const std::vector<Expr> fs =
        term->kind() == Kind::Mul ? term->args() : std::vector<Expr>{term};
    for (const Expr& factor : fs) {
        if (k == 0 && factor->kind() == Kind::Pow &&
            factor->arg(1)->kind() == Kind::Number &&
            factor->arg(1)->number().is_integer() &&
            factor->arg(1)->number().num() < 0 &&
            contains_symbol(factor->arg(0), v)) {
            base = factor->arg(0);
            k = -factor->arg(1)->number().num();
        } else {
            num_factors.push_back(factor);
        }
    }
    const Expr numerator =
        num_factors.empty()
            ? make_num(1)
            : (num_factors.size() == 1 ? num_factors.front()
                                       : simplify(make_mul(std::move(num_factors))));
    const auto nc = polynomial_coefficients(numerator, v);
    const auto bc = polynomial_coefficients(base, v);
    if (!nc || !bc) {
        throw Error("series: the term is not rational");
    }
    const long long m = static_cast<long long>(bc->size()) - 1;
    const long long shift =
        k * m - (static_cast<long long>(nc->size()) - 1);
    return simplify(make_mul(
        {make_pow(make_sym(u), make_num(shift)), reversed_poly(*nc, u),
         make_pow(reversed_poly(*bc, u), make_num(-k))}));
}

} // namespace

Expr series_at_infinity(const Expr& f, std::string_view var, int order) {
    const std::string v{var};
    if (v.empty()) {
        throw Error("series needs a variable");
    }
    // Rational inputs: apart() separates the exact polynomial part (which
    // IS its own asymptotic expansion) from proper fractions, each of which
    // is rewritten as a reversed-polynomial ratio regular at u = 0. Anything
    // non-rational tries the direct u = 1/var substitution (ln(1+1/x),
    // e^(1/x), ...).
    const std::string u = "__seru";
    const Expr target = simplify(f);
    std::vector<Expr> out;   // pass-through polynomial part
    std::vector<Expr> in_u;  // terms as regular functions of u
    bool rational = true;
    try {
        const Expr split = apart(target, v);
        const std::vector<Expr> parts =
            split->kind() == Kind::Add ? split->args()
                                       : std::vector<Expr>{split};
        for (const Expr& part : parts) {
            bool has_den = false;
            const std::vector<Expr> fs =
                part->kind() == Kind::Mul ? part->args()
                                          : std::vector<Expr>{part};
            for (const Expr& factor : fs) {
                if (factor->kind() == Kind::Pow &&
                    factor->arg(1)->kind() == Kind::Number &&
                    factor->arg(1)->number() < Rational{0} &&
                    contains_symbol(factor->arg(0), v)) {
                    has_den = true;
                    break;
                }
            }
            if (has_den) {
                in_u.push_back(proper_term_in_u(part, v, u));
            } else if (polynomial_coefficients(part, v)) {
                out.push_back(part);
            } else {
                rational = false; // e^x-style term with no denominator
                break;
            }
        }
    } catch (const Error&) {
        rational = false;
    }
    if (!rational) {
        out.clear();
        in_u = {simplify(substitute(target, v,
                                    make_div(make_num(1), make_sym(u))))};
    }

    if (!in_u.empty()) {
        const Expr gu = in_u.size() == 1 ? in_u.front()
                                         : simplify(make_add(std::move(in_u)));
        Expr expansion;
        try {
            expansion = series(gu, u, make_num(0), order);
        } catch (const Error& e) {
            throw Error(std::format(
                "series: no expansion in powers of 1/{} at infinity ({})", v,
                e.what()));
        }
        out.push_back(simplify(substitute(
            expansion, u, make_div(make_num(1), make_sym(v)))));
    }
    if (out.empty()) {
        return make_num(0);
    }
    if (out.size() == 1) {
        return simplify(out.front());
    }
    return simplify(make_add(std::move(out)));
}

// --- Stirling / gamma asymptotics ------------------------------------------

std::vector<Rational> bernoulli_numbers(int m) {
    if (m < 0 || m > 20) {
        throw Error("bernoulli: the index must be in [0, 20]");
    }
    // B_n = -1/(n+1) * sum_{j<n} C(n+1, j) B_j, from
    // sum_{j=0}^{n} C(n+1, j) B_j = 0 (n >= 1), B_0 = 1.
    std::vector<Rational> b{Rational(1)};
    for (int n = 1; n <= m; ++n) {
        // Binomials C(n+1, j) by the multiplicative recurrence, exact.
        Rational acc(0);
        Rational binom(1); // C(n+1, 0)
        for (int j = 0; j < n; ++j) {
            acc = acc + binom * b[static_cast<std::size_t>(j)];
            binom = binom * Rational(n + 1 - j) / Rational(j + 1);
        }
        b.push_back(-acc / Rational(n + 1));
    }
    return b;
}

StirlingResult stirling_series(std::string_view var, int terms) {
    if (terms < 0 || terms > 8) {
        throw Error("stirling: the number of correction terms must be in "
                    "[0, 8]");
    }
    const std::string v(var);
    const Expr x = make_sym(v);
    // (x - 1/2) ln x - x + ln(2 pi)/2.
    std::vector<Expr> parts;
    parts.push_back(make_mul(
        {make_sub(x, make_num(Rational(1, 2))), make_fn(FunctionId::Ln, x)}));
    parts.push_back(make_neg(x));
    parts.push_back(make_div(
        make_fn(FunctionId::Ln,
                make_mul({make_num(2), make_const(ConstantId::Pi)})),
        make_num(2)));
    const std::vector<Rational> b = bernoulli_numbers(2 * terms);
    for (int k = 1; k <= terms; ++k) {
        // B_2k / (2k (2k-1)) * x^(1-2k), coefficient folded exactly.
        const Rational coeff = b[static_cast<std::size_t>(2 * k)] /
                               Rational(2LL * k * (2LL * k - 1));
        parts.push_back(make_mul(
            {make_num(coeff), make_pow(x, make_num(1 - 2 * k))}));
    }
    StirlingResult out;
    out.series = simplify(make_add(std::move(parts)));
    for (const double sample : {5.0, 10.0, 20.0}) {
        const double approx = evaluate(out.series, Bindings{{v, sample}});
        const double exact = std::lgamma(sample);
        out.checks.push_back(std::format(
            "ln Gamma({:g}): Stirling {:.12g} vs exact {:.12g} "
            "(|error| = {:.3g})",
            sample, approx, exact, std::abs(approx - exact)));
    }
    return out;
}

} // namespace mathsolver
