// Linear-algebra plugin (docs/PLUGINS.md): dense numeric methods from
// linalg_core plus exact symbolic determinant/solve for small matrices with
// symbolic entries (fraction-free Bareiss over Expr arithmetic).
//
// Matrix syntax: rows separated by ';', entries by spaces or commas, with
// optional surrounding brackets — "[1 2; 3 4]" or "1,2;3,4". Entries are
// full expressions; a matrix whose entries all fold to numbers takes the
// numeric path, anything symbolic routes to the exact machinery (det and
// solve only).

#include "linalg_core.hpp"
#include "mathsolver/mathsolver.hpp"
#include "mathsolver/plugin.hpp"

#include <cctype>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace mathsolver;
using namespace mathsolver::plugins;
namespace la = mathsolver::plugins::linalg;

std::string jstr(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            default: out += c;
        }
    }
    return out + "\"";
}


std::string error_json(std::string_view message) {
    return std::format("{{\"ok\":false,\"error\":{}}}", jstr(std::string(message)));
}

std::string trim(std::string_view s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b])) != 0) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])) != 0) --e;
    return std::string(s.substr(b, e - b));
}

/// An Expr matrix parsed from "[a b; c d]" text.
using ExprMatrix = std::vector<std::vector<Expr>>;

ExprMatrix parse_matrix(std::string text) {
    text = trim(text);
    if (!text.empty() && text.front() == '[') {
        if (text.back() != ']') {
            throw Error("unbalanced brackets in the matrix");
        }
        text = trim(text.substr(1, text.size() - 2));
    }
    if (text.empty()) {
        throw Error("empty matrix");
    }
    ExprMatrix rows;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = start;
        int depth = 0;
        while (end < text.size() && (text[end] != ';' || depth > 0)) {
            if (text[end] == '(' || text[end] == '[') ++depth;
            if (text[end] == ')' || text[end] == ']') --depth;
            ++end;
        }
        const std::string row_text = trim(text.substr(start, end - start));
        if (!row_text.empty()) {
            // Entries split on commas, or on whitespace when there are none.
            std::vector<std::string> cells;
            if (row_text.find(',') != std::string::npos) {
                std::size_t cs = 0;
                int d2 = 0;
                for (std::size_t i = 0; i <= row_text.size(); ++i) {
                    if (i == row_text.size() ||
                        (row_text[i] == ',' && d2 == 0)) {
                        cells.push_back(trim(row_text.substr(cs, i - cs)));
                        cs = i + 1;
                    } else {
                        if (row_text[i] == '(' || row_text[i] == '[') ++d2;
                        if (row_text[i] == ')' || row_text[i] == ']') --d2;
                    }
                }
            } else {
                std::size_t cs = 0;
                int d2 = 0;
                for (std::size_t i = 0; i <= row_text.size(); ++i) {
                    const bool split =
                        i == row_text.size() ||
                        (std::isspace(static_cast<unsigned char>(row_text[i])) != 0 &&
                         d2 == 0);
                    if (split) {
                        const std::string cell = trim(row_text.substr(cs, i - cs));
                        if (!cell.empty()) {
                            cells.push_back(cell);
                        }
                        cs = i + 1;
                    } else {
                        if (row_text[i] == '(' || row_text[i] == '[') ++d2;
                        if (row_text[i] == ')' || row_text[i] == ']') --d2;
                    }
                }
            }
            std::vector<Expr> row;
            for (const std::string& cell : cells) {
                row.push_back(simplify(parse_expression(cell)));
            }
            if (!rows.empty() && row.size() != rows.front().size()) {
                throw Error(std::format(
                    "ragged matrix: row {} has {} entries, row 1 has {}",
                    rows.size() + 1, row.size(), rows.front().size()));
            }
            rows.push_back(std::move(row));
        }
        if (end >= text.size()) {
            break;
        }
        start = end + 1;
    }
    if (rows.empty()) {
        throw Error("empty matrix");
    }
    return rows;
}

/// Numeric view of an Expr matrix, or nullopt when any entry is symbolic.
std::optional<la::Matrix> numeric_matrix(const ExprMatrix& m) {
    la::Matrix out;
    for (const auto& row : m) {
        la::Vector r;
        for (const Expr& e : row) {
            try {
                r.push_back(evaluate(e, Bindings{}));
            } catch (const Error&) {
                return std::nullopt;
            }
        }
        out.push_back(std::move(r));
    }
    return out;
}

