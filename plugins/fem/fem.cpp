// Finite-element plugin (docs/PLUGINS.md): 1-D Sturm–Liouville problems
// -(p(x) u')' + q(x) u = f(x) by Galerkin FEM with P1/P2 elements.
//
// Commands:
//   fem.bvp <p(x)>, <q(x)>, <f(x)>, <a>, <b>, <left bc>, <right bc>[, <p1|p2>[, <elements>]]
//       Boundary conditions are written u=<value> (Dirichlet) or
//       u'=<value> (natural flux p u' = value).
//   fem.modes <p(x)>, <q(x)>, <w(x)>, <a>, <b>[, <count>[, <p1|p2>[, <elements>]]]
//       Smallest eigenpairs of -(p u')' + q u = lambda w u, u(a)=u(b)=0.

#include "fem_core.hpp"

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
namespace fe = mathsolver::plugins::fem;

constexpr int k_chart_points = 121;

/// Numeric argument through the CAS, so "pi", "1/2", "-3/4" all work.
std::optional<double> parse_number(const std::string& s) {
    try {
        const double v = evaluate(simplify(parse_expression(s)), Bindings{});
        if (!std::isfinite(v)) {
            return std::nullopt;
        }
        return v;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t");
    const auto e = s.find_last_not_of(" \t");
    return b == std::string::npos ? std::string{} : s.substr(b, e - b + 1);
}

/// "u=1" or "u'=0" (spaces allowed around '=').
std::optional<fe::Bc> parse_bc(const std::string& text) {
    std::string t = trim(text);
    fe::Bc bc;
    if (t.rfind("u'", 0) == 0) {
        bc.dirichlet = false;
        t = trim(t.substr(2));
    } else if (t.rfind("u", 0) == 0) {
        bc.dirichlet = true;
        t = trim(t.substr(1));
    } else {
        return std::nullopt;
    }
    if (t.empty() || t.front() != '=') {
        return std::nullopt;
    }
    const auto v = parse_number(trim(t.substr(1)));
    if (!v) {
        return std::nullopt;
    }
    bc.value = *v;
    return bc;
}

std::string jarr(const std::vector<double>& v) {
    std::string out = "[";
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i > 0) out += ",";
        out += jnum(v[i]);
    }
    return out + "]";
}

std::string kv_block(const std::vector<std::pair<std::string, std::string>>& items) {
    std::string out = "{\"type\":\"kv\",\"items\":[";
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) out += ",";
        out += std::format("[{},{}]", jstr(items[i].first),
                           jstr(items[i].second));
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
    return std::format("{{\"ok\":true,\"title\":{},\"blocks\":{}}}",
                       jstr(title), b);
}

std::string bc_text(const fe::Bc& bc) {
    return std::format("{} = {:.6g}", bc.dirichlet ? "u" : "p u'", bc.value);
}

