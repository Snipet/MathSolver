#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "mathsolver/expr.hpp"

namespace mathsolver {

/// The classical families of orthogonal polynomials generated exactly here.
enum class OrthoFamily {
    ChebyshevT, ///< first kind:  T0=1, T1=x,   T_{k+1}=2x·T_k − T_{k−1}
    ChebyshevU, ///< second kind: U0=1, U1=2x,  U_{k+1}=2x·U_k − U_{k−1}
    Legendre,   ///< P0=1, P1=x,  (k+1)P_{k+1}=(2k+1)x·P_k − k·P_{k−1}
    Hermite,    ///< physicists': H0=1, H1=2x,  H_{k+1}=2x·H_k − 2k·H_{k−1}
    Laguerre,   ///< L0=1, L1=1−x, (k+1)L_{k+1}=(2k+1−x)L_k − k·L_{k−1}
};

/// The degree-n orthogonal polynomial of one family, with exact rational
/// coefficients, as an expression in `variable`.
struct OrthoPolyResult {
    enum class Status { Ok, Error };

    Status status = Status::Error;
    Expr expr;               ///< the polynomial (Status::Ok)
    std::string family;      ///< human label, e.g. "Chebyshev T", "Legendre"
    std::string variable;    ///< the variable it is written in, e.g. "x"
    int degree = 0;          ///< == n
    std::string message;     ///< explanation for a non-Ok status
};

/// Generate the degree-`n` polynomial of `family` in `variable` via the standard
/// three-term recurrence, kept exact over the rationals. `n` must be ≥ 0; very
/// large `n` whose integer coefficients overflow the exact range returns an
/// Error rather than a wrapped value.
OrthoPolyResult ortho_poly(OrthoFamily family, int n, std::string_view variable = "x");

/// Parse a family name: "chebyshev"/"chebyshevt"/"chebyt"/"t", "chebyshevu"/
/// "chebyu"/"u", "legendre"/"p", "hermite"/"h", "laguerre"/"l" (case-insensitive).
/// Returns nullopt for an unknown name.
std::optional<OrthoFamily> parse_ortho_family(std::string_view name);

} // namespace mathsolver
