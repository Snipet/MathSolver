// linalg plugin tests: numerics by property (residuals, reconstruction,
// known spectra), command layer through the envelope.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <complex>
#include <string>
#include <vector>

#include "../plugins/linalg/linalg_core.hpp"
#include "mathsolver/plugin.hpp"

using namespace mathsolver::plugins;
namespace la = mathsolver::plugins::linalg;
using Catch::Matchers::ContainsSubstring;

namespace {

const Plugin& linalg_plugin() {
    register_builtin_plugins();
    const Plugin* p = find("linalg");
    REQUIRE(p != nullptr);
    return *p;
}

} // namespace

TEST_CASE("linalg: LU solve satisfies the system exactly") {
    const la::Matrix a{{2, 1, -1}, {-3, -1, 2}, {-2, 1, 2}};
    const la::Vector b{8, -11, -3};
    const la::Vector x = la::lu_solve(a, b);
    const la::Vector ax = la::matvec(a, x);
    for (std::size_t i = 0; i < b.size(); ++i) {
        CHECK(std::abs(ax[i] - b[i]) < 1e-10);
    }
    // Known solution (2, 3, -1).
    CHECK(std::abs(x[0] - 2.0) < 1e-10);
    CHECK(std::abs(x[1] - 3.0) < 1e-10);
    CHECK(std::abs(x[2] + 1.0) < 1e-10);
}

TEST_CASE("linalg: determinant and inverse") {
    const la::Matrix a{{4, 7}, {2, 6}};
    CHECK(std::abs(la::determinant(a) - 10.0) < 1e-12);
    const la::Matrix inv = la::inverse(a);
    const la::Matrix prod = la::matmul(a, inv);
    for (std::size_t i = 0; i < 2; ++i) {
        for (std::size_t j = 0; j < 2; ++j) {
            CHECK(std::abs(prod[i][j] - (i == j ? 1.0 : 0.0)) < 1e-12);
        }
    }
    CHECK_THROWS_AS(la::inverse(la::Matrix{{1, 2}, {2, 4}}), la::LinalgError);
    CHECK(std::abs(la::determinant(la::Matrix{{1, 2}, {2, 4}})) < 1e-12);
}

TEST_CASE("linalg: eigenvalues of symmetric, defective, and rotation matrices") {
    // Symmetric: [[2,1],[1,2]] -> {3, 1}.
    auto e1 = la::eigenvalues({{2, 1}, {1, 2}});
    REQUIRE(e1.size() == 2);
    CHECK(std::abs(e1[0].real() - 3.0) < 1e-9);
    CHECK(std::abs(e1[1].real() - 1.0) < 1e-9);
    // Rotation: [[0,-1],[1,0]] -> ±i.
    auto e2 = la::eigenvalues({{0, -1}, {1, 0}});
    REQUIRE(e2.size() == 2);
    CHECK(std::abs(e2[0].imag() - 1.0) < 1e-9);
    CHECK(std::abs(e2[1].imag() + 1.0) < 1e-9);
    CHECK(std::abs(e2[0].real()) < 1e-9);
    // 3x3 with known spectrum: diag-dominant companion-style.
    auto e3 = la::eigenvalues({{6, -1, 0}, {-1, 5, -1}, {0, -1, 4}});
    double sum = 0;
    for (const auto& l : e3) {
        sum += l.real();
        CHECK(std::abs(l.imag()) < 1e-9); // symmetric: all real
    }
    CHECK(std::abs(sum - 15.0) < 1e-8); // trace
    // 4x4 with two complex pairs.
    auto e4 = la::eigenvalues(
        {{0, -1, 0, 0}, {1, 0, 0, 0}, {0, 0, 0, -2}, {0, 0, 2, 0}});
    int complex_count = 0;
    for (const auto& l : e4) {
        if (std::abs(l.imag()) > 1e-9) ++complex_count;
    }
    CHECK(complex_count == 4);
}

TEST_CASE("linalg: SVD reconstructs the matrix") {
    const la::Matrix a{{1, 2}, {3, 4}, {5, 6}};
    const la::Svd s = la::svd(a);
    REQUIRE(s.sigma.size() == 2);
    CHECK(s.sigma[0] >= s.sigma[1]);
    // A ≈ U Σ Vᵀ.
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 2; ++j) {
            double r = 0.0;
            for (std::size_t k = 0; k < 2; ++k) {
                r += s.u[i][k] * s.sigma[k] * s.v[j][k];
            }
            CHECK(std::abs(r - a[i][j]) < 1e-10);
        }
    }
    // Wide matrices go through the transpose path.
    const la::Svd sw = la::svd(la::transpose(a));
    CHECK(std::abs(sw.sigma[0] - s.sigma[0]) < 1e-10);
}

TEST_CASE("linalg: rank, cond, and least squares") {
    CHECK(la::rank({{1, 2}, {2, 4}}) == 1);
    CHECK(la::rank({{1, 0}, {0, 1}}) == 2);
    CHECK(la::cond({{1, 0}, {0, 1}}) == 1.0);
    // Fit y = 1 + 3x/2 through (0,1), (1,2), (2,4) in least squares:
    // x = (11/12... ) — verify by orthogonality of the residual instead.
    const la::Matrix a{{1, 0}, {1, 1}, {1, 2}};
    const la::Vector b{1, 2, 4};
    const la::Vector x = la::lstsq(a, b);
    const la::Vector ax = la::matvec(a, x);
    la::Vector resid(3);
    for (std::size_t i = 0; i < 3; ++i) {
        resid[i] = b[i] - ax[i];
    }
    // Residual orthogonal to the column space.
    const la::Matrix at = la::transpose(a);
    const la::Vector atr = la::matvec(at, resid);
    CHECK(std::abs(atr[0]) < 1e-10);
    CHECK(std::abs(atr[1]) < 1e-10);
    // Slope 3/2, intercept 5/6 (the classic normal-equation answer).
    CHECK(std::abs(x[1] - 1.5) < 1e-10);
    CHECK(std::abs(x[0] - 5.0 / 6.0) < 1e-10);
}

// ---------------------------------------------------------------------------
// Command layer
// ---------------------------------------------------------------------------

TEST_CASE("linalg plugin: commands and matrix parsing") {
    const Plugin& p = linalg_plugin();
    CHECK(p.commands().size() == 7);

    const std::string solve =
        p.invoke("solve", {"[2 1; 1 3]", "[3 5]"});
    CHECK_THAT(solve, ContainsSubstring("\"ok\":true"));
    CHECK_THAT(solve, ContainsSubstring("(0.8, 1.4)"));

    // Comma-separated entries and no brackets both parse.
    const std::string det = p.invoke("det", {"4,7;2,6"});
    CHECK_THAT(det, ContainsSubstring("\"10\""));

    // Symbolic determinant.
    const std::string sym = p.invoke("det", {"[a b; c d]"});
    CHECK_THAT(sym, ContainsSubstring("a*d"));
    CHECK_THAT(sym, ContainsSubstring("b*c"));

    const std::string eig = p.invoke("eig", {"[0 -1; 1 0]"});
    CHECK_THAT(eig, ContainsSubstring("i"));

    const std::string rank = p.invoke("rank", {"[1 2; 2 4]"});
    CHECK_THAT(rank, ContainsSubstring("[\"Rank\",\"1\"]"));

    const std::string err = p.invoke("solve", {"[1 2; 2 4]", "[1 1]"});
    CHECK_THAT(err, ContainsSubstring("singular"));

    const std::string ragged = p.invoke("det", {"[1 2; 3]"});
    CHECK_THAT(ragged, ContainsSubstring("ragged"));
}
