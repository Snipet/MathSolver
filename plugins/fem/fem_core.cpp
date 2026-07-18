// 1-D finite-element numerics (fem_core.hpp).

#include "fem_core.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <memory>
#include <numbers>

#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/expr.hpp"

namespace mathsolver::plugins::fem {

namespace {

using Matrix = std::vector<std::vector<double>>;
using Vector = std::vector<double>;

void check_symbols(const Expr& e, const char* what) {
    for (const std::string& s : free_symbols(e)) {
        if (s != "x") {
            throw Error(std::format("the {} may only use x (found '{}')",
                                    what, s));
        }
    }
}

double eval_at(const Expr& e, double x, const char* what) {
    const double v = evaluate(e, Bindings{{"x", x}});
    if (!std::isfinite(v)) {
        throw Error(std::format("{} is not finite at x = {:.6g}", what, x));
    }
    return v;
}

// 3-point Gauss–Legendre on the reference element [0, 1]: exact through
// degree 5, which covers every P1/P2 stiffness and mass integrand with
// constant coefficients (variable coefficients are approximated, as usual).
struct GaussPoint {
    double xi;
    double w;
};
constexpr double k_gauss_off = 0.3872983346207417; // sqrt(3/5) / 2
constexpr GaussPoint k_gauss[3] = {
    {0.5 - k_gauss_off, 5.0 / 18.0},
    {0.5, 8.0 / 18.0},
    {0.5 + k_gauss_off, 5.0 / 18.0},
};

/// Lagrange basis on [0, 1]: values and xi-derivatives at xi.
void shape(int degree, double xi, double* n, double* dn) {
    if (degree == 1) {
        n[0] = 1.0 - xi;
        n[1] = xi;
        dn[0] = -1.0;
        dn[1] = 1.0;
        return;
    }
    // P2 nodes at 0, 1/2, 1.
    n[0] = 2.0 * xi * xi - 3.0 * xi + 1.0;
    n[1] = 4.0 * xi * (1.0 - xi);
    n[2] = 2.0 * xi * xi - xi;
    dn[0] = 4.0 * xi - 3.0;
    dn[1] = 4.0 - 8.0 * xi;
    dn[2] = 4.0 * xi - 1.0;
}

/// Dense LU with partial pivoting, factored once and reused (the FEM
/// systems here are small enough that banded storage buys nothing).
class DenseLU {
  public:
    explicit DenseLU(Matrix a) : a_(std::move(a)), piv_(a_.size()) {
        const std::size_t n = a_.size();
        // Singularity is judged RELATIVE to the matrix scale: an absolute
        // pivot floor is meaningless when the whole system is scaled by
        // p/h (tiny p) or 1/h (large 1/h), which produced both spurious
        // "singular" errors and missed singularities.
        double scale = 0.0;
        for (const Vector& row : a_) {
            for (const double v : row) {
                scale = std::max(scale, std::abs(v));
            }
        }
        const double tol = std::max(1e-300, 1e-12 * scale);
        for (std::size_t k = 0; k < n; ++k) {
            std::size_t p = k;
            for (std::size_t i = k + 1; i < n; ++i) {
                if (std::abs(a_[i][k]) > std::abs(a_[p][k])) {
                    p = i;
                }
            }
            if (std::abs(a_[p][k]) < tol) {
                throw Error("the assembled system is singular");
            }
            std::swap(a_[p], a_[k]);
            piv_[k] = p;
            for (std::size_t i = k + 1; i < n; ++i) {
                a_[i][k] /= a_[k][k];
                for (std::size_t j = k + 1; j < n; ++j) {
                    a_[i][j] -= a_[i][k] * a_[k][j];
                }
            }
        }
    }

    Vector solve(Vector b) const {
        const std::size_t n = a_.size();
        // All row interchanges FIRST (LAPACK laswp order): the stored
        // multipliers are in final-pivot positions, so interleaving the
        // swaps with the forward substitution pairs them with the wrong
        // right-hand-side entries.
        for (std::size_t k = 0; k < n; ++k) {
            std::swap(b[k], b[piv_[k]]);
        }
        for (std::size_t k = 0; k < n; ++k) {
            for (std::size_t i = k + 1; i < n; ++i) {
                b[i] -= a_[i][k] * b[k];
            }
        }
        for (std::size_t k = n; k-- > 0;) {
            for (std::size_t j = k + 1; j < n; ++j) {
                b[k] -= a_[k][j] * b[j];
            }
            b[k] /= a_[k][k];
        }
        return b;
    }

