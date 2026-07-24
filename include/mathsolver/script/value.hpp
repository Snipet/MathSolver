#pragma once

// The script value model (docs/proposals/console-language.md §3).
//
// A script value is what one console statement produces and what one session
// binding stores. Phase 1 carries exactly the two things the REPL already
// treated as first-class — an expression and an equation — so this type is a
// rename of the `std::variant<Expr, Equation>` the CLI session environment
// already used. The alternatives later phases add (lists, booleans, strings,
// closures) are named in the proposal and deliberately absent here: Phase 1
// changes no user-visible behaviour, and an alternative nothing constructs is
// dead weight in a variant every caller has to switch over.
//
// The invariant that keeps this cheap: a bare Expr behaves exactly as it does
// today. Every existing verb consumes and produces expressions, so the default
// alternative is the whole of the current language.

#include <set>
#include <string>
#include <variant>

#include "mathsolver/expr.hpp"

namespace mathsolver::script {

/// Every symbol mentioned on either side of an equation.
std::set<std::string> equation_symbols(const Equation& eq);

/// One script value.
class Value {
public:
    Value(Expr e) : v_(std::move(e)) {}                    // NOLINT(*-explicit-*)
    Value(Equation eq) : v_(std::move(eq)) {}              // NOLINT(*-explicit-*)
    explicit Value(std::variant<Expr, Equation> v) : v_(std::move(v)) {}

    bool is_expr() const noexcept { return std::holds_alternative<Expr>(v_); }
    bool is_equation() const noexcept {
        return std::holds_alternative<Equation>(v_);
    }

    /// Throws std::bad_variant_access on the wrong alternative; callers that
    /// can face either kind test with if_expr()/if_equation() first.
    const Expr& expr() const { return std::get<Expr>(v_); }
    const Equation& equation() const { return std::get<Equation>(v_); }

    const Expr* if_expr() const noexcept { return std::get_if<Expr>(&v_); }
    const Equation* if_equation() const noexcept {
        return std::get_if<Equation>(&v_);
    }

    /// Every symbol the value mentions, on either side of an equation.
    std::set<std::string> free_symbols() const;

    /// Canonical plain-style rendering; round-trips through the parser.
    std::string to_plain() const;

private:
    std::variant<Expr, Equation> v_;
};

} // namespace mathsolver::script
