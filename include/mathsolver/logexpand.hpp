#pragma once

#include "mathsolver/expr.hpp"

namespace mathsolver {

/// Expand logarithms of products, quotients and powers into sums and multiples
/// of logarithms: `ln(x·y)` → `ln(x) + ln(y)`, `ln(x^n)` → `n·ln(x)`,
/// `ln(x/y)` → `ln(x) - ln(y)`. A formal rewrite (valid for positive
/// arguments), leaving atomic logarithms and non-logarithmic parts untouched.
Expr log_expand(const Expr& e);

/// The inverse: collect a sum of logarithm terms into a single logarithm.
/// `ln(x) + ln(y)` → `ln(x·y)`, `n·ln(x)` → `ln(x^n)`, `ln(x) - ln(y)` →
/// `ln(x/y)`. Non-logarithmic terms are kept as they are.
Expr log_combine(const Expr& e);

} // namespace mathsolver