  private:
    Matrix a_;
    std::vector<std::size_t> piv_;
};

struct Assembled {
    Matrix k;        ///< Stiffness incl. the q (or w for mass) term.
    Vector f;        ///< Load vector (zero for eigen assembly).
    Vector nodes;    ///< Global node coordinates.
};

/// Assemble the Galerkin system for -(p u')' + q u = f. Passing q as the
/// weight and no p/f term (see `mass`) yields the mass matrix instead.
Assembled assemble(const Expr& p, const Expr& q, const Expr* f, double a,
                   double b, int degree, int elements, bool mass) {
    const int per = degree + 1;               // local nodes per element
    const int m = degree * elements + 1;      // global nodes
    Assembled out;
    out.k.assign(m, Vector(m, 0.0));
    out.f.assign(m, 0.0);
    out.nodes.resize(m);
    const double h = (b - a) / elements;
    for (int i = 0; i < m; ++i) {
        out.nodes[i] = a + (b - a) * i / (m - 1);
    }
    double n[3];
    double dn[3];
    for (int e = 0; e < elements; ++e) {
        const double xl = a + h * e;
        const int base = degree * e;
        for (const GaussPoint& g : k_gauss) {
            shape(degree, g.xi, n, dn);
            const double x = xl + g.xi * h;
            double pv = 0.0;
            if (!mass) {
                pv = eval_at(p, x, "p(x)");
                if (pv <= 0.0) {
                    throw Error(std::format(
                        "p(x) must be positive on [a, b] (p({:.6g}) = {:.6g})",
                        x, pv));
                }
            }
            const double qv = eval_at(q, x, mass ? "w(x)" : "q(x)");
            if (mass && qv <= 0.0) {
                throw Error(std::format(
                    "w(x) must be positive on [a, b] (w({:.6g}) = {:.6g})",
                    x, qv));
            }
            const double fv = f ? eval_at(*f, x, "f(x)") : 0.0;
            for (int i = 0; i < per; ++i) {
                for (int j = 0; j < per; ++j) {
                    double kij = qv * n[i] * n[j] * h;
                    if (!mass) {
                        kij += pv * dn[i] * dn[j] / h;
                    }
                    out.k[base + i][base + j] += g.w * kij;
                }
                out.f[base + i] += g.w * fv * n[i] * h;
            }
        }
    }
    return out;
}

/// Rigorous lower bound for every generalized eigenvalue of
/// -(p u')' + q u = lambda w u: since the p-stiffness term is >= 0 and
/// lambda = [∫ p u'^2 + ∫ q u^2] / [∫ w u^2] >= (∫ q u^2)/(∫ w u^2)
/// >= min_x (q(x) / w(x)) (using w > 0), the smallest ratio over the
/// quadrature points bounds the whole spectrum below. Inverse iteration
/// shifts by this so it finds the smallest ALGEBRAIC eigenvalue (the
/// physical ground state) rather than the smallest in magnitude.
double spectrum_lower_bound(const Expr& q, const Expr& w, double a, double b,
                            int elements) {
    const double h = (b - a) / elements;
    double lo = std::numeric_limits<double>::infinity();
    for (int e = 0; e < elements; ++e) {
        const double xl = a + h * e;
        for (const GaussPoint& g : k_gauss) {
            const double x = xl + g.xi * h;
            const double qv = eval_at(q, x, "q(x)");
            const double wv = eval_at(w, x, "w(x)");
            lo = std::min(lo, qv / wv);
        }
    }
    return lo;
}

/// Apply one boundary condition to the assembled system (symmetrically for
/// Dirichlet: the known column moves to the right-hand side).
void apply_bc(Assembled& s, std::size_t node, const Bc& bc, bool left) {
    if (!bc.dirichlet) {
        // Natural condition p u' = v: the weak boundary term is
        // [p u' phi]_a^b, so +v at the right end and -v at the left.
        s.f[node] += left ? -bc.value : bc.value;
        return;
    }
    const std::size_t m = s.k.size();
    for (std::size_t i = 0; i < m; ++i) {
        if (i != node) {
            s.f[i] -= s.k[i][node] * bc.value;
            s.k[i][node] = 0.0;
            s.k[node][i] = 0.0;
        }
    }
    s.k[node][node] = 1.0;
    s.f[node] = bc.value;
}

FemSolution solve_single(const Expr& p, const Expr& q, const Expr& f,
                         double a, double b, const Bc& left, const Bc& right,
                         int degree, int elements) {
    Assembled s = assemble(p, q, &f, a, b, degree, elements, false);
    apply_bc(s, 0, left, true);
    apply_bc(s, s.k.size() - 1, right, false);
    FemSolution out;
    out.degree = degree;
    out.x = s.nodes;
    try {
        out.u = DenseLU(std::move(s.k)).solve(std::move(s.f));
    } catch (const Error&) {
        throw Error(
            "the assembled system is singular — a pure-Neumann problem with "
            "q = 0 determines u only up to a constant (fix u at one "
            "endpoint), and q < 0 can hit a resonance");
    }
    return out;
}

double m_dot(const Matrix& m, const Vector& x, const Vector& y) {
    double acc = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        double row = 0.0;
        for (std::size_t j = 0; j < y.size(); ++j) {
            row += m[i][j] * y[j];
        }
        acc += x[i] * row;
    }
    return acc;
}

Vector mat_vec(const Matrix& m, const Vector& x) {
    Vector out(m.size(), 0.0);
    for (std::size_t i = 0; i < m.size(); ++i) {
        for (std::size_t j = 0; j < x.size(); ++j) {
            out[i] += m[i][j] * x[j];
        }
    }
    return out;
}

} // namespace

