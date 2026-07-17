// Built-in plugin registration (mathsolver/plugin.hpp).
//
// Every compiled-in plugin is listed here; hosts (the WASM bindings, tests)
// call register_builtin_plugins() once at startup. Explicit registration —
// rather than static-initializer magic — keeps linker section-GC from
// silently dropping plugins and makes the load order deterministic.

#include "mathsolver/plugin.hpp"

namespace mathsolver::plugins {

std::unique_ptr<Plugin> make_dsp_plugin();    // plugins/dsp/dsp.cpp
std::unique_ptr<Plugin> make_sys_plugin();    // plugins/sys/sys.cpp
std::unique_ptr<Plugin> make_linalg_plugin(); // plugins/linalg/linalg.cpp
std::unique_ptr<Plugin> make_pde_plugin();    // plugins/pde/pde.cpp
std::unique_ptr<Plugin> make_ie_plugin();     // plugins/ie/ie.cpp
std::unique_ptr<Plugin> make_hyb_plugin();    // plugins/hyb/hyb.cpp

void register_builtin_plugins() {
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    register_plugin(make_dsp_plugin());
    register_plugin(make_sys_plugin());
    register_plugin(make_linalg_plugin());
    register_plugin(make_pde_plugin());
    register_plugin(make_ie_plugin());
    register_plugin(make_hyb_plugin());
}

} // namespace mathsolver::plugins
