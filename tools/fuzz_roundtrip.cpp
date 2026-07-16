// fuzz_roundtrip.cpp — seeded round-trip fuzz harness for MathSolver.
//
// Generates random canonical expressions through the expr.hpp factories and
// checks, for each expression e:
//   (a) parse_expression(to_string(e, Plain))  is structurally_equal to e
//   (b) parse_expression(to_string(e, LaTeX))  is structurally_equal to e
//   (c) differential evaluation: evaluate(e, b) agrees with
//       evaluate(reparsed, b) at two fixed binding sets (EvalError on both
//       sides counts as agreement; non-finite values are skipped)
//
// Usage: fuzz_roundtrip [--gen-only] [seed] [count]     (defaults 42, 5000)
//
// --gen-only exercises only the generator and the parser (printer/evaluator
// may still be under construction): it verifies that generation never throws
// and that a fixed list of grammar-stress inputs parses.
//
// On any failure the harness prints the index within the seed run, the
// debug_string of the expression, both renderings, and the mismatch, then
// continues; the exit code is nonzero iff at least one failure occurred.
//
// Dependency-free (no test framework); links only mathsolver sources.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/expr.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/printer.hpp"

namespace {

using mathsolver::ConstantId;
using mathsolver::Expr;
using mathsolver::FunctionId;
using mathsolver::Rational;

int g_failures = 0;

// ---------------------------------------------------------------------------
// Random AST generator (deterministic for a given seed).
// ---------------------------------------------------------------------------

class AstGenerator {
public:
    explicit AstGenerator(std::uint64_t seed) : rng_(seed) {}

    /// Top-level generation: random nesting depth up to ~6, size decays with
    /// depth. Factory throws (OverflowError / DivisionByZeroError from exact
    /// folds) are caught and the node is regenerated.
    Expr generate() { return gen(static_cast<int>(int_in(0, 6))); }

private:
    std::mt19937_64 rng_;

    long long int_in(long long lo, long long hi) {
        return std::uniform_int_distribution<long long>(lo, hi)(rng_);
    }

    Expr gen(int depth) {
        for (int attempt = 0; attempt < 100; ++attempt) {
            try {
                return gen_once(depth);
            } catch (const mathsolver::OverflowError&) {
                // Exact numeric fold exceeded 64 bits — regenerate this node.
            } catch (const mathsolver::DivisionByZeroError&) {
                // e.g. make_pow(0, negative) — regenerate this node.
            }
        }
        return mathsolver::make_num(1); // pathological seed; keep going
    }

    Expr gen_once(int depth) {
        if (depth <= 0) {
            return gen_leaf();
        }
        long long r = int_in(0, 99);
        if (r < 15) {
            return gen_leaf();
        }
        if (r < 38) { // Add, 2-4 terms
            std::vector<Expr> terms;
            long long n = int_in(2, 4);
            for (long long i = 0; i < n; ++i) {
                terms.push_back(gen(depth - 1));
            }
            return mathsolver::make_add(std::move(terms));
        }
        if (r < 60) { // Mul, 2-4 factors
            std::vector<Expr> factors;
            long long n = int_in(2, 4);
            for (long long i = 0; i < n; ++i) {
                factors.push_back(gen(depth - 1));
            }
            return mathsolver::make_mul(std::move(factors));
        }
        if (r < 74) { // Pow
            return mathsolver::make_pow(gen(depth - 1), gen_exponent(depth - 1));
        }
        if (r < 88) { // Function — every FunctionId reachable
            static constexpr FunctionId kAll[] = {
                FunctionId::Sin,  FunctionId::Cos,  FunctionId::Tan,
                FunctionId::Asin, FunctionId::Acos, FunctionId::Atan,
                FunctionId::Sinh, FunctionId::Cosh, FunctionId::Tanh,
                FunctionId::Ln,   FunctionId::Abs,
            };
            return mathsolver::make_fn(kAll[int_in(0, 10)], gen(depth - 1));
        }
        return gen_tricky(depth);
    }

    Expr gen_leaf() {
        long long r = int_in(0, 99);
        if (r < 45) {
            return mathsolver::make_num(gen_rational());
        }
        if (r < 85) {
            return mathsolver::make_sym(gen_symbol_name());
        }
        return mathsolver::make_const(r < 93 ? ConstantId::Pi : ConstantId::E);
    }

    /// Mix of small/large/negative values, integer and non-integer.
    Rational gen_rational() {
        switch (int_in(0, 9)) {
        case 0:
        case 1:
        case 2:
        case 3: return Rational(int_in(-9, 9));
        case 4: return Rational(int_in(-1000000, 1000000));
        case 5: return Rational(int_in(-1000000000000LL, 1000000000000LL));
        case 6:
        case 7: return Rational(int_in(-99, 99), int_in(2, 12));
        case 8: return Rational(int_in(-999999, 999999), int_in(2, 999999));
        default: return Rational(-(2 * int_in(1, 9) + 1), 2); // negative half-integer
        }
    }

