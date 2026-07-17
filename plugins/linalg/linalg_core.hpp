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

// --- helpers shared with the command layer ---------------------------------

/// A * B, A * x, and the transpose (dimension-checked).
Matrix matmul(const Matrix& a, const Matrix& b);
Vector matvec(const Matrix& a, const Vector& x);
Matrix transpose(const Matrix& a);

} // namespace mathsolver::plugins::linalg
