// Taylor series expansion (series.hpp): repeated symbolic differentiation
// with exact evaluation at the center.

#include "mathsolver/series.hpp"

#include <format>
#include <string>
#include <vector>

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

} // namespace mathsolver
