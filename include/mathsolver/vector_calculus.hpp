#pragma once

// Multivariate and vector calculus: differential operators built on the
// scalar derivative, plus rendering helpers for vectors and matrices.

#include <string>
#include <string_view>
#include <vector>

#include "mathsolver/expr.hpp"
#include "mathsolver/printer.hpp"

namespace mathsolver {

/// A column vector / matrix of expressions (row-major for matrices).
using ExprVec = std::vector<Expr>;
using ExprMat = std::vector<std::vector<Expr>>;

/// ∇f = (∂f/∂x_i): one entry per variable, simplified.
ExprVec gradient(const Expr& f, const std::vector<std::string>& vars);

/// ∇·F = Σ ∂F_i/∂x_i. `field` and `vars` must have equal length.
Expr divergence(const ExprVec& field, const std::vector<std::string>& vars);

/// ∇×F for a 3-component field over 3 variables (the vector curl).
ExprVec curl(const ExprVec& field, const std::vector<std::string>& vars);

/// Scalar (2-D) curl ∂F_y/∂x − ∂F_x/∂y for a 2-component field.
Expr curl2d(const ExprVec& field, const std::vector<std::string>& vars);

/// ∇²f = Σ ∂²f/∂x_i² (the scalar Laplacian).
Expr laplacian(const Expr& f, const std::vector<std::string>& vars);

/// Jacobian J_ij = ∂F_i/∂x_j (rows = components, cols = variables).
ExprMat jacobian(const ExprVec& field, const std::vector<std::string>& vars);

/// Hessian H_ij = ∂²f/∂x_i∂x_j (symmetric).
ExprMat hessian(const Expr& f, const std::vector<std::string>& vars);

/// Directional derivative ∇f·d̂ (the direction is normalized when it has a
/// numeric norm; left un-normalized for symbolic directions).
Expr directional_derivative(const Expr& f, const std::vector<std::string>& vars,
                            const ExprVec& direction);

// --- rendering --------------------------------------------------------------

/// A vector as "(a, b, c)" (Plain) or a LaTeX pmatrix column.
std::string vec_to_string(const ExprVec& v, PrintStyle style);

/// A matrix as row-per-line "[a, b; c, d]" (Plain) or a LaTeX pmatrix.
std::string mat_to_string(const ExprMat& m, PrintStyle style);

} // namespace mathsolver
