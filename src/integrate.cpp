#include "mathsolver/integrate.hpp"

// Implemented in the v0.3 integration stage - see DESIGN.md §8b.

namespace mathsolver {

IntegrateResult integrate(const Expr&, std::string_view) {
    IntegrateResult res;
    res.warnings.emplace_back("no applicable integration rule");
    return res;
}

DefiniteIntegralResult integrate_definite(const Expr&, std::string_view, const Expr&,
                                          const Expr&) {
    DefiniteIntegralResult res;
    res.warnings.emplace_back("no applicable integration rule");
    return res;
}

} // namespace mathsolver
