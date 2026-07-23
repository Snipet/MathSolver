#pragma once

#include "mathsolver/expr.hpp"

namespace mathsolver {

/// Expand trigonometric functions of sums and integer multiples into products
/// and powers of single-angle trig, then simplify: `sin(a + b)` becomes
/// `sin(a) cos(b) + cos(a) sin(b)`, `cos(2x)` becomes `cos(x)² - sin(x)²`,
/// `sin(3x)` its cubic form, and so on. Non-integer or symbolic multiples
/// (`sin(x/2)`, `sin(a x)`) and non-trig subexpressions are left untouched.
Expr trig_expand(const Expr& e);

/// The inverse of `trig_expand`: rewrite products and powers of sines and
/// cosines into a linear combination of sines and cosines of multiple angles.
/// `sin(x)²` becomes `1/2 - cos(2x)/2`, `2 sin(x) cos(x)` becomes `sin(2x)`,
/// and `sin(x) sin(y)` becomes `cos(x - y)/2 - cos(x + y)/2`. Exact, via the
/// complex-exponential form; non-trig factors ride along as coefficients.
Expr trig_reduce(const Expr& e);

} // namespace mathsolver
