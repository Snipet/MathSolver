#include "mathsolver/steps.hpp"

#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mathsolver/derivative.hpp"
#include "mathsolver/errors.hpp"
#include "mathsolver/expr.hpp"
#include "mathsolver/printer.hpp"
#include "mathsolver/rational.hpp"
#include "mathsolver/simplify.hpp"

namespace mathsolver {
namespace {

bool is_zero(const Expr& e) {
    return e->kind() == Kind::Number && e->number().is_zero();
}

Expr one_minus_square(const Expr& u) {
    return make_sub(make_num(1), make_pow(u, make_num(2)));
}

/// f'(u) per the DESIGN.md §8 chain-rule table (mirrors derivative.cpp).
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
        return make_pow(make_add({make_pow(u, make_num(2)), make_num(1)}),
                        make_num(Rational(-1, 2)));
    case FunctionId::Acosh:
        return make_pow(make_add({make_pow(u, make_num(2)), make_num(-1)}),
                        make_num(Rational(-1, 2)));
    case FunctionId::Atanh:
        return make_pow(make_sub(make_num(1), make_pow(u, make_num(2))), make_num(-1));
    case FunctionId::Gamma:
        return make_mul({make_fn(FunctionId::Gamma, u), make_fn(FunctionId::Digamma, u)});
    case FunctionId::Digamma:
        throw Error("differentiate: the derivative of digamma needs polygamma, "
                    "which is not supported");
    case FunctionId::Erf:
        return make_mul({make_num(2),
                         make_pow(make_const(ConstantId::Pi), make_num(Rational(-1, 2))),
                         make_pow(make_const(ConstantId::E), make_neg(make_pow(u, make_num(2))))});
    case FunctionId::Erfc:
        return make_mul({make_num(-2),
                         make_pow(make_const(ConstantId::Pi), make_num(Rational(-1, 2))),
                         make_pow(make_const(ConstantId::E), make_neg(make_pow(u, make_num(2))))});
    case FunctionId::Fib:
        throw Error("differentiate: fib is a discrete sequence, not a "
                    "differentiable function");
    case FunctionId::Harmonic:
        throw Error("differentiate: the derivative of harmonic needs "
                    "trigamma, which is not supported");
    case FunctionId::Ln:
        return make_pow(u, make_num(-1));
    case FunctionId::Abs:
        return make_mul({u, make_pow(make_fn(FunctionId::Abs, u), make_num(-1))});
    case FunctionId::Conj:
    case FunctionId::Re:
    case FunctionId::Im:
    case FunctionId::Arg:
        throw Error(std::format(
            "differentiate: {} is not analytic; its derivative is not supported",
            function_name(id)));
    }
    throw Error("differentiate: unknown FunctionId");
}

/// Push a step "d/d<sym>(before) = <simplified after>" in both renderings.
void record(std::vector<ExplainStep>& steps, std::string rule, std::string_view sym,
            const Expr& before, const Expr& after) {
    const Expr shown = simplify(after);
    ExplainStep s;
    s.rule = std::move(rule);
    s.plain = std::format("d/d{0}({1}) = {2}", sym, to_string(before, PrintStyle::Plain),
                          to_string(shown, PrintStyle::Plain));
    s.latex = std::format("\\frac{{d}}{{d{0}}}\\left({1}\\right) = {2}", sym,
                          to_string(before, PrintStyle::LaTeX), to_string(shown, PrintStyle::LaTeX));
    steps.push_back(std::move(s));
}

/// Mirror derivative.cpp's `diff`, recording a step per rule application. Steps
/// accrue innermost-first (children before their parent). The returned Expr is
/// the raw (unsimplified) derivative, structurally identical to `diff`'s.
Expr rec(const Expr& e, std::string_view sym, std::vector<ExplainStep>& steps) {
    if (!contains_symbol(e, sym)) {
        return make_num(0);
    }
    switch (e->kind()) {
    case Kind::Symbol:
        return make_num(1); // the enclosing rule shows the concrete result

    case Kind::Add: {
        std::vector<Expr> terms;
        terms.reserve(e->args().size());
        for (const Expr& t : e->args()) terms.push_back(rec(t, sym, steps));
        Expr res = make_add(std::move(terms));
        record(steps, "sum rule", sym, e, res);
        return res;
    }

    case Kind::Mul: {
        const std::vector<Expr>& f = e->args();
        std::size_t nvar = 0;
        for (const Expr& g : f)
            if (contains_symbol(g, sym)) ++nvar;
        std::vector<Expr> outer;
        for (std::size_t i = 0; i < f.size(); ++i) {
            Expr di = rec(f[i], sym, steps);
            if (is_zero(di)) continue;
            std::vector<Expr> prod;
            prod.reserve(f.size());
            prod.push_back(std::move(di));
            for (std::size_t j = 0; j < f.size(); ++j)
                if (j != i) prod.push_back(f[j]);
            outer.push_back(make_mul(std::move(prod)));
        }
        Expr res = make_add(std::move(outer));
        record(steps, nvar <= 1 ? "constant multiple rule" : "product rule", sym, e, res);
        return res;
    }

    case Kind::Pow: {
        const Expr& base = e->arg(0);
        const Expr& expo = e->arg(1);
        // d(e^v) = e^v * v'.
        if (base->kind() == Kind::Constant && base->constant() == ConstantId::E) {
            Expr res = make_mul({e, rec(expo, sym, steps)});
            record(steps, "exponential rule", sym, e, res);
            return res;
        }
        // Constant exponent: d(u^c) = c * u^(c-1) * u'.
        if (!contains_symbol(expo, sym)) {
            Expr du = rec(base, sym, steps);
            Expr res = is_zero(du)
                           ? make_num(0)
                           : make_mul({expo, make_pow(base, make_sub(expo, make_num(1))),
                                       std::move(du)});
            record(steps, "power rule", sym, e, res);
            return res;
        }
        // General: d(u^v) = u^v * (v'*ln(u) + v*u'/u).
        Expr du = rec(base, sym, steps);
        Expr dv = rec(expo, sym, steps);
        Expr log_part = make_mul({std::move(dv), make_fn(FunctionId::Ln, base)});
        Expr ratio_part = is_zero(du)
                              ? make_num(0)
                              : make_mul({expo, std::move(du), make_pow(base, make_num(-1))});
        Expr res = make_mul({e, make_add({std::move(log_part), std::move(ratio_part)})});
        record(steps, "general power rule", sym, e, res);
        return res;
    }

    case Kind::Function: {
        const Expr& u = e->arg(0);
        Expr du = rec(u, sym, steps);
        Expr res = is_zero(du) ? make_num(0) : make_mul({chain_factor(e->function(), u), du});
        record(steps, "chain rule", sym, e, res);
        return res;
    }

    case Kind::Number:
    case Kind::Constant:
        break; // unreachable: contains_symbol is false for these kinds
    }
    return make_num(0);
}

} // namespace

Explanation explain_derivative(const Expr& e, std::string_view symbol) {
    std::vector<ExplainStep> steps;
    const Expr raw = rec(e, symbol, steps);
    const Expr result = simplify(raw);
    // A bare variable or a constant produces no rule step; record the trivial one.
    if (steps.empty()) {
        record(steps, contains_symbol(e, symbol) ? "identity rule" : "constant rule", symbol, e,
               result);
    }
    Explanation ex;
    ex.steps = std::move(steps);
    ex.result_plain = to_string(result, PrintStyle::Plain);
    ex.result_latex = to_string(result, PrintStyle::LaTeX);
    return ex;
}

} // namespace mathsolver