std::string cmd_bvp(const std::vector<std::string>& args) {
    constexpr const char* usage =
        "usage: fem.bvp <p(x)>, <q(x)>, <f(x)>, <a>, <b>, <u=v|u'=v>, "
        "<u=v|u'=v>[, <p1|p2>[, <elements>]]";
    if (args.size() < 7 || args.size() > 9) {
        return error_json(usage);
    }
    const Expr p = simplify(parse_expression(args[0]));
    const Expr q = simplify(parse_expression(args[1]));
    const Expr f = simplify(parse_expression(args[2]));
    const auto a = parse_number(args[3]);
    const auto b = parse_number(args[4]);
    if (!a || !b) {
        return error_json("a and b must be finite numbers");
    }
    const auto left = parse_bc(args[5]);
    const auto right = parse_bc(args[6]);
    if (!left || !right) {
        return error_json(
            "boundary conditions are u=<value> (Dirichlet) or u'=<value> "
            "(natural flux p u' = value)");
    }
    int degree = 1;
    if (args.size() >= 8) {
        const std::string d = trim(args[7]);
        if (d == "p1") {
            degree = 1;
        } else if (d == "p2") {
            degree = 2;
        } else {
            return error_json("element type must be p1 or p2");
        }
    }
    int elements = 32;
    if (args.size() == 9) {
        const auto n = parse_number(args[8]);
        if (!n || *n != std::floor(*n) || *n < 1 || *n > 1e7) {
            return error_json("element count must be a positive integer");
        }
        elements = static_cast<int>(*n);
    }
    const fe::BvpResult r =
        fe::solve_bvp(p, q, f, *a, *b, *left, *right, degree, elements);

    std::vector<double> xs;
    std::vector<double> us;
    for (int i = 0; i < k_chart_points; ++i) {
        const double x = *a + (*b - *a) * i / (k_chart_points - 1);
        xs.push_back(x);
        us.push_back(fe::fem_eval(r.solution, x));
    }
    const std::string chart = std::format(
        "{{\"type\":\"series\",\"title\":\"Solution u(x)\",\"xlabel\":\"x\","
        "\"ylabel\":\"u(x)\",\"x\":{},\"series\":[{{\"label\":\"u\","
        "\"ys\":{}}}]}}",
        jarr(xs), jarr(us));
    const std::string order_text =
        std::isnan(r.observed_order)
            ? "exact at this resolution"
            : std::format("{:.2f} (theory: {})", r.observed_order,
                          degree + 1);
    std::vector<std::pair<std::string, std::string>> kv{
        {"Equation", "-(p u')' + q u = f on [a, b]"},
        {"p / q / f",
         std::format("{} / {} / {}", to_string(p, PrintStyle::Plain),
                     to_string(q, PrintStyle::Plain),
                     to_string(f, PrintStyle::Plain))},
        {"Interval", std::format("[{:.6g}, {:.6g}]", *a, *b)},
        {"Boundary conditions",
         std::format("left {}, right {}", bc_text(*left), bc_text(*right))},
        {"Elements",
         std::format("{} P{} (refined to {} for the estimate)", elements,
                     degree, 4 * elements)},
        {"Error estimate (vs refinement)",
         std::format("{:.3g}", r.error_estimate)},
        {"Observed convergence order", order_text}};
    for (const std::string& w : r.warnings) {
        kv.emplace_back("Note", w);
    }
    return envelope(
        std::format("FEM boundary-value problem ({} P{} elements)",
                    4 * elements, degree),
        {kv_block(kv), chart});
}

