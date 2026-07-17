// PDE separation-of-variables numerics (pde_core.hpp).

#include "pde_core.hpp"

#include <cmath>
#include <format>
#include <numbers>

#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/expr.hpp"
#include "mathsolver/integrate.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/printer.hpp"
#include "mathsolver/simplify.hpp"

namespace mathsolver::plugins::pde {

SineSeries sine_coefficients(const Expr& f, const std::string& var,
                             double length, int modes) {
    if (!(length > 0.0)) {
        throw Error("the interval length L must be positive");
    }
    if (modes < 1 || modes > 64) {
        throw Error("mode count must be in [1, 64]");
    }
    for (const std::string& s : free_symbols(f)) {
        if (s != var) {
            throw Error(std::format(
                "the profile may only use the variable {} (found '{}')", var,
                s));
        }
    }
    SineSeries out;
    // Rational approximation of L for the exact-integration attempt; the
    // sine argument is built as n*pi*x / L.
    for (int n = 1; n <= modes; ++n) {
        const Expr arg = simplify(make_div(
            make_mul({make_num(n), make_const(ConstantId::Pi), make_sym(var)}),
            parse_expression(std::format("{:.12g}", length))));
        const Expr integrand =
            simplify(make_mul({f, make_fn(FunctionId::Sin, arg)}));
        const DefiniteIntegralResult r = integrate_definite(
            integrand, var, make_num(0),
            parse_expression(std::format("{:.12g}", length)));
        double value = 0.0;
        switch (r.status) {
            case DefiniteIntegralResult::Status::Exact:
                out.exact_count += 1;
                [[fallthrough]];
            case DefiniteIntegralResult::Status::Numeric:
                value = evaluate(r.value, Bindings{});
                break;
            case DefiniteIntegralResult::Status::Unsolved:
                throw Error(std::format(
                    "cannot integrate the profile against mode {}: {}", n,
                    r.warnings.empty() ? "integration failed"
                                       : r.warnings.front()));
        }
        out.b.push_back(2.0 / length * value);
    }
    return out;
}

double heat_eval(const SineSeries& s, double length, double alpha, double x,
                 double t) {
    double u = 0.0;
    for (std::size_t k = 0; k < s.b.size(); ++k) {
        const double n = static_cast<double>(k + 1);
        const double w = n * std::numbers::pi / length;
        u += s.b[k] * std::sin(w * x) * std::exp(-alpha * w * w * t);
    }
    return u;
}

double wave_eval(const SineSeries& a, const SineSeries& g, double length,
                 double c, double x, double t) {
    double u = 0.0;
    const std::size_t modes = std::max(a.b.size(), g.b.size());
    for (std::size_t k = 0; k < modes; ++k) {
        const double n = static_cast<double>(k + 1);
        const double kx = n * std::numbers::pi / length;
        const double w = kx * c;
        const double an = k < a.b.size() ? a.b[k] : 0.0;
        const double gn = k < g.b.size() ? g.b[k] : 0.0;
        u += std::sin(kx * x) *
             (an * std::cos(w * t) + (w > 0 ? gn / w * std::sin(w * t) : 0.0));
    }
    return u;
}

} // namespace mathsolver::plugins::pde
