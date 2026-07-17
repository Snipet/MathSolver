// Dense linear-algebra numerics (linalg_core.hpp).

#include "linalg_core.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <utility>

namespace mathsolver::plugins::linalg {

namespace {

void check_square(const Matrix& a, const char* op) {
    if (a.empty() || a.size() > static_cast<std::size_t>(k_max_n)) {
        throw LinalgError(std::format("{}: matrix dimension must be in [1, {}]",
                                      op, k_max_n));
    }
    for (const auto& row : a) {
        if (row.size() != a.size()) {
            throw LinalgError(std::format("{}: the matrix must be square "
                                          "({} rows, a row of {} entries)",
                                          op, a.size(), row.size()));
        }
    }
}

void check_rect(const Matrix& a, const char* op) {
    if (a.empty() || a.front().empty() ||
        a.size() > static_cast<std::size_t>(k_max_n) ||
        a.front().size() > static_cast<std::size_t>(k_max_n)) {
        throw LinalgError(std::format("{}: matrix dimensions must be in "
                                      "[1, {}]",
                                      op, k_max_n));
    }
    for (const auto& row : a) {
        if (row.size() != a.front().size()) {
            throw LinalgError(std::format("{}: ragged matrix rows", op));
        }
    }
}

/// LU with partial pivoting in place: returns the permutation sign, fills
/// `piv`; a is overwritten with L\U. Returns 0 on singularity.
int lu_factor(Matrix& a, std::vector<int>& piv) {
    const int n = static_cast<int>(a.size());
    piv.resize(static_cast<std::size_t>(n));
    int sign = 1;
    for (int k = 0; k < n; ++k) {
        int p = k;
        for (int i = k + 1; i < n; ++i) {
            if (std::abs(a[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)]) >
                std::abs(a[static_cast<std::size_t>(p)][static_cast<std::size_t>(k)])) {
                p = i;
            }
        }
        if (std::abs(a[static_cast<std::size_t>(p)][static_cast<std::size_t>(k)]) < 1e-13) {
            return 0;
        }
        if (p != k) {
            std::swap(a[static_cast<std::size_t>(p)], a[static_cast<std::size_t>(k)]);
            sign = -sign;
        }
        piv[static_cast<std::size_t>(k)] = p;
        for (int i = k + 1; i < n; ++i) {
            auto& ai = a[static_cast<std::size_t>(i)];
            const auto& ak = a[static_cast<std::size_t>(k)];
            const double m = ai[static_cast<std::size_t>(k)] / ak[static_cast<std::size_t>(k)];
            ai[static_cast<std::size_t>(k)] = m;
            for (int j = k + 1; j < n; ++j) {
                ai[static_cast<std::size_t>(j)] -= m * ak[static_cast<std::size_t>(j)];
            }
        }
    }
    return sign;
}

Vector lu_backsolve(const Matrix& lu, const std::vector<int>& piv, Vector b) {
    const int n = static_cast<int>(lu.size());
    // Apply ALL row interchanges before eliminating: the stored multipliers
    // reflect every later swap, so interleaving swaps with the forward
    // substitution pairs multipliers with the wrong entries.
    for (int k = 0; k < n; ++k) {
        std::swap(b[static_cast<std::size_t>(k)], b[static_cast<std::size_t>(piv[static_cast<std::size_t>(k)])]);
    }
    for (int k = 0; k < n; ++k) {
        for (int i = k + 1; i < n; ++i) {
            b[static_cast<std::size_t>(i)] -=
                lu[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)] * b[static_cast<std::size_t>(k)];
        }
    }
    for (int i = n - 1; i >= 0; --i) {
        for (int j = i + 1; j < n; ++j) {
            b[static_cast<std::size_t>(i)] -=
                lu[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] * b[static_cast<std::size_t>(j)];
        }
        b[static_cast<std::size_t>(i)] /= lu[static_cast<std::size_t>(i)][static_cast<std::size_t>(i)];
    }
    return b;
}

} // namespace

Vector lu_solve(Matrix a, Vector b) {
    check_square(a, "solve");
    if (b.size() != a.size()) {
        throw LinalgError(std::format(
            "solve: b has {} entries for a {}x{} matrix", b.size(), a.size(),
            a.size()));
    }
    std::vector<int> piv;
    if (lu_factor(a, piv) == 0) {
        throw LinalgError("solve: the matrix is singular (try lstsq)");
    }
    return lu_backsolve(a, piv, std::move(b));
}