    /// Single letters, greek names, and subscripted names. Subscripts stay in
    /// the shapes whose unbraced Plain form re-lexes to one symbol (§4):
    /// a single letter or a digit run.
    std::string gen_symbol_name() {
        static constexpr const char* kPlain[] = {"x", "y", "z", "a", "b", "t", "u", "v", "w"};
        static constexpr const char* kGreek[] = {"alpha", "beta",   "gamma", "delta", "epsilon",
                                                 "theta", "lambda", "mu",    "phi",   "omega"};
        long long r = int_in(0, 9);
        if (r < 5) {
            return kPlain[int_in(0, 8)];
        }
        if (r < 8) {
            return kGreek[int_in(0, 9)];
        }
        std::string name = kPlain[int_in(0, 8)];
        name += '_';
        if (int_in(0, 1) == 0) {
            static constexpr char kSub[] = "abcijkmn";
            name += kSub[int_in(0, 7)];
        } else {
            name += std::to_string(int_in(0, 999));
        }
        return name;
    }

    /// Exponents skew small so evaluation stays mostly finite.
    Expr gen_exponent(int depth) {
        long long r = int_in(0, 9);
        if (r < 4) {
            return mathsolver::make_num(int_in(-3, 3));
        }
        if (r < 7) {
            static const Rational kChoices[] = {{1, 2}, {-1, 2}, {1, 3},  {-1, 3},
                                                {2, 3}, {3, 2},  {-3, 2}, {5, 2}};
            return mathsolver::make_num(kChoices[int_in(0, 7)]);
        }
        if (r < 9) {
            return mathsolver::make_sym(gen_symbol_name());
        }
        return gen(std::min(depth, 2));
    }

