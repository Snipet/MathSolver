#pragma once

// Plugin framework (docs/PLUGINS.md).
//
// A plugin packages domain-specific computations — typically numeric-heavy
// code (DSP, statistics, linear algebra) that benefits from running as native
// C++/WASM rather than being expressed through the CAS — behind a uniform,
// string-in/JSON-out command surface:
//
//   - Plugins are compiled in (registered at startup via
//     register_builtin_plugins()); there is no dynamic loading.
//   - Each plugin exposes named commands. The host (WASM bindings, and in the
//     future the CLI/REPL) splits arguments and passes them as strings.
//   - A command returns a complete JSON object. Success envelopes carry
//     ok:true, a display title, and a list of declarative UI *blocks* the
//     website renders generically (no plugin-specific frontend code):
//
//       {"ok":true,"title":"...","blocks":[
//         {"type":"kv","items":[["label","value"],...]},
//         {"type":"table","title":"...","columns":[...],"rows":[[...],...]},
//         {"type":"series","title":"...","xlabel":"...","ylabel":"...",
//          "logx":true,"x":[...],"series":[{"label":"...","ys":[...]}]},
//         {"type":"text","lines":["..."]}
//       ]}
//
//     Failures carry {"ok":false,"error":"..."} (use error_json()).
//     No exception may escape invoke(); the host also guards.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mathsolver::plugins {

struct CommandInfo {
    std::string name;    ///< Verb, e.g. "butter" (invoked as "dsp.butter").
    std::string summary; ///< One-line description for catalogs/help.
    std::string usage;   ///< Argument synopsis, e.g. "dsp.butter <lowpass|highpass>, <order>, <fc>, <fs>".
};

class Plugin {
  public:
    virtual ~Plugin() = default;
    virtual std::string_view name() const = 0;    ///< Namespace, e.g. "dsp".
    virtual std::string_view version() const = 0; ///< Plugin's own semver.
    virtual std::string_view summary() const = 0; ///< One-line description.
    virtual std::vector<CommandInfo> commands() const = 0;
    /// Run `command` with caller-split argument strings; returns a complete
    /// JSON object (see the envelope contract above). Must not throw.
    virtual std::string invoke(std::string_view command,
                               const std::vector<std::string>& args) const = 0;
};

/// Register a plugin. A plugin with the same name replaces the earlier one
/// (last registration wins), so re-registration is safe.
void register_plugin(std::unique_ptr<Plugin> plugin);

/// All registered plugins, in registration order.
const std::vector<std::unique_ptr<Plugin>>& registry();

/// The registered plugin with this name, or nullptr.
const Plugin* find(std::string_view name);

/// Registers every built-in plugin (defined in plugins/register_builtin.cpp;
/// hosts that link the mathsolver_plugins library call this once at startup).
/// Idempotent.
void register_builtin_plugins();

// --- JSON writing helpers for plugin authors (same escaping rules as the
// --- WASM binding layer: strings escaped, non-finite doubles become null).

std::string json_escape(std::string_view s);
std::string jstr(std::string_view s);
std::string jnum(double v);
/// {"ok":false,"error":"..."} with the message escaped.
std::string error_json(std::string_view message);

} // namespace mathsolver::plugins
