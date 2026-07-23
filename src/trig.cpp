#include "mathsolver/trig.hpp"

#include <utility>
#include <vector>

#include "mathsolver/simplify.hpp"

namespace mathsolver {

namespace {

/// Split an angle into a list of atoms to be summed, expanding positive/negative
/// integer multiples into repeated copies (2x → x, x; -3y → -y, -y, -y). A
/// non-integer or symbolic multiple, or a constant, stays a single atom.
std::vector<Expr> angle_atoms(const Expr& angle) {
    const Expr t = simplify(angle);
    const std::vector<Expr> terms =
        t->kind() == Kind::Add ? t->args() : std::vector<Expr>{t};

    std::vector<Expr> out;
    for (const Expr& term : terms) {
        long long coeff = 1;
        Expr base = term;
        if (term->kind() == Kind::Mul) {
            std::vector<Expr> rest;
            bool found = false;
            for (const Expr& f : term->args()) {
                if (!found && f->kind() == Kind::Number && f->number().is_integer()) {
                    coeff = f->number().num();
                    found = true;
                } else {
                    rest.push_back(f);
                }
            }
            if (found) {
                base = rest.empty()      ? make_num(1)
                       : rest.size() == 1 ? rest[0]
                                          : make_mul(rest);
            }
        }
        // Only expand an integer multiple (|coeff| >= 2) of a non-constant base;
        // fold the sign into each repeated copy so simplify's parity rules
        // (sin(-u) = -sin u) tidy the result.
        if ((coeff >= 2 || coeff <= -2) && base->kind() != Kind::Number) {
            const Expr unit = coeff < 0 ? simplify(make_mul({make_num(-1), base})) : base;
            for (long long i = 0; i < (coeff < 0 ? -coeff : coeff); ++i) {
                out.push_back(unit);
            }
        } else {
            out.push_back(term);
        }
    }
    return out;
}

/// (sin, cos) of the sum of `atoms[i..]`, built purely from the addition
/// formulas — no simplification here (the caller simplifies once at the end).
std::pair<Expr, Expr> sin_cos_of(const std::vector<Expr>& atoms, std::size_t i) {
    if (i >= atoms.size()) return {make_num(0), make_num(1)}; // sin 0 = 0, cos 0 = 1
    const Expr sA = make_fn(FunctionId::Sin, atoms[i]);
    const Expr cA = make_fn(FunctionId::Cos, atoms[i]);
    if (i + 1 == atoms.size()) return {sA, cA};
    const auto [sB, cB] = sin_cos_of(atoms, i + 1);
    // sin(A+B) = sinA cosB + cosA sinB;  cos(A+B) = cosA cosB - sinA sinB.
    return {make_add({make_mul({sA, cB}), make_mul({cA, sB})}),
            make_sub(make_mul({cA, cB}), make_mul({sA, sB}))};
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
        case Kind::Function: {
            const Expr arg = expand_rec(e->arg(0));
            const FunctionId id = e->function();
            if (id == FunctionId::Sin || id == FunctionId::Cos || id == FunctionId::Tan) {
                const std::vector<Expr> atoms = angle_atoms(arg);
                const auto [s, c] = sin_cos_of(atoms, 0);
                if (id == FunctionId::Sin) return s;
                if (id == FunctionId::Cos) return c;
                return make_div(s, c); // tan = sin / cos
            }
            return make_fn(id, arg);
        }
    }
    return e;
}

} // namespace

Expr trig_expand(const Expr& e) {
    return simplify(expand_rec(simplify(e)));
}

} // namespace mathsolver
