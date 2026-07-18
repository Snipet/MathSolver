#pragma once

// Dense linear-algebra numerics, separated from the plugin command layer so
// the native test suite can verify the math directly.
//
// Everything is double-precision dense with small-n caps (the plugin's job
// is exactness of presentation, not scale): LU with partial pivoting for
// solve/det/inverse, Hessenberg + Wilkinson-shifted QR with trailing 2x2
// complex-pair extraction for eigenvalues, and one-sided Jacobi for the SVD
// (which also powers rank, cond, and least squares).

#include <complex>
#include <stdexcept>
#include <string>
#include <vector>

namespace mathsolver::plugins::linalg {

using Matrix = std::vector<std::vector<double>>;
using Vector = std::vector<double>;

class LinalgError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

inline constexpr int k_max_n = 32;      ///< Hard cap on matrix dimension.
inline constexpr int k_max_eig_n = 16;  ///< Eigen decomposition cap.

/// Solve A x = b by LU with partial pivoting. Throws on singular A.
Vector lu_solve(Matrix a, Vector b);

/// Determinant by LU (0 for a singular matrix, no throw).
double determinant(Matrix a);

/// Inverse by LU on the identity columns. Throws on singular A.
Matrix inverse(const Matrix& a);

/// Eigenvalues of a real square matrix: Householder Hessenberg reduction,
/// then shifted QR; a trailing 2x2 that resists real deflation contributes
/// its exact conjugate pair. Throws when the iteration stalls.
std::vector<std::complex<double>> eigenvalues(Matrix a);

struct Svd {
    Matrix u;        ///< m x r with orthonormal columns.
    Vector sigma;    ///< r singular values, descending.
    Matrix v;        ///< n x r with orthonormal columns.
};

/// Thin SVD by one-sided Jacobi. Works for m >= n and m < n (via transpose).
Svd svd(const Matrix& a);

/// Numeric rank via SVD with the usual max(m,n)·eps·sigma_max tolerance.
int rank(const Matrix& a);

/// 2-norm condition number sigma_max / sigma_min (inf for singular).
double cond(const Matrix& a);

/// Least-squares solution of A x ~= b via the SVD pseudoinverse.
Vector lstsq(const Matrix& a, const Vector& b);

// --- structured solvers -----------------------------------------------------
//
// Structure-exploiting solvers that scale far past the dense k_max_n cap:
// O(n) Thomas elimination for tridiagonal systems, the O(n^2)
// Levinson-Durbin recursion for symmetric Toeplitz systems, and DFT
// diagonalization for circulant systems.

inline constexpr int k_max_structured_n = 4096;

/// Tridiagonal solve (Thomas algorithm, no pivoting): sub/super have n-1
/// entries, diag and rhs have n. Throws on a zero pivot (the algorithm
/// requires it structurally, e.g. diagonal dominance).
Vector tridiag_solve(const Vector& sub, const Vector& diag,
                     const Vector& super, const Vector& rhs);

/// Symmetric Toeplitz solve T x = b by the Levinson recursion, T given by
/// its first column. Requires every leading principal minor nonsingular
/// (throws when the recursion's beta hits zero).
Vector toeplitz_solve(const Vector& first_col, const Vector& b);

/// Circulant solve C x = b by DFT diagonalization, C given by its first
/// column (C_{j,k} = c[(j-k) mod n]). Throws when an eigenvalue (a DFT
/// coefficient of c) vanishes.
Vector circulant_solve(const Vector& first_col, const Vector& b);

/// y = T x for the symmetric Toeplitz matrix given by its first column
/// (used for residual reporting).
Vector toeplitz_matvec(const Vector& first_col, const Vector& x);

/// y = C x for the circulant matrix given by its first column.
Vector circulant_matvec(const Vector& first_col, const Vector& x);

// --- helpers shared with the command layer ---------------------------------

/// A * B, A * x, and the transpose (dimension-checked).
Matrix matmul(const Matrix& a, const Matrix& b);
Vector matvec(const Matrix& a, const Vector& x);
Matrix transpose(const Matrix& a);

} // namespace mathsolver::plugins::linalg