double determinant(Matrix a) {
    check_square(a, "det");
    std::vector<int> piv;
    const int sign = lu_factor(a, piv);
    if (sign == 0) {
        return 0.0;
    }
    double d = sign;
    for (std::size_t i = 0; i < a.size(); ++i) {
        d *= a[i][i];
    }
    return d;
}

Matrix inverse(const Matrix& a_in) {
    check_square(a_in, "inv");
    Matrix a = a_in;
    std::vector<int> piv;
    if (lu_factor(a, piv) == 0) {
        throw LinalgError("inv: the matrix is singular");
    }
    const std::size_t n = a.size();
    Matrix out(n, Vector(n, 0.0));
    for (std::size_t j = 0; j < n; ++j) {
        Vector e(n, 0.0);
        e[j] = 1.0;
        const Vector col = lu_backsolve(a, piv, std::move(e));
        for (std::size_t i = 0; i < n; ++i) {
            out[i][j] = col[i];
        }
    }
    return out;
}

// --- eigenvalues ------------------------------------------------------------

std::vector<std::complex<double>> eigenvalues(Matrix a) {
    check_square(a, "eig");
    const int n = static_cast<int>(a.size());
    if (n > k_max_eig_n) {
        throw LinalgError(std::format("eig: dimension is capped at {}",
                                      k_max_eig_n));
    }
    auto at = [&a](int i, int j) -> double& {
        return a[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
    };

    // Householder reduction to upper Hessenberg form.
    for (int k = 1; k < n - 1; ++k) {
        double norm = 0.0;
        for (int i = k; i < n; ++i) {
            norm += at(i, k - 1) * at(i, k - 1);
        }
        norm = std::sqrt(norm);
        if (norm < 1e-300 || std::abs(std::abs(at(k, k - 1)) - norm) < 1e-300) {
            continue;
        }
        const double alpha = at(k, k - 1) > 0 ? -norm : norm;
        Vector v(static_cast<std::size_t>(n), 0.0);
        v[static_cast<std::size_t>(k)] = at(k, k - 1) - alpha;
        for (int i = k + 1; i < n; ++i) {
            v[static_cast<std::size_t>(i)] = at(i, k - 1);
        }
        double vnorm2 = 0.0;
        for (int i = k; i < n; ++i) {
            vnorm2 += v[static_cast<std::size_t>(i)] * v[static_cast<std::size_t>(i)];
        }
        if (vnorm2 < 1e-300) {
            continue;
        }
        // A <- (I - 2vv^T/v^Tv) A (I - 2vv^T/v^Tv).
        for (int j = 0; j < n; ++j) {
            double dot = 0.0;
            for (int i = k; i < n; ++i) {
                dot += v[static_cast<std::size_t>(i)] * at(i, j);
            }
            dot = 2.0 * dot / vnorm2;
            for (int i = k; i < n; ++i) {
                at(i, j) -= dot * v[static_cast<std::size_t>(i)];
            }
        }
        for (int i = 0; i < n; ++i) {
            double dot = 0.0;
            for (int j = k; j < n; ++j) {
                dot += at(i, j) * v[static_cast<std::size_t>(j)];
            }
            dot = 2.0 * dot / vnorm2;
            for (int j = k; j < n; ++j) {
                at(i, j) -= dot * v[static_cast<std::size_t>(j)];
            }
        }
    }

    // Shifted QR on the Hessenberg matrix with deflation from the bottom.
    std::vector<std::complex<double>> eig;
    int m = n;
    int stall = 0;
    while (m > 0) {
        if (m == 1) {
            eig.push_back(at(0, 0));
            break;
        }
        // Deflate a negligible subdiagonal.
        int split = -1;
        for (int i = m - 1; i > 0; --i) {
            if (std::abs(at(i, i - 1)) <
                1e-13 * (std::abs(at(i, i)) + std::abs(at(i - 1, i - 1)) + 1e-300)) {
                split = i;
                break;
            }
        }
        if (split == m - 1) {
            eig.push_back(at(m - 1, m - 1));
            --m;
            stall = 0;
            continue;
        }
        // A stalled trailing 2x2 holds a conjugate (or close real) pair.
        const double a11 = at(m - 2, m - 2);
        const double a12 = at(m - 2, m - 1);
        const double a21 = at(m - 1, m - 2);
        const double a22 = at(m - 1, m - 1);
        const double tr = a11 + a22;
        const double det2 = a11 * a22 - a12 * a21;
        const double disc = tr * tr / 4.0 - det2;
        if (m == 2 || split == m - 2 || stall > 40) {
            if (m != 2 && split != m - 2) {
                throw LinalgError("eig: the QR iteration did not converge");
            }
            if (disc >= 0) {
                const double root = std::sqrt(disc);
                eig.push_back(tr / 2.0 + root);
                eig.push_back(tr / 2.0 - root);
            } else {
                const double root = std::sqrt(-disc);
                eig.push_back({tr / 2.0, root});
                eig.push_back({tr / 2.0, -root});
            }
            m -= 2;
            stall = 0;
            continue;
        }
        // Wilkinson shift: the 2x2 eigenvalue closest to a22.
        double shift = a22;
        if (disc >= 0) {
            const double root = std::sqrt(disc);
            const double l1 = tr / 2.0 + root;
            const double l2 = tr / 2.0 - root;
            shift = std::abs(l1 - a22) < std::abs(l2 - a22) ? l1 : l2;
        }
        // QR step via Givens rotations on the Hessenberg band.
        for (int i = 0; i < m; ++i) {
            at(i, i) -= shift;
        }
        std::vector<std::pair<double, double>> rot(static_cast<std::size_t>(m - 1));
        for (int k = 0; k < m - 1; ++k) {
            const double x = at(k, k);
            const double y = at(k + 1, k);
            const double r = std::hypot(x, y);
            const double c = r < 1e-300 ? 1.0 : x / r;
            const double s = r < 1e-300 ? 0.0 : y / r;
            rot[static_cast<std::size_t>(k)] = {c, s};
            for (int j = k; j < m; ++j) {
                const double t1 = at(k, j);
                const double t2 = at(k + 1, j);
                at(k, j) = c * t1 + s * t2;
                at(k + 1, j) = -s * t1 + c * t2;
            }
        }
        for (int k = 0; k < m - 1; ++k) {
            const auto [c, s] = rot[static_cast<std::size_t>(k)];
            for (int i = 0; i <= std::min(k + 2, m - 1); ++i) {
                const double t1 = at(i, k);
                const double t2 = at(i, k + 1);
                at(i, k) = c * t1 + s * t2;
                at(i, k + 1) = -s * t1 + c * t2;
            }
        }
        for (int i = 0; i < m; ++i) {
            at(i, i) += shift;
        }
        ++stall;
    }
    // Descending by real part, then imaginary, for stable presentation.
    std::sort(eig.begin(), eig.end(), [](const auto& x, const auto& y) {
        if (x.real() != y.real()) return x.real() > y.real();
        return x.imag() > y.imag();
    });
    return eig;
}

// --- SVD (one-sided Jacobi) -------------------------------------------------

Svd svd(const Matrix& a_in) {
    check_rect(a_in, "svd");
    const bool wide = a_in.front().size() > a_in.size();
    const Matrix a = wide ? transpose(a_in) : a_in;
    const std::size_t m = a.size();
    const std::size_t n = a.front().size();

    Matrix u = a;                        // columns rotate toward orthogonality
    Matrix v(n, Vector(n, 0.0));
    for (std::size_t i = 0; i < n; ++i) {
        v[i][i] = 1.0;
    }
    auto coldot = [&u, m](std::size_t p, std::size_t q) {
        double s = 0.0;
        for (std::size_t i = 0; i < m; ++i) {
            s += u[i][p] * u[i][q];
        }
        return s;
    };
    for (int sweep = 0; sweep < 60; ++sweep) {
        double off = 0.0;
        for (std::size_t p = 0; p + 1 < n; ++p) {
            for (std::size_t q = p + 1; q < n; ++q) {
                const double apq = coldot(p, q);
                const double app = coldot(p, p);
                const double aqq = coldot(q, q);
                off = std::max(off, std::abs(apq) /
                                        (std::sqrt(app * aqq) + 1e-300));
                if (std::abs(apq) < 1e-15 * std::sqrt(app * aqq) + 1e-300) {
                    continue;
                }
                const double zeta = (aqq - app) / (2.0 * apq);
                const double t = (zeta >= 0 ? 1.0 : -1.0) /
                                 (std::abs(zeta) + std::sqrt(1.0 + zeta * zeta));
                const double c = 1.0 / std::sqrt(1.0 + t * t);
                const double s = c * t;
                for (std::size_t i = 0; i < m; ++i) {
                    const double t1 = u[i][p];
                    const double t2 = u[i][q];
                    u[i][p] = c * t1 - s * t2;
                    u[i][q] = s * t1 + c * t2;
                }
                for (std::size_t i = 0; i < n; ++i) {
                    const double t1 = v[i][p];
                    const double t2 = v[i][q];
                    v[i][p] = c * t1 - s * t2;
                    v[i][q] = s * t1 + c * t2;
                }
            }
        }
        if (off < 1e-14) {
            break;
        }
    }
    // Column norms are the singular values; sort descending.
    std::vector<std::size_t> order(n);
    Vector sig(n, 0.0);
    for (std::size_t j = 0; j < n; ++j) {
        sig[j] = std::sqrt(coldot(j, j));
        order[j] = j;
    }
    std::sort(order.begin(), order.end(),
              [&sig](std::size_t x, std::size_t y) { return sig[x] > sig[y]; });
    Svd out;
    out.sigma.resize(n);
    out.u.assign(m, Vector(n, 0.0));
    out.v.assign(n, Vector(n, 0.0));
    for (std::size_t jj = 0; jj < n; ++jj) {
        const std::size_t j = order[jj];
        out.sigma[jj] = sig[j];
        for (std::size_t i = 0; i < m; ++i) {
            out.u[i][jj] = sig[j] > 1e-300 ? u[i][j] / sig[j] : 0.0;
        }
        for (std::size_t i = 0; i < n; ++i) {
            out.v[i][jj] = v[i][j];
        }
    }
    if (wide) {
        std::swap(out.u, out.v);
    }
    return out;
}

int rank(const Matrix& a) {
    const Svd s = svd(a);
    if (s.sigma.empty()) {
        return 0;
    }
    const double tol = std::max(a.size(), a.front().size()) *
                       std::numeric_limits<double>::epsilon() * s.sigma.front();
    int r = 0;
    for (const double v : s.sigma) {
        if (v > tol) {
            ++r;
        }
    }
    return r;
}

double cond(const Matrix& a) {
    const Svd s = svd(a);
    const double smin = s.sigma.back();
    if (smin < 1e-300) {
        return std::numeric_limits<double>::infinity();
    }
    return s.sigma.front() / smin;
}

Vector lstsq(const Matrix& a, const Vector& b) {
    check_rect(a, "lstsq");
    if (b.size() != a.size()) {
        throw LinalgError(std::format(
            "lstsq: b has {} entries for a {}x{} matrix", b.size(), a.size(),
            a.front().size()));
    }
    const Svd s = svd(a);
    const double tol = std::max(a.size(), a.front().size()) *
                       std::numeric_limits<double>::epsilon() *
                       (s.sigma.empty() ? 0.0 : s.sigma.front());
    // x = V Σ⁺ Uᵀ b.
    const std::size_t r = s.sigma.size();
    Vector utb(r, 0.0);
    for (std::size_t j = 0; j < r; ++j) {
        for (std::size_t i = 0; i < b.size(); ++i) {
            utb[j] += s.u[i][j] * b[i];
        }
        utb[j] = s.sigma[j] > tol ? utb[j] / s.sigma[j] : 0.0;
    }
    Vector x(s.v.size(), 0.0);
    for (std::size_t i = 0; i < s.v.size(); ++i) {
        for (std::size_t j = 0; j < r; ++j) {
            x[i] += s.v[i][j] * utb[j];
        }
    }
    return x;
}

// --- helpers ----------------------------------------------------------------

Matrix matmul(const Matrix& a, const Matrix& b) {
    if (a.empty() || b.empty() || a.front().size() != b.size()) {
        throw LinalgError("matmul: inner dimensions do not match");
    }
    Matrix out(a.size(), Vector(b.front().size(), 0.0));
    for (std::size_t i = 0; i < a.size(); ++i) {
        for (std::size_t k = 0; k < b.size(); ++k) {
            const double aik = a[i][k];
            for (std::size_t j = 0; j < b.front().size(); ++j) {
                out[i][j] += aik * b[k][j];
            }
        }
    }
    return out;
}

Vector matvec(const Matrix& a, const Vector& x) {
    if (a.empty() || a.front().size() != x.size()) {
        throw LinalgError("matvec: dimensions do not match");
    }
    Vector out(a.size(), 0.0);
    for (std::size_t i = 0; i < a.size(); ++i) {
        for (std::size_t j = 0; j < x.size(); ++j) {
            out[i] += a[i][j] * x[j];
        }
    }
    return out;
}

Matrix transpose(const Matrix& a) {
    if (a.empty()) {
        return {};
    }
    Matrix out(a.front().size(), Vector(a.size(), 0.0));
    for (std::size_t i = 0; i < a.size(); ++i) {
        for (std::size_t j = 0; j < a.front().size(); ++j) {
            out[j][i] = a[i][j];
        }
    }
    return out;
}

} // namespace mathsolver::plugins::linalg
