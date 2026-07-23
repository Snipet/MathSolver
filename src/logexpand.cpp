#include "mathsolver/logexpand.hpp"

#include <utility>
#include <vector>

#include "mathsolver/simplify.hpp"

namespace mathsolver {

namespace {

bool is_ln(const Expr& e) {
    return e->kind() == Kind::Function && e->function() == FunctionId::Ln;
}

/// Expansion of ln(u): distribute over products and pull down exponents.
Expr expand_ln(const Expr& u) {
    if (u->kind() == Kind::Mul) {
        std::vector<Expr> parts;
        for (const Expr& f : u->args()) parts.push_back(expand_ln(f));
        return make_add(std::move(parts));
    }
    if (u->kind() == Kind::Pow) {
        return make_mul({u->arg(1), expand_ln(u->arg(0))});
    }
    return make_fn(FunctionId::Ln, u);
}

Expr expand_rec(const Expr& e) {
    switch (e->kind()) {
        case Kind::Number:
        case Kind::Symbol:
        case Kind::Constant:
            return e;
        case Kind::Add: {
            std::vector<Expr> out;
            for (const Expr& a : e->args()) out.push_back(expand_rec(a));
            return make_add(std::move(out));
        }
        case Kind::Mul: {
            std::vector<Expr> out;
            for (const Expr& a : e->args()) out.push_back(expand_rec(a));
            return make_mul(std::move(out));
        }
        case Kind::Pow:
            return make_pow(expand_rec(e->arg(0)), expand_rec(e->arg(1)));
        case Kind::Function:
            if (is_ln(e)) return expand_ln(expand_rec(e->arg(0)));
            return make_fn(e->function(), expand_rec(e->arg(0)));
    }
    return e;
}

} // namespace

Expr log_expand(const Expr& e) {
    return simplify(expand_rec(simplify(e)));
}

Expr log_combine(const Expr& e) {
    const Expr ex = expand(simplify(e));
    const std::vector<Expr> terms =
        ex->kind() == Kind::Add ? ex->args() : std::vector<Expr>{ex};

    std::vector<std::pair<Expr, Expr>> logs; // (u, coefficient) for coeff·ln(u)
    std::vector<Expr> others;
    for (const Expr& term : terms) {
        const std::vector<Expr> factors =
            term->kind() == Kind::Mul ? term->args() : std::vector<Expr>{term};
        std::vector<Expr> rest;
        Expr arg;
        int ln_count = 0;
        for (const Expr& f : factors) {
            if (is_ln(f)) {
                ++ln_count;
                arg = f->arg(0);
            } else {
                rest.push_back(f);
            }
        }
        if (ln_count == 1) {
            const Expr coeff = rest.empty()      ? make_num(1)
                               : rest.size() == 1 ? rest[0]
                                                  : make_mul(rest);
            logs.emplace_back(arg, coeff);
        } else {
            others.push_back(term);
        }
    }

    if (logs.empty()) return simplify(e);

    // Combine into ln( Π u^coeff ).
    std::vector<Expr> product;
    for (const auto& [u, coeff] : logs) product.push_back(make_pow(u, coeff));
    const Expr combined = make_fn(FunctionId::Ln, simplify(make_mul(product)));

    std::vector<Expr> sum{combined};
    for (const Expr& o : others) sum.push_back(o);
    return simplify(make_add(std::move(sum)));
}

} // namespace mathsolver