double fem_eval(const FemSolution& s, double x) {
    const std::size_t m = s.x.size();
    if (m < 2) {
        throw Error("fem_eval: empty solution");
    }
    const double a = s.x.front();
    const double b = s.x.back();
    x = std::clamp(x, a, b);
    // Element index from the uniform vertex spacing.
    const int elements = static_cast<int>((m - 1) / s.degree);
    const double h = (b - a) / elements;
    int e = std::min(elements - 1,
                     static_cast<int>(std::floor((x - a) / h)));
    const double xi = (x - (a + e * h)) / h;
    double n[3];
    double dn[3];
    shape(s.degree, xi, n, dn);
    const int base = s.degree * e;
    double u = 0.0;
    for (int i = 0; i <= s.degree; ++i) {
        u += n[i] * s.u[base + i];
    }
    return u;
}

BvpResult solve_bvp(const Expr& p, const Expr& q, const Expr& f, double a,
                    double b, Bc left, Bc right, int degree, int elements) {
    check_symbols(p, "coefficient p(x)");
    check_symbols(q, "coefficient q(x)");
    check_symbols(f, "right-hand side f(x)");
    if (!(b > a)) {
        throw Error("the interval must satisfy a < b");
    }
    if (degree != 1 && degree != 2) {
        throw Error("element degree must be 1 (linear) or 2 (quadratic)");
    }
    // The estimate refines to 4x, so the FINEST mesh — not the requested one —
    // sets the cost. Cap that: a 4*elements P2 mesh is 8*elements+1 nodes
    // through a dense O(n^3) LU, and we keep the finest under ~1000 nodes so
    // the browser never stalls.
    const int max_fine_nodes = 1000;
    const int max_elements = (max_fine_nodes - 1) / (4 * degree);
    if (elements < 4 || elements > max_elements) {
        throw Error(std::format(
            "element count must be in [4, {}] for P{} (the solve refines to "
            "4x for the error estimate)",
            max_elements, degree));
    }
    const FemSolution coarse =
        solve_single(p, q, f, a, b, left, right, degree, elements);
    const FemSolution mid =
        solve_single(p, q, f, a, b, left, right, degree, 2 * elements);
    const FemSolution fine =
        solve_single(p, q, f, a, b, left, right, degree, 4 * elements);

    // Sample all three on one off-node grid; nodal values can superconverge
    // (P1 is exact at nodes for -u'' = f), which would fake a zero error.
    constexpr int k_samples = 211;
    double e_coarse = 0.0;
    double e_mid = 0.0;
    double u_scale = 0.0;
    for (int i = 0; i < k_samples; ++i) {
        const double x = a + (b - a) * (i + 0.5) / k_samples;
        const double uf = fem_eval(fine, x);
        u_scale = std::max(u_scale, std::abs(uf));
        e_coarse = std::max(e_coarse, std::abs(fem_eval(coarse, x) - uf));
        e_mid = std::max(e_mid, std::abs(fem_eval(mid, x) - uf));
    }
    BvpResult out;
    out.solution = fine;
    out.error_estimate = e_mid;
    out.observed_order = std::numeric_limits<double>::quiet_NaN();
    const double scale = std::max(1.0, u_scale);
    if (e_mid <= 1e-10 * scale) {
        // Genuinely at the roundoff floor: the discrete solution is (near)
        // exact for this problem, so an order cannot be measured.
        out.warnings.push_back(
            "refinement differences are at roundoff — no observed order "
            "(the discrete solution is essentially exact for this problem)");
    } else if (e_mid > 0.1 * scale || e_coarse <= e_mid) {
        // Either the refinement disagreement is a large FRACTION of the
        // solution (a converged FEM solution has small relative error), or
        // it fails to shrink under refinement — both mean the discrete
        // problem is not usefully converging, almost always q near an
        // eigenvalue (a resonant / near-singular BVP). Say so loudly
        // instead of claiming the solution is exact or reporting an order.
        out.warnings.push_back(std::format(
            "the solution is NOT converging under refinement (refinement "
            "disagreement {:.3g} is {:.0f}% of the solution scale {:.3g}) — "
            "the problem is likely near a resonance (q close to an "
            "eigenvalue); treat the result as unreliable",
            e_mid, 100.0 * e_mid / scale, scale));
    } else {
        out.observed_order = std::log2(e_coarse / e_mid);
    }
    return out;
}

