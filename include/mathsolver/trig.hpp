#pragma once

#include "mathsolver/expr.hpp"

namespace mathsolver {

/// Expand trigonometric functions of sums and integer multiples into products
/// and powers of single-angle trig, then simplify: `sin(a + b)` becomes
/// `sin(a) cos(b) + cos(a) sin(b)`, `cos(2x)` becomes `cos(x)² - sin(x)²`,
/// `sin(3x)` its cubic form, and so on. Non-integer or symbolic multiples
/// (`sin(x/2)`, `sin(a x)`) and non-trig subexpressions are left untouched.
Expr trig_expand(const Expr& e);

} // namespace mathsolver
