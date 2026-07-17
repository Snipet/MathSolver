// Integral-equation plugin (docs/PLUGINS.md): second-kind Fredholm and
// Volterra equations with symbolic kernels, solved numerically.
//
// Commands:
//   ie.fredholm <K(x,t)>, <f(x)>, <lambda>, <a>, <b>
//       u(x) = f(x) + lambda * ∫_a^b K(x,t) u(t) dt   (Nyström / Simpson)
//   ie.volterra <K(x,t)>, <f(x)>, <lambda>, <a>, <b>
//       u(x) = f(x) + lambda * ∫_a^x K(x,t) u(t) dt   (trapezoidal marching)

#include "ie_core.hpp"

#include "mathsolver/mathsolver.hpp"
#include "mathsolver/plugin.hpp"

#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace mathsolver;
using namespace mathsolver::plugins;
namespace ei = mathsolver::plugins::ie;

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

std::string solution_block(const std::vector<double>& xs,
                           const std::vector<double>& us) {
    return std::format(
        "{{\"type\":\"series\",\"title\":\"Solution u(x)\",\"xlabel\":\"x\","
        "\"ylabel\":\"u(x)\",\"x\":{},\"series\":[{{\"label\":\"u\","
        "\"ys\":{}}}]}}",
        jarr(xs), jarr(us));
}

std::string sample_table(const std::vector<double>& xs,
                         const std::vector<double>& us) {
    std::string rows = "[";
    for (int k = 0; k <= 4; ++k) {
        const std::size_t i = (xs.size() - 1) * static_cast<std::size_t>(k) / 4;
        if (k > 0) rows += ",";
        rows += std::format("[{},{}]", jstr(std::format("{:.6g}", xs[i])),
                            jstr(std::format("{:.8g}", us[i])));
    }
    rows += "]";
    return std::format(
        "{{\"type\":\"table\",\"title\":\"Sample values\","
        "\"columns\":[\"x\",\"u(x)\"],\"rows\":{}}}",
        rows);
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

struct Inputs {
    Expr kernel;
    Expr f;
    double lambda = 0.0;
    double a = 0.0;
    double b = 0.0;
};

/// Shared argument parsing; returns an error envelope on failure.
std::optional<std::string> parse_inputs(const std::vector<std::string>& args,
                                        const char* usage, Inputs& in) {
    if (args.size() != 5) {
        return error_json(usage);
    }
    in.kernel = simplify(parse_expression(args[0]));
    in.f = simplify(parse_expression(args[1]));
    const auto lambda = parse_number(args[2]);
    const auto a = parse_number(args[3]);
    const auto b = parse_number(args[4]);
    if (!lambda || !a || !b) {
        return error_json("lambda, a, b must be finite numbers");
    }
    in.lambda = *lambda;
    in.a = *a;
    in.b = *b;
    return std::nullopt;
}

std::string cmd_fredholm(const std::vector<std::string>& args) {
    Inputs in;
    if (const auto err = parse_inputs(
            args, "usage: ie.fredholm <K(x,t)>, <f(x)>, <lambda>, <a>, <b>",
            in)) {
        return *err;
    }
    const ei::IeSolution s =
        ei::solve_fredholm(in.kernel, in.f, in.lambda, in.a, in.b);

    // Nyström interpolation gives a smooth chart between the 31 nodes.
    std::vector<double> xs;
    std::vector<double> us;
    for (int i = 0; i < k_chart_points; ++i) {
        const double x =
            in.a + (in.b - in.a) * i / (k_chart_points - 1);
        xs.push_back(x);
        us.push_back(ei::fredholm_eval(s, in.kernel, in.f, in.lambda, x));
    }
    const std::string kv = kv_block(
        {{"Equation", "u(x) = f(x) + lambda ∫_a^b K(x,t) u(t) dt"},
         {"Kernel K(x,t)", to_string(in.kernel, PrintStyle::Plain)},
         {"f(x)", to_string(in.f, PrintStyle::Plain)},
         {"lambda", std::format("{:.6g}", in.lambda)},
         {"Interval", std::format("[{:.6g}, {:.6g}]", in.a, in.b)},
         {"Method", std::format("Nyström, {}-node composite Simpson",
                                s.x.size())},
         {"Error estimate (vs half resolution)",
          std::format("{:.3g}", s.error_estimate)}});
    return envelope(
        "Fredholm integral equation (2nd kind)",
        {kv, sample_table(xs, us), solution_block(xs, us)});
}

std::string cmd_volterra(const std::vector<std::string>& args) {
    Inputs in;
    if (const auto err = parse_inputs(
            args, "usage: ie.volterra <K(x,t)>, <f(x)>, <lambda>, <a>, <b>",
            in)) {
        return *err;
    }
    const ei::IeSolution s =
        ei::solve_volterra(in.kernel, in.f, in.lambda, in.a, in.b);

    // Downsample the 201 marching nodes to the chart budget.
    std::vector<double> xs;
    std::vector<double> us;
    const std::size_t n = s.x.size();
    const std::size_t stride = n > k_chart_points ? (n - 1) / (k_chart_points - 1) : 1;
    for (std::size_t i = 0; i < n; i += stride) {
        xs.push_back(s.x[i]);
        us.push_back(s.u[i]);
    }
    if (xs.back() != s.x.back()) {
        xs.push_back(s.x.back());
        us.push_back(s.u.back());
    }
    const std::string kv = kv_block(
        {{"Equation", "u(x) = f(x) + lambda ∫_a^x K(x,t) u(t) dt"},
         {"Kernel K(x,t)", to_string(in.kernel, PrintStyle::Plain)},
         {"f(x)", to_string(in.f, PrintStyle::Plain)},
         {"lambda", std::format("{:.6g}", in.lambda)},
         {"Interval", std::format("[{:.6g}, {:.6g}]", in.a, in.b)},
         {"Method", std::format("trapezoidal marching, {} steps",
                                s.x.size() - 1)},
         {"Error estimate (vs half resolution)",
          std::format("{:.3g}", s.error_estimate)}});
    return envelope(
        "Volterra integral equation (2nd kind)",
        {kv, sample_table(xs, us), solution_block(xs, us)});
}

class IePlugin final : public Plugin {
  public:
    std::string_view name() const override { return "ie"; }
    std::string_view version() const override { return "0.1.0"; }
    std::string_view summary() const override {
        return "second-kind integral equations: Fredholm by Nyström "
               "quadrature, Volterra by trapezoidal marching";
    }
    std::vector<CommandInfo> commands() const override {
        return {
            {"fredholm",
             "Fredholm equation u = f + lambda ∫_a^b K(x,t) u(t) dt",
             "ie.fredholm <K(x,t)>, <f(x)>, <lambda>, <a>, <b>"},
            {"volterra",
             "Volterra equation u = f + lambda ∫_a^x K(x,t) u(t) dt",
             "ie.volterra <K(x,t)>, <f(x)>, <lambda>, <a>, <b>"},
        };
    }
    std::string invoke(std::string_view command,
                       const std::vector<std::string>& args) const override {
        try {
            if (command == "fredholm") return cmd_fredholm(args);
            if (command == "volterra") return cmd_volterra(args);
            return error_json(std::format("ie has no command '{}'", command));
        } catch (const Error& e) {
            return error_json(e.what());
        } catch (const std::exception& e) {
            return error_json(std::format("ie internal error: {}", e.what()));
        }
    }
};

} // namespace

namespace mathsolver::plugins {

std::unique_ptr<Plugin> make_ie_plugin() {
    return std::make_unique<IePlugin>();
}

} // namespace mathsolver::plugins