    /// The DESIGN §5 printer stress shapes.
    Expr gen_tricky(int depth) {
        switch (int_in(0, 4)) {
        case 0: { // Pow(u, +-1/2): sqrt(u) / 1/sqrt(u) reconstruction
            Rational half(int_in(0, 1) == 0 ? 1 : -1, 2);
            return mathsolver::make_pow(gen(depth - 1), mathsolver::make_num(half));
        }
        case 1: { // Pow(E, ...): e^x rule must win over sqrt/division rules
            Expr expo;
            switch (int_in(0, 3)) {
            case 0: expo = mathsolver::make_num(Rational(1, 2)); break;
            case 1: expo = mathsolver::make_num(int_in(-3, -1)); break;
            default: expo = gen(depth - 1); break;
            }
            return mathsolver::make_pow(mathsolver::make_const(ConstantId::E), expo);
        }
        case 2: // negative Number exponent: 3/x^2 style denominators
            return mathsolver::make_pow(gen(depth - 1), mathsolver::make_num(-int_in(1, 3)));
        case 3: { // non-integer rational coefficient over a denominator: 3/(2*x)
            long long q = int_in(2, 9);
            long long p = int_in(1, 99);
            if (p % q == 0) {
                ++p; // keep the coefficient non-integer
            }
            if (int_in(0, 1) == 0) {
                p = -p;
            }
            std::vector<Expr> factors;
            factors.push_back(mathsolver::make_num(Rational(p, q)));
            factors.push_back(
                mathsolver::make_pow(gen(depth - 1), mathsolver::make_num(-int_in(1, 2))));
            if (int_in(0, 2) == 0) {
                factors.push_back(mathsolver::make_sym(gen_symbol_name()));
            }
            return mathsolver::make_mul(std::move(factors));
        }
        default: { // Mul(Number, Pow(Number, symbol)): LaTeX digit-boundary \cdot
            Expr coeff = mathsolver::make_num(int_in(2, 99));
            Expr base = mathsolver::make_num(int_in(2, 12));
            Expr power =
                mathsolver::make_pow(std::move(base), mathsolver::make_sym(gen_symbol_name()));
            return mathsolver::make_mul({std::move(coeff), std::move(power)});
        }
        }
    }
};

// ---------------------------------------------------------------------------
// Failure reporting.
// ---------------------------------------------------------------------------

void report_failure(std::size_t index, const Expr& e, const std::string& plain,
                    const std::string& latex, const std::string& message) {
    ++g_failures;
    std::cout << "FAIL at index " << index << "\n"
              << "  debug : " << mathsolver::debug_string(e) << "\n"
              << "  plain : " << plain << "\n"
              << "  latex : " << latex << "\n"
              << "  error : " << message << "\n";
}

// ---------------------------------------------------------------------------
// Differential evaluation at two fixed binding sets.
// ---------------------------------------------------------------------------

mathsolver::Bindings fixed_bindings(const std::set<std::string>& names, int which) {
    static constexpr double kSet0[] = {0.7, 1.3, 2.1, 0.4, 1.9, 0.85, 1.55, 0.35};
    static constexpr double kSet1[] = {-0.6, 3.2, -1.4, 0.9, -2.3, 1.15, -0.45, 2.6};
    const double* values = which == 0 ? kSet0 : kSet1;
    mathsolver::Bindings bindings;
    std::size_t i = 0;
    for (const std::string& name : names) {
        bindings[name] = values[i++ % 8];
    }
    return bindings;
}

std::optional<std::string> differential_eval(const Expr& original, const Expr& reparsed) {
    std::set<std::string> names = mathsolver::free_symbols(original);
    std::set<std::string> more = mathsolver::free_symbols(reparsed);
    names.insert(more.begin(), more.end());
    for (int which = 0; which < 2; ++which) {
        mathsolver::Bindings bindings = fixed_bindings(names, which);
        bool ok1 = false;
        bool ok2 = false;
        double v1 = 0.0;
        double v2 = 0.0;
        try {
            v1 = mathsolver::evaluate(original, bindings);
            ok1 = true;
        } catch (const mathsolver::EvalError&) {
        }
        try {
            v2 = mathsolver::evaluate(reparsed, bindings);
            ok2 = true;
        } catch (const mathsolver::EvalError&) {
        }
        if (ok1 != ok2) {
            return "eval, binding set " + std::to_string(which) + ": " +
                   (ok1 ? "reparsed" : "original") + " threw EvalError but the other did not";
        }
        if (!ok1) {
            continue; // EvalError on both sides counts as agreement
        }
        if (!std::isfinite(v1) || !std::isfinite(v2)) {
            continue; // skip non-finite values
        }
        double scale = std::max({1.0, std::fabs(v1), std::fabs(v2)});
        if (std::fabs(v1 - v2) > 1e-9 * scale) {
            return "eval, binding set " + std::to_string(which) +
                   ": original=" + std::to_string(v1) + " reparsed=" + std::to_string(v2);
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Full round-trip check for one expression.
// ---------------------------------------------------------------------------

void run_full_case(std::size_t index, const Expr& e) {
    std::string plain = "<not rendered>";
    std::string latex = "<not rendered>";
    try {
        plain = mathsolver::to_string(e, mathsolver::PrintStyle::Plain);
        latex = mathsolver::to_string(e, mathsolver::PrintStyle::LaTeX);
    } catch (const std::exception& ex) {
        report_failure(index, e, plain, latex, std::string("to_string threw: ") + ex.what());
        return;
    }

    Expr reparsed_plain;
    try {
        reparsed_plain = mathsolver::parse_expression(plain);
        if (!mathsolver::structurally_equal(e, reparsed_plain)) {
            report_failure(index, e, plain, latex,
                           "Plain round-trip mismatch; reparsed as " +
                               mathsolver::debug_string(reparsed_plain));
        }
    } catch (const std::exception& ex) {
        report_failure(index, e, plain, latex, std::string("Plain reparse threw: ") + ex.what());
    }

    Expr reparsed_latex;
    try {
        reparsed_latex = mathsolver::parse_expression(latex);
        if (!mathsolver::structurally_equal(e, reparsed_latex)) {
            report_failure(index, e, plain, latex,
                           "LaTeX round-trip mismatch; reparsed as " +
                               mathsolver::debug_string(reparsed_latex));
        }
    } catch (const std::exception& ex) {
        report_failure(index, e, plain, latex, std::string("LaTeX reparse threw: ") + ex.what());
    }

    const Expr& reparsed = reparsed_plain ? reparsed_plain : reparsed_latex;
    if (reparsed) {
        if (std::optional<std::string> msg = differential_eval(e, reparsed)) {
            report_failure(index, e, plain, latex, *msg);
        }
    }
}

// ---------------------------------------------------------------------------
// --gen-only mode: generator soak + fixed grammar-stress parses.
// ---------------------------------------------------------------------------

// 30 grammar-stress inputs from DESIGN §4 (implicit multiplication, LaTeX
// commands, subscripts, identifier segmentation, bare function application,
// \sin^{n}, \log bases, spacing commands, mixed grouping).
constexpr const char* kGrammarStress[30] = {
    "2x + 3x",
    "(x+1)(x-2)",
    "2\\pi r",
    "\\frac{1}{2} + x",
    "\\frac{x+1}{x-1}",
    "\\sqrt{x}",
    "\\sqrt[3]{x + 1}",
    "2^3^2",
    "2^-3",
    "-x^2 + +x",
    "1/2x",
    "abs(x - 3.5) + x**2 + y**3",
    "\\sin 2x",
    "\\sin x \\cos y",
    "\\sin x + 1",
    "\\sin^2 x",
    "\\sin^{-1} x",
    "sinx",
    "xy",
    "x_1 + x_{12} + x_a",
    "x_12",
    "x_ab",
    "x_{max}",
    "\\log_{2}{8}",
    "\\log_2 x + log(y)",
    "exp(x) + \\exp{x}",
    "sec(x) + csc(x) + cot(x)",
    "\\left( x + 1 \\right)^{2}",
    "\\alpha + \\beta\\gamma - \\theta",
    "1 \\quad + \\, 2 \\times 3 \\div 4 \\cdot e^{x} + {y+1}[z-2] - 3.14",
};

void run_gen_only(AstGenerator& gen, std::uint64_t count) {
    for (std::uint64_t i = 0; i < count; ++i) {
        try {
            Expr e = gen.generate();
            if (!e) {
                ++g_failures;
                std::cout << "FAIL at index " << i << ": generator returned null\n";
                continue;
            }
            std::string dump = mathsolver::debug_string(e);
            if (dump.empty()) {
                ++g_failures;
                std::cout << "FAIL at index " << i << ": empty debug_string\n";
            }
        } catch (const std::exception& ex) {
            ++g_failures;
            std::cout << "FAIL at index " << i << ": generation threw: " << ex.what() << "\n";
        }
    }

    for (std::size_t i = 0; i < std::size(kGrammarStress); ++i) {
        const char* src = kGrammarStress[i];
        try {
            Expr e = mathsolver::parse_expression(src);
            if (!e) {
                ++g_failures;
                std::cout << "FAIL grammar-stress [" << i << "] \"" << src
                          << "\": parser returned null\n";
            }
        } catch (const std::exception& ex) {
            ++g_failures;
            std::cout << "FAIL grammar-stress [" << i << "] \"" << src << "\": " << ex.what()
                      << "\n";
        }
    }

    // A few exact structural expectations (both sides go through the same
    // factories, so these pin the parser's canonical output).
    struct SpotCheck {
        const char* input;
        Expr expected;
    };
    const Expr x = mathsolver::make_sym("x");
    const Expr y = mathsolver::make_sym("y");
    const SpotCheck spot_checks[] = {
        {"2x", mathsolver::make_mul({mathsolver::make_num(2), x})},
        {"x - y", mathsolver::make_sub(x, y)},
        {"x/y", mathsolver::make_div(x, y)},
        {"sqrt(x)", mathsolver::make_sqrt(x)},
        {"exp(2x)", mathsolver::make_exp(mathsolver::make_mul({mathsolver::make_num(2), x}))},
        {"x_12", mathsolver::make_sym("x_12")},
    };
    for (const SpotCheck& check : spot_checks) {
        try {
            Expr parsed = mathsolver::parse_expression(check.input);
            if (!mathsolver::structurally_equal(parsed, check.expected)) {
                ++g_failures;
                std::cout << "FAIL spot-check \"" << check.input << "\": parsed "
                          << mathsolver::debug_string(parsed) << ", expected "
                          << mathsolver::debug_string(check.expected) << "\n";
            }
        } catch (const std::exception& ex) {
            ++g_failures;
            std::cout << "FAIL spot-check \"" << check.input << "\": " << ex.what() << "\n";
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    std::uint64_t seed = 42;
    std::uint64_t count = 5000;
    bool gen_only = false;
    int positional = 0;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--gen-only") {
            gen_only = true;
            continue;
        }
        char* end = nullptr;
        unsigned long long value = std::strtoull(argv[i], &end, 10);
        if (end == argv[i] || *end != '\0' || positional > 1) {
            std::cerr << "usage: fuzz_roundtrip [--gen-only] [seed] [count]\n";
            return 2;
        }
        (positional == 0 ? seed : count) = value;
        ++positional;
    }

    std::cout << "fuzz_roundtrip: seed=" << seed << " count=" << count
              << (gen_only ? " (gen-only mode)" : "") << "\n";

    AstGenerator gen(seed);
    if (gen_only) {
        run_gen_only(gen, count);
    } else {
        for (std::uint64_t i = 0; i < count; ++i) {
            Expr e;
            try {
                e = gen.generate();
            } catch (const std::exception& ex) {
                ++g_failures;
                std::cout << "FAIL at index " << i << ": generation threw: " << ex.what() << "\n";
                continue;
            }
            run_full_case(static_cast<std::size_t>(i), e);
        }
    }

    if (g_failures > 0) {
        std::cout << "FAILED: " << g_failures << " failure(s) over " << count
                  << " case(s), seed " << seed << "\n";
        return 1;
    }
    std::cout << "OK: 0 failures over " << count << " case(s), seed " << seed << "\n";
    return 0;
}
