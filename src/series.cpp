// Taylor series expansion (series.hpp): repeated symbolic differentiation
// with exact evaluation at the center.

#include "mathsolver/series.hpp"

#include <format>
#include <string>
#include <vector>

#include "mathsolver/apart.hpp"
#include "mathsolver/derivative.hpp"
#include "mathsolver/errors.hpp"
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

} // namespace mathsolver
