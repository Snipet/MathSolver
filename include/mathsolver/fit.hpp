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

/// Parse a model name to (family, default degree). Poly aliases fix a degree;
/// the generic "poly"/"polynomial" returns degree -1 (caller supplies one).
/// Returns nullopt for an unknown name.
std::optional<std::pair<FitModel, int>> parse_fit_model(std::string_view name);

/// Split a data blob — records separated by ';' or newlines, each "x,y" — into
/// parallel coordinate-string arrays. Throws ParseError on a malformed record.
std::pair<std::vector<std::string>, std::vector<std::string>> parse_point_data(
    std::string_view data);

} // namespace mathsolver
