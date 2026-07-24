#include "mathsolver/script/value.hpp"

#include "mathsolver/printer.hpp"

namespace mathsolver::script {

std::set<std::string> equation_symbols(const Equation& eq) {
    std::set<std::string> syms = free_symbols(eq.lhs);
    syms.merge(free_symbols(eq.rhs));
    return syms;
}

std::set<std::string> Value::free_symbols() const {
    if (const Expr* e = if_expr()) {
        return mathsolver::free_symbols(*e);
    }
    return equation_symbols(equation());
}

std::string Value::to_plain() const {
    if (const Expr* e = if_expr()) {
        return to_string(*e, PrintStyle::Plain);
    }
    return to_string(equation(), PrintStyle::Plain);
}

} // namespace mathsolver::script
