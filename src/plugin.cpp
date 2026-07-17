// Plugin registry (include/mathsolver/plugin.hpp).

#include "mathsolver/plugin.hpp"

#include <cmath>
#include <format>
#include <utility>

namespace mathsolver::plugins {

namespace {

std::vector<std::unique_ptr<Plugin>>& mutable_registry() {
    static std::vector<std::unique_ptr<Plugin>> plugins;
    return plugins;
}

} // namespace

void register_plugin(std::unique_ptr<Plugin> plugin) {
    auto& plugins = mutable_registry();
    for (auto& existing : plugins) {
        if (existing->name() == plugin->name()) {
            existing = std::move(plugin); // last registration wins, in place
            return;
        }
    }
    plugins.push_back(std::move(plugin));
}

const std::vector<std::unique_ptr<Plugin>>& registry() {
    return mutable_registry();
}

const Plugin* find(std::string_view name) {
    for (const auto& p : mutable_registry()) {
        if (p->name() == name) {
            return p.get();
        }
    }
    return nullptr;
}

std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out += std::format("\\u{:04x}", static_cast<unsigned char>(c));
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string jstr(std::string_view s) {
    return "\"" + json_escape(s) + "\"";
}

std::string jnum(double v) {
    if (!std::isfinite(v)) {
        return "null";
    }
    return std::format("{}", v);
}

std::string error_json(std::string_view message) {
    return std::format("{{\"ok\":false,\"error\":{}}}", jstr(message));
}

} // namespace mathsolver::plugins
