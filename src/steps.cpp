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
#include "mathsolver/integrate.hpp"
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

// ---------------------------------------------------------------------------
// Integral steps (§8b): mirror the integrator's outermost structural choices
// (linearity over a sum, pulling constant factors out) and let the real
// `integrate` solve each leaf, tagging the step with the method it reports.
// The recorder never changes the answer — every leaf is the engine's own.
// ---------------------------------------------------------------------------

/// Push a step "∫(before) d<sym> = <after>" in both renderings.
void record_int(std::vector<ExplainStep>& steps, std::string rule, std::string_view sym,
                const Expr& integrand, const Expr& anti) {
    ExplainStep s;
    s.rule = std::move(rule);
    s.plain = std::format("∫ {1} d{0} = {2}", sym, to_string(integrand, PrintStyle::Plain),
                          to_string(anti, PrintStyle::Plain));
    s.latex = std::format("\\int {1}\\, d{0} = {2}", sym, to_string(integrand, PrintStyle::LaTeX),
                          to_string(anti, PrintStyle::LaTeX));
    steps.push_back(std::move(s));
}

/// The engine's antiderivative of `e` d`sym`, or throw when it has no
/// elementary form — mirroring how the plain verb reports Unsolved.
Expr must_integrate(const Expr& e, const std::string& sym, std::string* method) {
    const IntegrateResult r = integrate(e, sym);
    if (r.status != IntegrateResult::Status::Integrated) {
        throw Error("steps: this integral has no elementary antiderivative");
    }
    if (method) *method = r.method;
    return r.antiderivative;
}

/// Record steps for ∫ e d`sym`, innermost-first. Sums split by linearity and
/// constant factors peel off; each remaining leaf is solved by `integrate` and
/// labelled with its reported method. Throws when any piece is not integrable.
void rec_integral(const Expr& e_in, const std::string& sym,
                  std::vector<ExplainStep>& steps) {
    const Expr e = simplify(e_in);

    // ∫ c d<sym> = c*<sym> for a symbol-free integrand.
    if (!contains_symbol(e, sym)) {
        record_int(steps, "constant rule", sym, e,
                   simplify(make_mul({e, make_sym(std::string(sym))})));
        return;
    }

    // Linearity: ∫(f + g + ...) = ∫f + ∫g + ... — record each term, then the sum.
    if (e->kind() == Kind::Add) {
        for (const Expr& term : e->args()) rec_integral(term, sym, steps);
        record_int(steps, "linearity", sym, e, must_integrate(e, sym, nullptr));
        return;
    }

    // Constant multiple: ∫ c*f = c ∫ f — peel every symbol-free factor out.
    if (e->kind() == Kind::Mul) {
        std::vector<Expr> free_part;
        std::vector<Expr> dep_part;
        for (const Expr& f : e->args())
            (contains_symbol(f, sym) ? dep_part : free_part).push_back(f);
        if (!free_part.empty() && !dep_part.empty()) {
            rec_integral(make_mul(std::move(dep_part)), sym, steps);
            record_int(steps, "constant multiple", sym, e, must_integrate(e, sym, nullptr));
            return;
        }
    }

    // Leaf: the engine integrates it and names the technique it used.
    std::string method;
    const Expr anti = must_integrate(e, sym, &method);
    record_int(steps, method.empty() ? "table" : method, sym, e, anti);
}

} // namespace

Explanation explain_integral(const Expr& e, std::string_view symbol) {
    const std::string sym(symbol);
    // The verified whole-integral answer (throws if there is no elementary form).
    const Expr F = must_integrate(e, sym, nullptr);
    std::vector<ExplainStep> steps;
    rec_integral(e, sym, steps);
    Explanation ex;
    ex.steps = std::move(steps);
    ex.result_plain = to_string(F, PrintStyle::Plain);
    ex.result_latex = to_string(F, PrintStyle::LaTeX);
    return ex;
}

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