ModesResult solve_modes(const Expr& p, const Expr& q, const Expr& w, double a,
                        double b, int count, int degree, int elements) {
    check_symbols(p, "coefficient p(x)");
    check_symbols(q, "coefficient q(x)");
    check_symbols(w, "weight w(x)");
    if (!(b > a)) {
        throw Error("the interval must satisfy a < b");
    }
    if (degree != 1 && degree != 2) {
        throw Error("element degree must be 1 (linear) or 2 (quadratic)");
    }
    if (elements < 4 || elements > 256) {
        throw Error("element count must be in [4, 256]");
    }
    if (count < 1 || count > 6) {
        throw Error("mode count must be in [1, 6]");
    }
    Assembled ks = assemble(p, q, nullptr, a, b, degree, elements, false);
    Assembled ms = assemble(p, w, nullptr, a, b, degree, elements, true);

    // Interior reduction: homogeneous Dirichlet drops the first and last
    // global node (vertices in both P1 and P2 numbering).
    const std::size_t m = ks.k.size();
    const std::size_t mi = m - 2;
    // The discrete problem has exactly `mi` eigenpairs; asking for more would
    // otherwise fabricate duplicates.
    if (count > static_cast<int>(mi)) {
        throw Error(std::format(
            "only {} eigenpairs exist for this discretization ({} interior "
            "degrees of freedom); ask for fewer modes or use more elements",
            mi, mi));
    }
    Matrix ki(mi, Vector(mi, 0.0));
    Matrix mm(mi, Vector(mi, 0.0));
    for (std::size_t i = 0; i < mi; ++i) {
        for (std::size_t j = 0; j < mi; ++j) {
            ki[i][j] = ks.k[i + 1][j + 1];
            mm[i][j] = ms.k[i + 1][j + 1];
        }
    }

    // Spectral shift so inverse iteration finds the smallest ALGEBRAIC
    // eigenvalues, not the smallest in magnitude: with an indefinite K
    // (q < 0) the true ground state can be a large-magnitude negative
    // eigenvalue that the unshifted iteration would skip entirely. Shift
    // sigma below the whole spectrum; (K - sigma M) is then M-positive-
    // definite, so inverse iteration on it converges to the smallest
    // lambda - sigma, i.e. the smallest algebraic lambda. This also makes
    // the factored operator nonsingular even when lambda = 0.
    const double lo = spectrum_lower_bound(q, w, a, b, elements);
    const double sigma = lo - 1.0; // strictly below every eigenvalue
    Matrix shifted(mi, Vector(mi, 0.0));
    for (std::size_t i = 0; i < mi; ++i) {
        for (std::size_t j = 0; j < mi; ++j) {
            shifted[i][j] = ki[i][j] - sigma * mm[i][j];
        }
    }
    std::unique_ptr<DenseLU> lu;
    try {
        lu = std::make_unique<DenseLU>(shifted);
    } catch (const Error&) {
        throw Error("the shifted stiffness matrix is singular — the problem "
                    "may be ill-posed (check that p > 0 and w > 0)");
    }

    struct Mode {
        double lambda;
        Vector vec;
        bool converged;
    };
    std::vector<Mode> modes;
    std::vector<Vector> found;
    for (int mode = 0; mode < count; ++mode) {
        // Deterministic start biased toward the next expected shape.
        Vector x(mi, 0.0);
        for (std::size_t i = 0; i < mi; ++i) {
            x[i] = std::sin((mode + 1) * std::numbers::pi *
                            static_cast<double>(i + 1) /
                            static_cast<double>(mi + 1));
        }
        double lambda = 0.0;
        bool converged = false;
        for (int it = 0; it < 500; ++it) {
            // M-orthogonalize against the modes already found (deflation).
            for (const Vector& y : found) {
                const double c = m_dot(mm, x, y);
                for (std::size_t i = 0; i < mi; ++i) {
                    x[i] -= c * y[i];
                }
            }
            Vector y = lu->solve(mat_vec(mm, x));
            const double norm = std::sqrt(m_dot(mm, y, y));
            if (!(norm > 0.0) || !std::isfinite(norm)) {
                throw Error("inverse iteration broke down");
            }
            for (double& v : y) {
                v /= norm;
            }
            // Rayleigh quotient of the ORIGINAL (unshifted) operator gives
            // the true lambda; convergence is judged on it.
            const double next = m_dot(ki, y, y); // M-normalized
            const bool done =
                it > 0 && std::abs(next - lambda) <=
                              1e-11 * std::max(1.0, std::abs(next));
            lambda = next;
            x = std::move(y);
            if (done) {
                converged = true;
                break;
            }
        }
        // Sign convention: the largest-magnitude component is positive.
        const auto peak = std::max_element(
            x.begin(), x.end(),
            [](double u, double v) { return std::abs(u) < std::abs(v); });
        if (*peak < 0.0) {
            for (double& v : x) {
                v = -v;
            }
        }
        found.push_back(x);
        modes.push_back({lambda, x, converged});
    }
    // Inverse iteration finds them smallest-first by construction, but the
    // deflated starts can occasionally swap near-degenerate pairs: sort by
    // eigenvalue, THEN number the convergence warnings by final position.
    std::sort(modes.begin(), modes.end(),
              [](const Mode& p, const Mode& q) { return p.lambda < q.lambda; });
    ModesResult out;
    for (std::size_t idx = 0; idx < modes.size(); ++idx) {
        const Mode& mo = modes[idx];
        if (!mo.converged) {
            out.warnings.push_back(std::format(
                "mode {} did not fully converge in 500 iterations (clustered "
                "eigenvalues?)",
                idx + 1));
        }
        out.lambdas.push_back(mo.lambda);
        FemSolution sol;
        sol.degree = degree;
        sol.x = ks.nodes;
        sol.u.assign(m, 0.0);
        for (std::size_t i = 0; i < mi; ++i) {
            sol.u[i + 1] = mo.vec[i];
        }
        out.modes.push_back(std::move(sol));
    }
    return out;
}

} // namespace mathsolver::plugins::fem
