// Built-in plugin registration (mathsolver/plugin.hpp).
//
// Every compiled-in plugin is listed here; hosts (the WASM bindings, tests)
// call register_builtin_plugins() once at startup. Explicit registration —
// rather than static-initializer magic — keeps linker section-GC from
// silently dropping plugins and makes the load order deterministic.

#include "mathsolver/plugin.hpp"

namespace mathsolver::plugins {

std::unique_ptr<Plugin> make_dsp_plugin(); // plugins/dsp/dsp.cpp

void register_builtin_plugins() {
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    register_plugin(make_dsp_plugin());
}

} // namespace mathsolver::plugins
