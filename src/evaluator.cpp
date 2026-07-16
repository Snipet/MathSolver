#include "mathsolver/evaluator.hpp"

#include <cmath>
#include <format>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <vector>

#include "mathsolver/errors.hpp"
#include "mathsolver/expr.hpp"
#include "mathsolver/rational.hpp"

namespace mathsolver {
namespace {

double check_finite(double value) {
    if (!std::isfinite(value)) {
        throw EvalError("non-finite result");
    }
    return value;
}

double eval_pow(double base, double exponent) {
    if (base == 0.0) {
        if (exponent < 0.0) {
            throw EvalError("division by zero (0 raised to a negative power)");
        }
        return exponent == 0.0 ? 1.0 : 0.0; // 0^0 == 1 by convention (DESIGN.md §2)
    }
    if (base < 0.0 && exponent != std::trunc(exponent)) {
        throw EvalError("negative base with non-integer exponent");
    }
    return std::pow(base, exponent);
}

double eval_function(FunctionId id, double u) {
    switch (id) {
        case FunctionId::Sin: return std::sin(u);
        case FunctionId::Cos: return std::cos(u);
        case FunctionId::Tan: return std::tan(u);
        case FunctionId::Asin:
            if (u < -1.0 || u > 1.0) {
                throw EvalError(std::format("asin argument {} outside [-1, 1]", u));
            }
            return std::asin(u);
        case FunctionId::Acos:
            if (u < -1.0 || u > 1.0) {
                throw EvalError(std::format("acos argument {} outside [-1, 1]", u));
            }
            return std::acos(u);
        case FunctionId::Atan: return std::atan(u);
        case FunctionId::Sinh: return std::sinh(u);
        case FunctionId::Cosh: return std::cosh(u);
        case FunctionId::Tanh: return std::tanh(u);
        case FunctionId::Ln:
            if (u <= 0.0) {
                throw EvalError(std::format("ln of a non-positive value ({})", u));
            }
            return std::log(u);
        case FunctionId::Abs: return std::fabs(u);
    }
    throw std::logic_error("evaluate: invalid FunctionId");
}

/// Exact fold. Reuses the n-ary factory folding (order-independent 128-bit
/// accumulation, exact rational roots); OverflowError/DivisionByZeroError
/// escaping the factories are caught by the public entry point below.
std::optional<Rational> fold_exact(const Expr& e) {
    switch (e->kind()) {
        case Kind::Number:
            return e->number();
        case Kind::Symbol:
        case Kind::Constant:
            return std::nullopt;
        case Kind::Add:
        case Kind::Mul: {
            std::vector<Expr> numbers;
            numbers.reserve(e->args().size());
            for (const Expr& a : e->args()) {
                const auto v = fold_exact(a);
                if (!v) {
                    return std::nullopt;
                }
                numbers.push_back(make_num(*v));
            }
            // All-Number args always fold to a single Number node.
            const Expr folded = e->kind() == Kind::Add ? make_add(std::move(numbers))
                                                       : make_mul(std::move(numbers));
            return folded->number();
        }
        case Kind::Pow: {
            const auto base = fold_exact(e->arg(0));
            const auto exponent = fold_exact(e->arg(1));
            if (!base || !exponent) {
                return std::nullopt;
            }
            const Expr folded = make_pow(make_num(*base), make_num(*exponent));
            if (folded->kind() == Kind::Number) {
                return folded->number();
            }
            return std::nullopt; // irrational (e.g. 2^(1/2)) or stayed symbolic on overflow
        }
        case Kind::Function: {
            if (e->function() != FunctionId::Abs) {
                return std::nullopt; // sin/ln/... values are simplify's job, not exact folds
            }
            const auto v = fold_exact(e->arg(0));
            if (!v) {
                return std::nullopt;
            }
            return v->is_negative() ? -*v : *v; // -LLONG_MIN overflows -> caught below
        }
    }
    throw std::logic_error("try_exact_numeric: invalid Kind");
}

} // namespace

double evaluate(const Expr& e, const Bindings& bindings) {
    switch (e->kind()) {
        case Kind::Number:
            return e->number().to_double();
        case Kind::Symbol: {
            const auto it = bindings.find(e->symbol_name());
            if (it == bindings.end()) {
                throw EvalError(std::format("unbound symbol '{}'", e->symbol_name()));
            }
            return check_finite(it->second);
        }
        case Kind::Constant:
            return e->constant() == ConstantId::Pi ? std::numbers::pi : std::numbers::e;
        case Kind::Add: {
            double sum = 0.0;
            for (const Expr& a : e->args()) {
                sum += evaluate(a, bindings);
            }
            return check_finite(sum);
        }
        case Kind::Mul: {
            double product = 1.0;
            for (const Expr& a : e->args()) {
                product *= evaluate(a, bindings);
            }
            return check_finite(product);
        }
        case Kind::Pow:
            return check_finite(eval_pow(evaluate(e->arg(0), bindings),
                                         evaluate(e->arg(1), bindings)));
        case Kind::Function:
            return check_finite(eval_function(e->function(), evaluate(e->arg(0), bindings)));
    }
    throw std::logic_error("evaluate: invalid Kind");
}

std::optional<Rational> try_exact_numeric(const Expr& e) {
    try {
        return fold_exact(e);
    } catch (const OverflowError&) {
        return std::nullopt;
    } catch (const DivisionByZeroError&) {
        return std::nullopt;
    }
}

} // namespace mathsolver
