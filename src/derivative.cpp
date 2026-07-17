#include "mathsolver/derivative.hpp"

#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

#include "mathsolver/errors.hpp"
#include "mathsolver/expr.hpp"
#include "mathsolver/rational.hpp"
#include "mathsolver/simplify.hpp"

namespace mathsolver {
namespace {

bool is_zero(const Expr& e) {
    return e->kind() == Kind::Number && e->number().is_zero();
}

Expr diff(const Expr& e, std::string_view symbol);

Expr one_minus_square(const Expr& u) {
    return make_sub(make_num(1), make_pow(u, make_num(2)));
}

/// f'(u) per the DESIGN.md §8 chain-rule table; the caller multiplies by u'.
Expr chain_factor(FunctionId id, const Expr& u) {
    switch (id) {
    case FunctionId::Sin:
        return make_fn(FunctionId::Cos, u);
    case FunctionId::Cos:
        return make_neg(make_fn(FunctionId::Sin, u));
    case FunctionId::Tan:
        return make_add({make_num(1), make_pow(make_fn(FunctionId::Tan, u), make_num(2))});
    case FunctionId::Asin:
        return make_pow(one_minus_square(u), make_num(Rational(-1, 2)));
    case FunctionId::Acos:
        return make_neg(make_pow(one_minus_square(u), make_num(Rational(-1, 2))));
    case FunctionId::Atan:
        return make_pow(make_add({make_num(1), make_pow(u, make_num(2))}), make_num(-1));
    case FunctionId::Sinh:
        return make_fn(FunctionId::Cosh, u);
    case FunctionId::Cosh:
        return make_fn(FunctionId::Sinh, u);
    case FunctionId::Tanh:
        return make_sub(make_num(1), make_pow(make_fn(FunctionId::Tanh, u), make_num(2)));
    case FunctionId::Asinh:
        // 1 / sqrt(u^2 + 1)
        return make_pow(make_add({make_pow(u, make_num(2)), make_num(1)}),
                        make_num(Rational(-1, 2)));
    case FunctionId::Acosh:
        // 1 / sqrt(u^2 - 1)
        return make_pow(make_add({make_pow(u, make_num(2)), make_num(-1)}),
                        make_num(Rational(-1, 2)));
    case FunctionId::Atanh:
        // 1 / (1 - u^2)
        return make_pow(make_sub(make_num(1), make_pow(u, make_num(2))), make_num(-1));
    case FunctionId::Ln:
        return make_pow(u, make_num(-1));
    case FunctionId::Abs:
        // d(abs(u)) = u' * u / abs(u); undefined at u == 0 (documented).
        return make_mul({u, make_pow(make_fn(FunctionId::Abs, u), make_num(-1))});
    }
    throw Error("differentiate: unknown FunctionId");
}

Expr diff_add(const Expr& e, std::string_view symbol) {
    std::vector<Expr> terms;
    terms.reserve(e->args().size());
    for (const Expr& t : e->args()) {
        terms.push_back(diff(t, symbol));
    }
    return make_add(std::move(terms));
}

/// n-ary product rule: sum over i of arg_i' * prod_{j != i} arg_j.
Expr diff_mul(const Expr& e, std::string_view symbol) {
    const std::vector<Expr>& factors = e->args();
    std::vector<Expr> terms;
    terms.reserve(factors.size());
    for (std::size_t i = 0; i < factors.size(); ++i) {
        Expr di = diff(factors[i], symbol);
        if (is_zero(di)) {
            continue;
        }
        std::vector<Expr> product;
        product.reserve(factors.size());
        product.push_back(std::move(di));
        for (std::size_t j = 0; j < factors.size(); ++j) {
            if (j != i) {
                product.push_back(factors[j]);
            }
        }
        terms.push_back(make_mul(std::move(product)));
    }
    return make_add(std::move(terms));
}

Expr diff_pow(const Expr& e, std::string_view symbol) {
    const Expr& base = e->arg(0);
    const Expr& expo = e->arg(1);

    // Special case: d(E^v) = E^v * v'.
    if (base->kind() == Kind::Constant && base->constant() == ConstantId::E) {
        return make_mul({e, diff(expo, symbol)});
    }

    // Special case: constant exponent, d(u^v) = v * u^(v-1) * u'.
    if (!contains_symbol(expo, symbol)) {
        Expr du = diff(base, symbol);
        if (is_zero(du)) {
            // Short-circuit before building u^(v-1): keeps degenerate bases
            // (whose derivative is literally 0) from tripping factory folds.
            return make_num(0);
        }
        return make_mul({expo, make_pow(base, make_sub(expo, make_num(1))), std::move(du)});
    }

    // General rule: d(u^v) = u^v * (v'*ln(u) + v*u'/u).
    Expr du = diff(base, symbol);
    Expr dv = diff(expo, symbol);
    Expr log_part = make_mul({std::move(dv), make_fn(FunctionId::Ln, base)});
    Expr ratio_part = is_zero(du)
                          ? make_num(0)
                          : make_mul({expo, std::move(du), make_pow(base, make_num(-1))});
    return make_mul({e, make_add({std::move(log_part), std::move(ratio_part)})});
}

Expr diff_fn(const Expr& e, std::string_view symbol) {
    const Expr& u = e->arg(0);
    Expr du = diff(u, symbol);
    if (is_zero(du)) {
        return make_num(0);
    }
    return make_mul({chain_factor(e->function(), u), std::move(du)});
}

Expr diff(const Expr& e, std::string_view symbol) {
    if (!contains_symbol(e, symbol)) {
        // Numbers, constants, other symbols, and any subtree free of the
        // differentiation variable.
        return make_num(0);
    }
    switch (e->kind()) {
    case Kind::Symbol:
        return make_num(1); // contains_symbol above guarantees the name matches
    case Kind::Add:
        return diff_add(e, symbol);
    case Kind::Mul:
        return diff_mul(e, symbol);
    case Kind::Pow:
        return diff_pow(e, symbol);
    case Kind::Function:
        return diff_fn(e, symbol);
    case Kind::Number:
    case Kind::Constant:
        break; // unreachable: contains_symbol is false for these kinds
    }
    return make_num(0);
}

} // namespace

Expr differentiate(const Expr& e, std::string_view symbol) {
    return simplify(diff(e, symbol));
}

} // namespace mathsolver
