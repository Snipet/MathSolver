// Hybrid-systems plugin (docs/PLUGINS.md): event-driven simulation of a
// two-state flow with a guard surface and a reset map.
//
// Command:
//   hyb.sim <x'>; <v'>, <guard>, <reset x>; <reset v>, <x0>, <v0>, <T>
//       x' = f_x(t,x,v), v' = f_v(t,x,v); when the guard crosses > 0 → <= 0
//       the state resets. Bouncing ball:
//         hyb.sim v; -9.81, x, x; -0.8*v, 1, 0, 3

#include "hyb_core.hpp"

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
namespace hy = mathsolver::plugins::hyb;

constexpr std::size_t k_chart_points = 601;

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

/// "a; b" → the two trimmed halves (exactly one ';').
std::optional<std::pair<std::string, std::string>> split_pair(
    const std::string& s) {
    const auto pos = s.find(';');
    if (pos == std::string::npos || s.find(';', pos + 1) != std::string::npos) {
        return std::nullopt;
    }
    const auto trim = [](std::string t) {
        const auto b = t.find_first_not_of(" \t");
        const auto e = t.find_last_not_of(" \t");
        return b == std::string::npos ? std::string{}
                                      : t.substr(b, e - b + 1);
    };
    auto first = trim(s.substr(0, pos));
    auto second = trim(s.substr(pos + 1));
    if (first.empty() || second.empty()) {
        return std::nullopt;
    }
    return std::make_pair(std::move(first), std::move(second));
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

std::string events_table(const std::vector<hy::HybEvent>& events) {
    constexpr std::size_t k_show = 12;
    std::string rows = "[";
    for (std::size_t i = 0; i < events.size() && i < k_show; ++i) {
        const hy::HybEvent& e = events[i];
        if (i > 0) rows += ",";
        rows += std::format(
            "[{},{},{},{},{}]", jstr(std::format("{}", i + 1)),
            jstr(std::format("{:.6g}", e.t)),
            jstr(std::format("{:.6g}", e.x_before)),
            jstr(std::format("{:.6g}", e.v_before)),
            jstr(std::format("{:.6g}", e.v_after)));
    }
    rows += "]";
    std::string title = "Events";
    if (events.size() > k_show) {
        title = std::format("Events (first {} of {})", k_show, events.size());
    }
    return std::format(
        "{{\"type\":\"table\",\"title\":{},"
        "\"columns\":[\"#\",\"t\",\"x\",\"v before\",\"v after\"],"
        "\"rows\":{}}}",
        jstr(title), rows);
}

std::string trajectory_block(const hy::HybResult& r) {
    // Downsample to the chart budget; the reset discontinuities stay visible
    // through the event markers.
    std::vector<double> ts;
    std::vector<double> xs;
    std::vector<double> vs;
    const std::size_t n = r.t.size();
    const std::size_t stride =
        n > k_chart_points ? (n - 1) / (k_chart_points - 1) : 1;
    for (std::size_t i = 0; i < n; i += stride) {
        ts.push_back(r.t[i]);
        xs.push_back(r.x[i]);
        vs.push_back(r.v[i]);
    }
    if (ts.back() != r.t.back()) {
        ts.push_back(r.t.back());
        xs.push_back(r.x.back());
        vs.push_back(r.v.back());
    }
    std::string vlines;
    constexpr std::size_t k_marks = 8;
    if (!r.events.empty()) {
        vlines = ",\"vlines\":[";
        for (std::size_t i = 0; i < r.events.size() && i < k_marks; ++i) {
            if (i > 0) vlines += ",";
            vlines += std::format("{{\"x\":{},\"label\":{}}}",
                                  jnum(r.events[i].t),
                                  jstr(std::format("e{}", i + 1)));
        }
        vlines += "]";
    }
    return std::format(
        "{{\"type\":\"series\",\"title\":\"Trajectory\",\"xlabel\":\"t\","
        "\"ylabel\":\"state\",\"x\":{},\"series\":["
        "{{\"label\":\"x\",\"ys\":{}}},{{\"label\":\"v\",\"ys\":{}}}]{}}}",
        jarr(ts), jarr(xs), jarr(vs), vlines);
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

std::string cmd_sim(const std::vector<std::string>& args) {
    constexpr const char* usage =
        "usage: hyb.sim <x'>; <v'>, <guard>, <reset x>; <reset v>, "
        "<x0>, <v0>, <T>";
    if (args.size() != 6) {
        return error_json(usage);
    }
    const auto dynamics = split_pair(args[0]);
    const auto resets = split_pair(args[2]);
    if (!dynamics || !resets) {
        return error_json(usage);
    }
    const Expr fx = simplify(parse_expression(dynamics->first));
    const Expr fv = simplify(parse_expression(dynamics->second));
    const Expr guard = simplify(parse_expression(args[1]));
    const Expr rx = simplify(parse_expression(resets->first));
    const Expr rv = simplify(parse_expression(resets->second));
    const auto x0 = parse_number(args[3]);
    const auto v0 = parse_number(args[4]);
    const auto horizon = parse_number(args[5]);
    if (!x0 || !v0 || !horizon) {
        return error_json("x0, v0, T must be finite numbers");
    }

    const hy::HybResult r =
        hy::simulate(fx, fv, guard, rx, rv, *x0, *v0, *horizon);

    std::vector<std::pair<std::string, std::string>> kv{
        {"Dynamics",
         std::format("x' = {}, v' = {}", to_string(fx, PrintStyle::Plain),
                     to_string(fv, PrintStyle::Plain))},
        {"Guard (event when > 0 -> <= 0)",
         to_string(guard, PrintStyle::Plain)},
        {"Reset",
         std::format("x <- {}, v <- {}", to_string(rx, PrintStyle::Plain),
                     to_string(rv, PrintStyle::Plain))},
        {"Initial state", std::format("x = {:.6g}, v = {:.6g}", *x0, *v0)},
        {"Horizon", std::format("{:.6g} (RK4, 2000 steps)", *horizon)},
        {"Events", std::format("{}", r.events.size())}};
    if (!r.note.empty()) {
        kv.emplace_back(r.zeno ? "Zeno" : "Stopped early", r.note);
    }

    std::vector<std::string> blocks{kv_block(kv)};
    if (!r.events.empty()) {
        blocks.push_back(events_table(r.events));
    }
    blocks.push_back(trajectory_block(r));
    return envelope("hybrid simulation", blocks);
}

class HybPlugin final : public Plugin {
  public:
    std::string_view name() const override { return "hyb"; }
    std::string_view version() const override { return "0.1.0"; }
    std::string_view summary() const override {
        return "event-driven hybrid-system simulation: RK4 flow with guard "
               "events, reset maps, and Zeno detection";
    }
    std::vector<CommandInfo> commands() const override {
        return {
            {"sim",
             "Simulate x' = f_x, v' = f_v with a guard surface and reset map",
             "hyb.sim <x'>; <v'>, <guard>, <reset x>; <reset v>, <x0>, <v0>, <T>",
             "hyb.sim v; -9.81, x, x; -0.8*v, 1, 0, 3"},
        };
    }
    std::string invoke(std::string_view command,
                       const std::vector<std::string>& args) const override {
        try {
            if (command == "sim") return cmd_sim(args);
            return error_json(std::format("hyb has no command '{}'", command));
        } catch (const Error& e) {
            return error_json(e.what());
        } catch (const std::exception& e) {
            return error_json(std::format("hyb internal error: {}", e.what()));
        }
    }
};

} // namespace

namespace mathsolver::plugins {

std::unique_ptr<Plugin> make_hyb_plugin() {
    return std::make_unique<HybPlugin>();
}

} // namespace mathsolver::plugins
