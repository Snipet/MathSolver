// PDE plugin (docs/PLUGINS.md): separation-of-variables solutions of the
// classic 1-D boundary-value problems on [0, L] with homogeneous Dirichlet
// conditions, visualized as profile evolution.
//
// Commands:
//   pde.heat <L>, <alpha>, <f(x)>[, <T>]
//       u_t = alpha u_xx, u(0,t)=u(L,t)=0, u(x,0)=f(x).
//   pde.wave <L>, <c>, <f(x)>[, <g(x)>[, <T>]]
//       u_tt = c^2 u_xx, displacement f, velocity g (default 0).

#include "pde_core.hpp"

#include "mathsolver/mathsolver.hpp"
#include "mathsolver/plugin.hpp"

#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace mathsolver;
using namespace mathsolver::plugins;
namespace pd = mathsolver::plugins::pde;

constexpr int k_modes = 24;
constexpr int k_points = 121;

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

std::string jnum(double v) {
    if (!std::isfinite(v)) {
        return "null";
    }
    return std::format("{}", v);
}

std::string error_json(std::string_view message) {
    return std::format("{{\"ok\":false,\"error\":{}}}",
                       jstr(std::string(message)));
}

std::optional<double> parse_double(const std::string& s) {
    try {
        std::size_t pos = 0;
        const double v = std::stod(s, &pos);
        while (pos < s.size() &&
               std::isspace(static_cast<unsigned char>(s[pos])) != 0) {
            ++pos;
        }
        if (pos != s.size()) {
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

/// Profile chart: u(x, t_k) curves for the given snapshot times.
std::string profile_block(const std::string& title, double length,
                          const std::vector<double>& times,
                          const std::vector<std::vector<double>>& profiles) {
    std::vector<double> xs;
    for (int i = 0; i < k_points; ++i) {
        xs.push_back(length * i / (k_points - 1));
    }
    std::string series = "[";
    for (std::size_t k = 0; k < times.size(); ++k) {
        if (k > 0) series += ",";
        series += std::format("{{\"label\":{},\"ys\":{}}}",
                              jstr(std::format("t = {:.4g}", times[k])),
                              jarr(profiles[k]));
    }
    series += "]";
    return std::format(
        "{{\"type\":\"series\",\"title\":{},\"xlabel\":\"x\","
        "\"ylabel\":\"u(x, t)\",\"x\":{},\"series\":{}}}",
        jstr(title), jarr(xs), series);
}

std::string coeff_table(const pd::SineSeries& s, int show) {
    std::string rows = "[";
    for (int n = 0; n < show && n < static_cast<int>(s.b.size()); ++n) {
        if (n > 0) rows += ",";
        rows += std::format("[{},{}]", n + 1,
                            jstr(std::format("{:.6g}", s.b[static_cast<std::size_t>(n)])));
    }
    rows += "]";
    return std::format(
        "{{\"type\":\"table\",\"title\":\"Sine coefficients b_n\","
        "\"columns\":[\"n\",\"b_n\"],\"rows\":{}}}",
        rows);
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

std::string cmd_heat(const std::vector<std::string>& args) {
    if (args.size() < 3 || args.size() > 4) {
        return error_json("usage: pde.heat <L>, <alpha>, <f(x)>[, <T>]");
    }
    const auto length = parse_double(args[0]);
    const auto alpha = parse_double(args[1]);
    if (!length || !(*length > 0) || !alpha || !(*alpha > 0)) {
        return error_json("L and alpha must be positive numbers");
    }
    const Expr f = simplify(parse_expression(args[2]));
    const pd::SineSeries s = pd::sine_coefficients(f, "x", *length, k_modes);

    // Default horizon: mode 1 decayed to 10%.
    const double w1 = std::numbers::pi / *length;
    double horizon = std::log(10.0) / (*alpha * w1 * w1);
    if (args.size() == 4) {
        const auto T = parse_double(args[3]);
        if (!T || !(*T > 0)) {
            return error_json("T must be a positive number");
        }
        horizon = *T;
    }
    const std::vector<double> times{0.0, horizon / 16.0, horizon / 4.0, horizon};
    std::vector<std::vector<double>> profiles;
    for (const double t : times) {
        std::vector<double> row;
        for (int i = 0; i < k_points; ++i) {
            const double x = *length * i / (k_points - 1);
            row.push_back(pd::heat_eval(s, *length, *alpha, x, t));
        }
        profiles.push_back(std::move(row));
    }
    const std::string kv = kv_block(
        {{"Equation", "u_t = alpha u_xx, u(0)=u(L)=0"},
         {"L / alpha", std::format("{:.6g} / {:.6g}", *length, *alpha)},
         {"Initial profile", to_string(f, PrintStyle::Plain)},
         {"Modes", std::format("{} ({} coefficients exact)", k_modes,
                               s.exact_count)},
         {"Mode-1 time constant",
          std::format("{:.6g}", 1.0 / (*alpha * w1 * w1))}});
    return envelope(
        std::format("heat equation on [0, {:.4g}]", *length),
        {kv, coeff_table(s, 10),
         profile_block("Temperature profiles", *length, times, profiles)});
}

std::string cmd_wave(const std::vector<std::string>& args) {
    if (args.size() < 3 || args.size() > 5) {
        return error_json(
            "usage: pde.wave <L>, <c>, <f(x)>[, <g(x)>[, <T>]]");
    }
    const auto length = parse_double(args[0]);
    const auto c = parse_double(args[1]);
    if (!length || !(*length > 0) || !c || !(*c > 0)) {
        return error_json("L and c must be positive numbers");
    }
    const Expr f = simplify(parse_expression(args[2]));
    const pd::SineSeries a = pd::sine_coefficients(f, "x", *length, k_modes);
    pd::SineSeries g;
    if (args.size() >= 4 && !args[3].empty()) {
        g = pd::sine_coefficients(simplify(parse_expression(args[3])), "x",
                                  *length, k_modes);
    }
    const double period = 2.0 * *length / *c; // fundamental period
    double horizon = period / 2.0;
    if (args.size() == 5) {
        const auto T = parse_double(args[4]);
        if (!T || !(*T > 0)) {
            return error_json("T must be a positive number");
        }
        horizon = *T;
    }
    const std::vector<double> times{0.0, horizon / 4.0, horizon / 2.0,
                                    3.0 * horizon / 4.0, horizon};
    std::vector<std::vector<double>> profiles;
    for (const double t : times) {
        std::vector<double> row;
        for (int i = 0; i < k_points; ++i) {
            const double x = *length * i / (k_points - 1);
            row.push_back(pd::wave_eval(a, g, *length, *c, x, t));
        }
        profiles.push_back(std::move(row));
    }
    const std::string kv = kv_block(
        {{"Equation", "u_tt = c^2 u_xx, u(0)=u(L)=0"},
         {"L / c", std::format("{:.6g} / {:.6g}", *length, *c)},
         {"Initial displacement", to_string(f, PrintStyle::Plain)},
         {"Fundamental period", std::format("{:.6g}", period)},
         {"Modes", std::format("{} ({} exact)", k_modes, a.exact_count)}});
    return envelope(
        std::format("wave equation on [0, {:.4g}]", *length),
        {kv, coeff_table(a, 10),
         profile_block("Displacement profiles", *length, times, profiles)});
}

class PdePlugin final : public Plugin {
  public:
    std::string_view name() const override { return "pde"; }
    std::string_view version() const override { return "0.1.0"; }
    std::string_view summary() const override {
        return "1-D PDE boundary-value problems by separation of variables: "
               "heat and wave equations on [0, L] with Dirichlet conditions";
    }
    std::vector<CommandInfo> commands() const override {
        return {
            {"heat",
             "Heat equation u_t = alpha u_xx with u(x,0) = f(x)",
             "pde.heat <L>, <alpha>, <f(x)>[, <T>]",
             "pde.heat 1, 1, x*(1-x)"},
            {"wave",
             "Wave equation u_tt = c^2 u_xx with displacement f, velocity g",
             "pde.wave <L>, <c>, <f(x)>[, <g(x)>[, <T>]]",
             "pde.wave 1, 2, sin(pi*x)"},
        };
    }
    std::string invoke(std::string_view command,
                       const std::vector<std::string>& args) const override {
        try {
            if (command == "heat") return cmd_heat(args);
            if (command == "wave") return cmd_wave(args);
            return error_json(std::format("pde has no command '{}'", command));
        } catch (const Error& e) {
            return error_json(e.what());
        } catch (const std::exception& e) {
            return error_json(std::format("pde internal error: {}", e.what()));
        }
    }
};

} // namespace

namespace mathsolver::plugins {

std::unique_ptr<Plugin> make_pde_plugin() {
    return std::make_unique<PdePlugin>();
}

} // namespace mathsolver::plugins