/// A vector argument is a 1-row or 1-column matrix.
la::Vector as_vector(const la::Matrix& m, const char* what) {
    if (m.size() == 1) {
        return m.front();
    }
    if (m.front().size() == 1) {
        la::Vector v;
        for (const auto& row : m) {
            v.push_back(row.front());
        }
        return v;
    }
    throw Error(std::format("{} must be a vector (one row or one column)", what));
}

std::string fmt(double v) {
    if (std::abs(v) < 5e-12) {
        v = 0.0; // don't print -0 or 1e-16 noise
    }
    return std::format("{:.6g}", v);
}

std::string matrix_table(const std::string& title, const la::Matrix& m) {
    std::string cols = "[\"row\"";
    for (std::size_t j = 0; j < m.front().size(); ++j) {
        cols += std::format(",\"c{}\"", j + 1);
    }
    cols += "]";
    std::string rows = "[";
    for (std::size_t i = 0; i < m.size(); ++i) {
        if (i > 0) rows += ",";
        rows += std::format("[{}", i + 1);
        for (const double v : m[i]) {
            rows += "," + jstr(fmt(v));
        }
        rows += "]";
    }
    rows += "]";
    return std::format(
        "{{\"type\":\"table\",\"title\":{},\"columns\":{},\"rows\":{}}}",
        jstr(title), cols, rows);
}

std::string kv_block(const std::vector<std::pair<std::string, std::string>>& items) {
    std::string out = "{\"type\":\"kv\",\"items\":[";
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) out += ",";
        out += std::format("[{},{}]", jstr(items[i].first), jstr(items[i].second));
    }
    return out + "]}";
}

std::string envelope(const std::string& title,
                     const std::vector<std::string>& blocks) {
    std::string b = "[";
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        if (i > 0) b += ",";
        b += blocks[i];
    }
    b += "]";
    return std::format("{{\"ok\":true,\"title\":{},\"blocks\":{}}}", jstr(title), b);
}

std::string vec_text(const la::Vector& v) {
    std::string out = "(";
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i > 0) out += ", ";
        out += fmt(v[i]);
    }
    return out + ")";
}

// --- symbolic determinant (fraction-free Bareiss over Expr) -----------------

Expr symbolic_det(const ExprMatrix& m_in) {
    const std::size_t n = m_in.size();
    for (const auto& row : m_in) {
        if (row.size() != n) {
            throw Error("det: the matrix must be square");
        }
    }
    if (n > 5) {
        throw Error("symbolic det is capped at 5x5 (numeric matrices go "
                    "further)");
    }
    ExprMatrix m = m_in;
    Expr prev = make_num(1);
    int sign = 1;
    for (std::size_t k = 0; k + 1 < n; ++k) {
        // Pivot: find a structurally nonzero entry in column k.
        if (m[k][k]->kind() == Kind::Number && m[k][k]->number().is_zero()) {
            std::size_t p = k + 1;
            while (p < n && m[p][k]->kind() == Kind::Number &&
                   m[p][k]->number().is_zero()) {
                ++p;
            }
            if (p == n) {
                return make_num(0);
            }
            std::swap(m[p], m[k]);
            sign = -sign;
        }
        for (std::size_t i = k + 1; i < n; ++i) {
            for (std::size_t j = k + 1; j < n; ++j) {
                // Bareiss: m[i][j] = (m[i][j] m[k][k] - m[i][k] m[k][j]) / prev
                m[i][j] = simplify(make_div(
                    make_sub(make_mul({m[i][j], m[k][k]}),
                             make_mul({m[i][k], m[k][j]})),
                    prev));
            }
        }
        prev = m[k][k];
    }
    Expr det = m[n - 1][n - 1];
    if (sign < 0) {
        det = simplify(make_neg(det));
    }
    return simplify(det);
}

// --- commands ---------------------------------------------------------------

