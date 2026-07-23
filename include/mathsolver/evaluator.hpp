#pragma once

#include <complex>
#include <map>
#include <optional>
#include <string>

#include "mathsolver/errors.hpp"
#include "mathsolver/expr.hpp"

namespace mathsolver {

/// Symbol name -> value. Transparent comparator so string_view lookups work.
using Bindings = std::map<std::string, double, std::less<>>;

/// Numerically evaluate `e` with the given variable bindings.
/// Throws EvalError on: an unbound symbol (message names it), division by
/// zero, domain errors (ln(x<=0), asin/acos outside [-1,1], negative base
/// with non-integer exponent, 0^negative), or a non-finite result.
/// Real domain only.
double evaluate(const Expr& e, const Bindings& bindings = {});

/// Symbol name -> complex value.
using ComplexBindings = std::map<std::string, std::complex<double>, std::less<>>;

/// Complex-domain numeric evaluation (principal branch for multivalued
/// functions). Runs alongside `evaluate`, which stays real-only — the
/// solver/integrator verification paths never call this one. `i` evaluates to
/// the imaginary unit; `abs` returns the modulus. The special functions that
/// have no complex implementation (gamma, digamma, erf/erfc, fib, harmonic)
/// are evaluated on the real part when the argument is (numerically) real, and
/// otherwise throw. Throws EvalError on an unbound symbol or a non-finite
/// result.
std::complex<double> evaluate_complex(const Expr& e, const ComplexBindings& bindings = {});

/// Fold a symbol-free, constant-free expression to an exact Rational if
/// every step stays rational (integer powers, exact rational roots such as
/// 4^(1/2)). Returns nullopt otherwise — including when the expression
/// contains symbols/constants or an intermediate overflows (overflow is
/// caught internally, never thrown from here).
std::optional<Rational> try_exact_numeric(const Expr& e);

} // namespace mathsolver
