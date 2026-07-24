#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mathsolver/expr.hpp"

namespace mathsolver {

/// Regression model families.
///   Poly  — least-squares polynomial of the given degree. Solved EXACTLY over
///           the rationals (rational normal equations) when every data value is
///           rational; falls back to double least squares on overflow or
///           non-rational data.
///   Exp   — y = a·e^(b·x)   (linearized: ln y = ln a + b·x; needs y > 0).
///   Power — y = a·x^b       (linearized: ln y = ln a + b·ln x; needs x,y > 0).
///   Log   — y = a + b·ln x  (linearized on ln x; needs x > 0).
enum class FitModel { Poly, Exp, Power, Log };

struct FitResult {
    enum class Status { Ok, Error };

    Status status = Status::Error;
    /// The fitted model as a plottable expression in `variable` (valid iff Ok).
    Expr expr;
    std::string variable = "x";
    /// True when the polynomial fit was solved exactly over the rationals.
    bool exact = false;
    /// Coefficient of determination on the original y scale (0..1, may be < 0).
    double r2 = 0.0;
    /// Number of data points used.
    int n = 0;
    /// Human label of the model actually used, e.g. "quadratic", "exponential".
    std::string model;
    /// Error text when Status::Error.
    std::string message;
};

/// Fit `ys` against `xs` (parallel arrays of coordinate source strings, each
/// parsed as a constant expression) with the given model. `degree` applies to
/// Poly only. `variable` names the free symbol of the returned expression.
FitResult fit(const std::vector<std::string>& xs, const std::vector<std::string>& ys,
              FitModel model, int degree, std::string_view variable = "x");

/// Exact polynomial interpolation: the unique polynomial of degree ≤ n−1
/// through n points (distinct x). Unlike `fit` (least squares), it passes
/// through every point. Solved exactly over the rationals when the data are
/// rational; falls back to double for non-rational data.
struct InterpResult {
    enum class Status { Ok, Error };

    Status status = Status::Error;
    /// The interpolating polynomial in `variable` (valid iff Ok).
    Expr expr;
    std::string variable = "x";
    /// True when solved exactly over the rationals.
    bool exact = false;
    /// Number of data points.
    int n = 0;
    /// Degree of the interpolating polynomial (≤ n−1 after simplification).
    int degree = 0;
    /// Error text when Status::Error.
    std::string message;
};

/// Interpolate `ys` at `xs` (parallel coordinate-string arrays) with a
/// polynomial in `variable`. Requires ≥ 1 point and distinct x values.
InterpResult interp(const std::vector<std::string>& xs, const std::vector<std::string>& ys,
                    std::string_view variable = "x");

/// A structured presentation of the same interpolating polynomial `interp`
/// builds — kept factored (never expanded) so the construction is visible.
///   Newton:   c₀ + c₁(x−x₀) + c₂(x−x₀)(x−x₁) + …   (cₖ = divided differences)
///   Lagrange: Σ wᵢ · Π_{j≠i}(x − xⱼ)               (wᵢ = yᵢ / Π_{j≠i}(xᵢ − xⱼ))
/// Exact over the rationals when the data are rational (falls back to double).
enum class InterpForm { Newton, Lagrange };

struct InterpFormResult {
    enum class Status { Ok, Error };
    Status status = Status::Error;
    Expr expr;                       ///< the factored form (valid iff Ok)
    std::string variable = "x";
    bool exact = false;              ///< solved exactly over the rationals
    int n = 0;                       ///< number of data points
    std::vector<std::string> notes;  ///< the constants (divided differences / weights)
    std::string message;
};

InterpFormResult interp_form(const std::vector<std::string>& xs,
                             const std::vector<std::string>& ys,
                             std::string_view variable, InterpForm form);

/// Parse a model name to (family, default degree). Poly aliases fix a degree;
/// the generic "poly"/"polynomial" returns degree -1 (caller supplies one).
/// Returns nullopt for an unknown name.
std::optional<std::pair<FitModel, int>> parse_fit_model(std::string_view name);

/// Split a data blob — records separated by ';' or newlines, each "x,y" — into
/// parallel coordinate-string arrays. Throws ParseError on a malformed record.
std::pair<std::vector<std::string>, std::vector<std::string>> parse_point_data(
    std::string_view data);

/// The square Vandermonde matrix of a node list x_0, …, x_{n-1}: the n×n matrix
/// whose row i is (1, x_i, x_i², …, x_i^{n-1}). It is the coefficient matrix of
/// the polynomial-interpolation linear system (`interp` solves V·c = y), and its
/// determinant is ∏_{i<j}(x_j − x_i). Exact over the rationals; symbolic nodes
/// are kept symbolic. Requires ≥ 1 node.
struct VandermondeResult {
    enum class Status { Ok, Empty };
    Status status = Status::Empty;
    std::vector<std::vector<Expr>> matrix;  ///< n×n, row-major
    std::string message;
};

VandermondeResult vandermonde_matrix(const std::vector<Expr>& nodes);

} // namespace mathsolver