std::string cmd_det(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        return error_json("usage: linalg.det [a b; c d]");
    }
    const ExprMatrix m = parse_matrix(args[0]);
    if (m.size() != m.front().size()) {
        return error_json("det: the matrix must be square");
    }
    const auto numeric = numeric_matrix(m);
    if (numeric) {
        const double d = la::determinant(*numeric);
        return envelope(std::format("det ({}x{})", m.size(), m.size()),
                        {kv_block({{"Determinant", fmt(d)}})});
    }
    const Expr d = symbolic_det(m);
    return envelope(
        std::format("det ({}x{}, symbolic)", m.size(), m.size()),
        {kv_block({{"Determinant", to_string(d, PrintStyle::Plain)}})});
}

std::string cmd_solve(const std::vector<std::string>& args) {
    if (args.size() != 2) {
        return error_json("usage: linalg.solve [A], [b]");
    }
    const ExprMatrix am = parse_matrix(args[0]);
    const ExprMatrix bm = parse_matrix(args[1]);
    const auto a = numeric_matrix(am);
    const auto b = numeric_matrix(bm);
    if (!a || !b) {
        return error_json("linalg.solve needs numeric entries (symbolic "
                          "systems: use the core `solve` with equations)");
    }
    const la::Vector x = la::lu_solve(*a, as_vector(*b, "b"));
    const la::Vector residual_v = la::matvec(*a, x);
    double residual = 0.0;
    const la::Vector bv = as_vector(*b, "b");
    for (std::size_t i = 0; i < bv.size(); ++i) {
        residual = std::max(residual, std::abs(residual_v[i] - bv[i]));
    }
    return envelope(
        std::format("solve ({}x{})", a->size(), a->size()),
        {kv_block({{"x", vec_text(x)},
                   {"Residual max|Ax - b|", std::format("{:.2e}", residual)}})});
}

std::string cmd_inv(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        return error_json("usage: linalg.inv [A]");
    }
    const auto a = numeric_matrix(parse_matrix(args[0]));
    if (!a) {
        return error_json("linalg.inv needs numeric entries");
    }
    const la::Matrix inv = la::inverse(*a);
    return envelope(std::format("inverse ({0}x{0})", a->size()),
                    {matrix_table("A^-1", inv),
                     kv_block({{"cond(A)", std::format("{:.4g}", la::cond(*a))}})});
}

std::string cmd_eig(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        return error_json("usage: linalg.eig [A]");
    }
    const auto a = numeric_matrix(parse_matrix(args[0]));
    if (!a) {
        return error_json("linalg.eig needs numeric entries");
    }
    const auto eig = la::eigenvalues(*a);
    std::string rows = "[";
    for (std::size_t i = 0; i < eig.size(); ++i) {
        if (i > 0) rows += ",";
        const double re = eig[i].real();
        const double im = eig[i].imag();
        std::string text = fmt(re);
        if (std::abs(im) > 5e-12) {
            text = std::format("{} {} {}i", fmt(re), im > 0 ? "+" : "-",
                               fmt(std::abs(im)));
        }
        rows += std::format("[{},{}]", i + 1, jstr(text));
    }
    rows += "]";
    double radius = 0.0;
    double eig_sum = 0.0;
    for (const auto& l : eig) {
        radius = std::max(radius, std::abs(l));
        eig_sum += l.real();
    }
    double trace = 0.0;
    for (std::size_t i = 0; i < a->size(); ++i) {
        trace += (*a)[i][i];
    }
    return envelope(
        std::format("eigenvalues ({0}x{0})", a->size()),
        {std::format("{{\"type\":\"table\",\"title\":\"Eigenvalues\","
                     "\"columns\":[\"#\",\"lambda\"],\"rows\":{}}}",
                     rows),
         kv_block({{"Spectral radius", fmt(radius)},
                   {"Sum (= trace)",
                    std::format("{} (trace {})", fmt(eig_sum), fmt(trace))}})});
}

std::string cmd_svd(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        return error_json("usage: linalg.svd [A]");
    }
    const auto a = numeric_matrix(parse_matrix(args[0]));
    if (!a) {
        return error_json("linalg.svd needs numeric entries");
    }
    const la::Svd s = la::svd(*a);
    std::string sig = "(";
    for (std::size_t i = 0; i < s.sigma.size(); ++i) {
        if (i > 0) sig += ", ";
        sig += fmt(s.sigma[i]);
    }
    sig += ")";
    return envelope(
        std::format("SVD ({}x{})", a->size(), a->front().size()),
        {kv_block({{"Singular values", sig},
                   {"Rank", std::to_string(la::rank(*a))},
                   {"cond(A)", std::format("{:.4g}", la::cond(*a))}}),
         matrix_table("U", s.u), matrix_table("V", s.v)});
}