std::string cmd_modes(const std::vector<std::string>& args) {
    constexpr const char* usage =
        "usage: fem.modes <p(x)>, <q(x)>, <w(x)>, <a>, <b>[, <count>[, "
        "<p1|p2>[, <elements>]]]";
    if (args.size() < 5 || args.size() > 8) {
        return error_json(usage);
    }
    const Expr p = simplify(parse_expression(args[0]));
    const Expr q = simplify(parse_expression(args[1]));
    const Expr w = simplify(parse_expression(args[2]));
    const auto a = parse_number(args[3]);
    const auto b = parse_number(args[4]);
    if (!a || !b) {
        return error_json("a and b must be finite numbers");
    }
    int count = 4;
    if (args.size() >= 6) {
        const auto n = parse_number(args[5]);
        if (!n || *n != std::floor(*n) || *n < 1 || *n > 1e7) {
            return error_json("mode count must be a positive integer");
        }
        count = static_cast<int>(*n);
    }
    int degree = 1;
    if (args.size() >= 7) {
        const std::string d = trim(args[6]);
        if (d == "p1") {
            degree = 1;
        } else if (d == "p2") {
            degree = 2;
        } else {
            return error_json("element type must be p1 or p2");
        }
    }
    int elements = 128;
    if (args.size() == 8) {
        const auto n = parse_number(args[7]);
        if (!n || *n != std::floor(*n) || *n < 1 || *n > 1e7) {
            return error_json("element count must be a positive integer");
        }
        elements = static_cast<int>(*n);
    }
    const fe::ModesResult r =
        fe::solve_modes(p, q, w, *a, *b, count, degree, elements);

    std::string rows = "[";
    for (std::size_t i = 0; i < r.lambdas.size(); ++i) {
        if (i > 0) rows += ",";
        const double l = r.lambdas[i];
        rows += std::format(
            "[{},{},{}]", i + 1, jstr(std::format("{:.8g}", l)),
            jstr(l >= 0.0 ? std::format("{:.8g}", std::sqrt(l)) : "-"));
    }
    rows += "]";
    const std::string table = std::format(
        "{{\"type\":\"table\",\"title\":\"Eigenvalues\","
        "\"columns\":[\"n\",\"lambda_n\",\"sqrt(lambda_n)\"],\"rows\":{}}}",
        rows);

    std::vector<double> xs;
    for (int i = 0; i < k_chart_points; ++i) {
        xs.push_back(*a + (*b - *a) * i / (k_chart_points - 1));
    }
    std::string series = "[";
    for (std::size_t k = 0; k < r.modes.size(); ++k) {
        if (k > 0) series += ",";
        std::vector<double> ys;
        for (const double x : xs) {
            ys.push_back(fe::fem_eval(r.modes[k], x));
        }
        series += std::format("{{\"label\":{},\"ys\":{}}}",
                              jstr(std::format("mode {}", k + 1)), jarr(ys));
    }
    series += "]";
    const std::string chart = std::format(
        "{{\"type\":\"series\",\"title\":\"Eigenmodes (M-normalized)\","
        "\"xlabel\":\"x\",\"ylabel\":\"u_n(x)\",\"x\":{},\"series\":{}}}",
        jarr(xs), series);

    std::vector<std::pair<std::string, std::string>> kv{
        {"Problem", "-(p u')' + q u = lambda w u, u(a) = u(b) = 0"},
        {"p / q / w",
         std::format("{} / {} / {}", to_string(p, PrintStyle::Plain),
                     to_string(q, PrintStyle::Plain),
                     to_string(w, PrintStyle::Plain))},
        {"Interval", std::format("[{:.6g}, {:.6g}]", *a, *b)},
        {"Discretization", std::format("{} P{} elements", elements, degree)},
        {"Method",
         "generalized eigenproblem K u = lambda M u, inverse iteration with "
         "M-orthogonal deflation"}};
    for (const std::string& warn : r.warnings) {
        kv.emplace_back("Warning", warn);
    }
    return envelope(
        std::format("Sturm–Liouville modes on [{:.4g}, {:.4g}]", *a, *b),
        {kv_block(kv), table, chart});
}

class FemPlugin final : public Plugin {
  public:
    std::string_view name() const override { return "fem"; }
    std::string_view version() const override { return "0.1.0"; }
    std::string_view summary() const override {
        return "1-D finite elements: Sturm–Liouville boundary-value problems "
               "and eigenmodes with P1/P2 Galerkin assembly";
    }
    std::vector<CommandInfo> commands() const override {
        return {
            {"bvp",
             "Solve -(p u')' + q u = f with Dirichlet (u=v) or flux (u'=v) "
             "ends; reports the observed convergence order",
             "fem.bvp <p(x)>, <q(x)>, <f(x)>, <a>, <b>, <u=v|u'=v>, "
             "<u=v|u'=v>[, <p1|p2>[, <elements>]]",
             "fem.bvp 1, 0, pi^2*sin(pi*x), 0, 1, u=0, u=0"},
            {"modes",
             "Smallest eigenpairs of -(p u')' + q u = lambda w u with "
             "u(a) = u(b) = 0",
             "fem.modes <p(x)>, <q(x)>, <w(x)>, <a>, <b>[, <count>[, "
             "<p1|p2>[, <elements>]]]",
             "fem.modes 1, 0, 1, 0, pi"},
        };
    }
    std::string invoke(std::string_view command,
                       const std::vector<std::string>& args) const override {
        try {
            if (command == "bvp") return cmd_bvp(args);
            if (command == "modes") return cmd_modes(args);
            return error_json(std::format("fem has no command '{}'", command));
        } catch (const Error& e) {
            return error_json(e.what());
        } catch (const std::exception& e) {
            return error_json(std::format("fem internal error: {}", e.what()));
        }
    }
};

} // namespace

namespace mathsolver::plugins {

std::unique_ptr<Plugin> make_fem_plugin() {
    return std::make_unique<FemPlugin>();
}

} // namespace mathsolver::plugins
