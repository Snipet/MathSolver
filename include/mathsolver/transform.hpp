#pragma once

// Symbolic Laplace and inverse-Laplace transforms (DESIGN.md §11).
//
// laplace(f, t) maps a time-domain expression f(t) to F(s); inverse_laplace
// maps F(s) back to f(t). Both are table-driven, composed from three
// theorems so a broad set of forms works without a case for each:
//
//   - base table:     1, t^n, sin/cos/sinh/cosh
//   - s-shift:        L{e^{a t} g(t)} = G(s - a)          (any symbolic a)
//   - freq. deriv.:   L{t^n g(t)} = (-1)^n d^n/ds^n G(s)
//
// so e.g. t^2 e^{-3t} sin(2t) transforms without a dedicated rule. Inputs
// outside the supported table throw Error with a specific message.

#include <string_view>

#include "mathsolver/expr.hpp"

namespace mathsolver {

/// L{f(t)}(s). `time` is the time variable (must not be "s"; the result is
/// expressed in s). Throws Error if a subterm has no known transform.
Expr laplace(const Expr& f, std::string_view time = "t");

/// L^{-1}{F(s)}(t). `svar` is the frequency variable (must not be "t"; the
/// result is expressed in t). Handles sums of table forms: c/(s-a)^n
/// (including a = 0) and (linear)/((s-a)^2 + w^2). Throws Error otherwise.
Expr inverse_laplace(const Expr& F, std::string_view svar = "s");

} // namespace mathsolver