std::string cmd_rank(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        return error_json("usage: linalg.rank [A]");
    }
    const auto a = numeric_matrix(parse_matrix(args[0]));
    if (!a) {
        return error_json("linalg.rank needs numeric entries");
    }
    return envelope("rank",
                    {kv_block({{"Rank", std::to_string(la::rank(*a))},
                               {"Rows", std::to_string(a->size())},
                               {"Columns", std::to_string(a->front().size())}})});
}

std::string cmd_lstsq(const std::vector<std::string>& args) {
    if (args.size() != 2) {
        return error_json("usage: linalg.lstsq [A], [b]");
    }
    const auto a = numeric_matrix(parse_matrix(args[0]));
    const auto bm = numeric_matrix(parse_matrix(args[1]));
    if (!a || !bm) {
        return error_json("linalg.lstsq needs numeric entries");
    }
    const la::Vector b = as_vector(*bm, "b");
    const la::Vector x = la::lstsq(*a, b);
    const la::Vector ax = la::matvec(*a, x);
    double rss = 0.0;
    for (std::size_t i = 0; i < b.size(); ++i) {
        rss += (ax[i] - b[i]) * (ax[i] - b[i]);
    }
    return envelope(
        std::format("least squares ({}x{})", a->size(), a->front().size()),
        {kv_block({{"x", vec_text(x)},
                   {"Residual ||Ax - b||", std::format("{:.6g}", std::sqrt(rss))},
                   {"Rank", std::to_string(la::rank(*a))}})});
}

class LinalgPlugin final : public Plugin {
  public:
    std::string_view name() const override { return "linalg"; }
    std::string_view version() const override { return "0.1.0"; }
    std::string_view summary() const override {
        return "Dense linear algebra: solve, det, inverse, eigenvalues, SVD, "
               "rank, least squares; symbolic det for small matrices";
    }
    std::vector<CommandInfo> commands() const override {
        return {
            {"solve", "Solve A x = b (LU with partial pivoting)",
             "linalg.solve [1 2; 3 4], [5 6]"},
            {"det", "Determinant (numeric LU, or exact Bareiss for symbolic "
                    "entries up to 5x5)",
             "linalg.det [a b; c d]"},
            {"inv", "Matrix inverse with condition number",
             "linalg.inv [2 1; 1 2]"},
            {"eig", "Eigenvalues incl. complex pairs (Hessenberg + shifted QR)",
             "linalg.eig [0 -1; 1 0]"},
            {"svd", "Singular value decomposition (one-sided Jacobi)",
             "linalg.svd [1 2; 3 4; 5 6]"},
            {"rank", "Numeric rank via SVD", "linalg.rank [1 2; 2 4]"},
            {"lstsq", "Least squares via the SVD pseudoinverse",
             "linalg.lstsq [1 0; 1 1; 1 2], [1 2 4]"},
        };
    }
    std::string invoke(std::string_view command,
                       const std::vector<std::string>& args) const override {
        try {
            if (command == "solve") return cmd_solve(args);
            if (command == "det") return cmd_det(args);
            if (command == "inv") return cmd_inv(args);
            if (command == "eig") return cmd_eig(args);
            if (command == "svd") return cmd_svd(args);
            if (command == "rank") return cmd_rank(args);
            if (command == "lstsq") return cmd_lstsq(args);
            return error_json(std::format("linalg has no command '{}'", command));
        } catch (const la::LinalgError& e) {
            return error_json(e.what());
        } catch (const Error& e) {
            return error_json(e.what());
        } catch (const std::exception& e) {
            return error_json(std::format("linalg internal error: {}", e.what()));
        }
    }
};

} // namespace

namespace mathsolver::plugins {

std::unique_ptr<Plugin> make_linalg_plugin() {
    return std::make_unique<LinalgPlugin>();
}

} // namespace mathsolver::plugins
