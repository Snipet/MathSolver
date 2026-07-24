// End-to-end tests for the mathsolver CLI/REPL (DESIGN.md §10).
//
// Each test runs the real binary (path injected as MATHSOLVER_CLI_PATH via
// CMake) through popen(), capturing stdout/stderr and the exit code.

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <string>
#include <vector>

#include <sys/wait.h>

namespace {

struct RunResult {
    std::string output;
    int exit_code = -1;
};

RunResult run_shell(const std::string& command) {
    std::FILE* pipe = ::popen(command.c_str(), "r");
    REQUIRE(pipe != nullptr);
    std::string output;
    char buffer[4096];
    std::size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof buffer, pipe)) > 0) {
        output.append(buffer, n);
    }
    const int status = ::pclose(pipe);
    RunResult result{std::move(output), -1};
    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    }
    return result;
}

std::string shell_quote(const std::string& s) {
    std::string quoted = "'";
    for (const char c : s) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += '\'';
    return quoted;
}

/// Run `mathsolver <args...>` with the given stream redirection
/// ("2>&1" both streams, "2>/dev/null" stdout only, "2>&1 1>/dev/null"
/// stderr only).
RunResult run_cli(std::initializer_list<std::string> args,
                  const std::string& redirect = "2>&1") {
    std::string command = shell_quote(MATHSOLVER_CLI_PATH);
    for (const std::string& a : args) {
        command += ' ';
        command += shell_quote(a);
    }
    command += ' ';
    command += redirect;
    return run_shell(command);
}

/// Pipe `input` (may contain newlines) into the REPL, capturing both streams.
RunResult run_repl(const std::string& input) {
    return run_shell("printf %s " + shell_quote(input) + " | " +
                     shell_quote(MATHSOLVER_CLI_PATH) + " 2>&1");
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

/// Extract the number following `prefix` (e.g. "x ≈ ") in `text`.
double number_after(const std::string& text, const std::string& prefix) {
    const std::size_t pos = text.find(prefix);
    REQUIRE(pos != std::string::npos);
    return std::strtod(text.c_str() + pos + prefix.size(), nullptr);
}

} // namespace

// ---------------------------------------------------------------------------
// One-shot subcommands
// ---------------------------------------------------------------------------

TEST_CASE("cli: simplify") {
    const RunResult r = run_cli({"simplify", "2x + 3x"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(r.output == "5*x\n");
}

TEST_CASE("cli: simplify accepts an equation and simplifies both sides") {
    const RunResult r = run_cli({"simplify", "x + x = 4"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(r.output == "2*x = 4\n");
}

TEST_CASE("cli: expand") {
    const RunResult r = run_cli({"expand", "(x+1)^3"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(r.output == "x^3 + 3*x^2 + 3*x + 1\n");
}

TEST_CASE("cli: factor") {
    const RunResult r = run_cli({"factor", "x^2 - 5x + 6"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "x - 2"));
    CHECK(contains(r.output, "x - 3"));
    CHECK(contains(r.output, ")*("));
}

TEST_CASE("cli: factor of a bare integer gives its prime factorization") {
    const RunResult r = run_cli({"factor", "360"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "2^3 * 3^2 * 5"));

    const RunResult prime = run_cli({"factor", "97"});
    CHECK(prime.exit_code == 0);
    CHECK(contains(prime.output, "97"));

    const RunResult tex = run_cli({"factor", "12", "--latex"});
    CHECK(tex.exit_code == 0);
    CHECK(contains(tex.output, "2^{2} \\cdot 3"));
}

TEST_CASE("cli: number-theory verbs") {
    const RunResult gcd = run_cli({"gcd", "48, 36"});
    INFO(gcd.output);
    CHECK(gcd.exit_code == 0);
    CHECK(contains(gcd.output, "12"));

    const RunResult lcm = run_cli({"lcm", "4, 6, 8"});
    CHECK(lcm.exit_code == 0);
    CHECK(contains(lcm.output, "24"));

    const RunResult prime = run_cli({"isprime", "97"});
    CHECK(prime.exit_code == 0);
    CHECK(contains(prime.output, "prime"));

    const RunResult composite = run_cli({"isprime", "91"});
    CHECK(composite.exit_code == 0);
    CHECK(contains(composite.output, "composite"));

    const RunResult np = run_cli({"nextprime", "100"});
    CHECK(np.exit_code == 0);
    CHECK(contains(np.output, "101"));

    const RunResult div = run_cli({"divisors", "28"});
    CHECK(div.exit_code == 0);
    CHECK(contains(div.output, "1, 2, 4, 7, 14, 28"));

    const RunResult phi = run_cli({"totient", "36"});
    CHECK(phi.exit_code == 0);
    CHECK(contains(phi.output, "12"));

    // A non-integer argument is a usage error (exit 2).
    const RunResult bad = run_cli({"isprime", "x"});
    CHECK(bad.exit_code == 2);
}

TEST_CASE("cli: trigexpand of sums and multiples") {
    const RunResult sum = run_cli({"trigexpand", "sin(a + b)"});
    INFO(sum.output);
    CHECK(sum.exit_code == 0);
    CHECK(contains(sum.output, "sin(a)*cos(b)"));
    CHECK(contains(sum.output, "sin(b)*cos(a)"));

    const RunResult dbl = run_cli({"trigexpand", "cos(2*x)"});
    CHECK(dbl.exit_code == 0);
    CHECK(contains(dbl.output, "cos(x)^2"));
    CHECK(contains(dbl.output, "sin(x)^2"));
}

TEST_CASE("cli: resultant of two polynomials") {
    const RunResult nonzero = run_cli({"resultant", "x^2 - 1", "x - 2"});
    INFO(nonzero.output);
    CHECK(nonzero.exit_code == 0);
    CHECK(contains(nonzero.output, "3"));

    // Shared root → resultant 0.
    const RunResult zero = run_cli({"resultant", "x^2 - 1", "x - 1"});
    CHECK(zero.exit_code == 0);
    CHECK(contains(zero.output, "0"));
}

TEST_CASE("cli: polygcd and polylcm") {
    const RunResult g = run_cli({"polygcd", "x^2 - 1", "x^3 - 1"});
    INFO(g.output);
    CHECK(g.exit_code == 0);
    CHECK(contains(g.output, "x - 1"));

    const RunResult l = run_cli({"polylcm", "x - 1", "x + 1"});
    CHECK(l.exit_code == 0);
    CHECK(contains(l.output, "x^2 - 1"));
}

TEST_CASE("cli: polydiv quotient and remainder") {
    const RunResult exact = run_cli({"polydiv", "x^3 - 1", "x - 1"});
    INFO(exact.output);
    CHECK(exact.exit_code == 0);
    CHECK(contains(exact.output, "quotient: x^2 + x + 1"));
    CHECK(contains(exact.output, "remainder: 0"));

    const RunResult rem = run_cli({"polydiv", "x^3 + 2x + 1", "x^2 + 1"});
    CHECK(rem.exit_code == 0);
    CHECK(contains(rem.output, "quotient: x"));
    CHECK(contains(rem.output, "remainder: x + 1"));
}

TEST_CASE("cli: logexpand and logcombine") {
    const RunResult e = run_cli({"logexpand", "ln(x*y)"});
    INFO(e.output);
    CHECK(e.exit_code == 0);
    CHECK(contains(e.output, "ln(x)"));
    CHECK(contains(e.output, "ln(y)"));

    const RunResult c = run_cli({"logcombine", "ln(x) + ln(y)"});
    CHECK(c.exit_code == 0);
    CHECK(contains(c.output, "ln(x*y)"));
}

TEST_CASE("cli: trigreduce of products and powers") {
    const RunResult sq = run_cli({"trigreduce", "sin(x)^2"});
    INFO(sq.output);
    CHECK(sq.exit_code == 0);
    CHECK(contains(sq.output, "cos(2*x)"));

    const RunResult prod = run_cli({"trigreduce", "2*sin(x)*cos(x)"});
    CHECK(prod.exit_code == 0);
    CHECK(contains(prod.output, "sin(2*x)"));
}

TEST_CASE("cli: discriminant of a polynomial") {
    const RunResult sym = run_cli({"discriminant", "a*x^2 + b*x + c", "x"});
    INFO(sym.output);
    CHECK(sym.exit_code == 0);
    CHECK(contains(sym.output, "b^2 - 4*a*c"));

    const RunResult num = run_cli({"discriminant", "x^2 - 5x + 6"});
    CHECK(num.exit_code == 0);
    CHECK(contains(num.output, "1"));
    CHECK(contains(num.output, "two distinct real"));

    // Degree 5 is unsupported (usage error, exit 2).
    const RunResult bad = run_cli({"discriminant", "x^5 + 1", "x"});
    CHECK(bad.exit_code == 2);
}

TEST_CASE("cli: companion matrix of a polynomial") {
    // x^2 - 3x + 2 → [3, -2; 1, 0] (MATLAB compan orientation).
    const RunResult quad = run_cli({"companion", "x^2 - 3x + 2"});
    INFO(quad.output);
    CHECK(quad.exit_code == 0);
    CHECK(contains(quad.output, "[3, -2; 1, 0]"));

    // Non-monic leading coefficient is normalized: 2x^2+4x-6 → x^2+2x-3.
    const RunResult scaled = run_cli({"companion", "2x^2 + 4x - 6"});
    CHECK(scaled.exit_code == 0);
    CHECK(contains(scaled.output, "[-2, 3; 1, 0]"));

    // LaTeX renders a pmatrix.
    const RunResult tex = run_cli({"companion", "x^2 - 3x + 2", "--latex"});
    CHECK(tex.exit_code == 0);
    CHECK(contains(tex.output, "pmatrix"));

    // A bare constant has degree 0 → usage error, exit 2.
    const RunResult bad = run_cli({"companion", "7"});
    CHECK(bad.exit_code == 2);
}

TEST_CASE("cli: vandermonde matrix of a node list") {
    // Nodes 1, 2, 3 → rows (1, x, x^2).
    const RunResult v = run_cli({"vandermonde", "1, 2, 3"});
    INFO(v.output);
    CHECK(v.exit_code == 0);
    CHECK(contains(v.output, "[1, 1, 1; 1, 2, 4; 1, 3, 9]"));

    // Symbolic nodes stay symbolic.
    const RunResult sym = run_cli({"vandermonde", "a, b"});
    CHECK(sym.exit_code == 0);
    CHECK(contains(sym.output, "[1, a; 1, b]"));

    // LaTeX renders a pmatrix.
    const RunResult tex = run_cli({"vandermonde", "1, 2, 3", "--latex"});
    CHECK(tex.exit_code == 0);
    CHECK(contains(tex.output, "pmatrix"));
}

TEST_CASE("cli: solve inequalities into interval solution sets") {
    const RunResult quad = run_cli({"solve", "x^2 < 4"});
    INFO(quad.output);
    CHECK(quad.exit_code == 0);
    CHECK(contains(quad.output, "(-2, 2)"));

    const RunResult ge = run_cli({"solve", "x^2 >= 4"});
    CHECK(ge.exit_code == 0);
    CHECK(contains(ge.output, "-2]"));
    CHECK(contains(ge.output, "[2"));

    const RunResult rat = run_cli({"solve", "1/x > 0"});
    CHECK(rat.exit_code == 0);
    CHECK(contains(rat.output, "(0"));

    // Equations still solve to roots (no regression).
    const RunResult eq = run_cli({"solve", "x^2 = 4", "x"});
    CHECK(eq.exit_code == 0);
    CHECK(contains(eq.output, "2"));
}

TEST_CASE("cli: modular arithmetic verbs") {
    const RunResult pm = run_cli({"powmod", "7, 100, 13"});
    INFO(pm.output);
    CHECK(pm.exit_code == 0);
    CHECK(contains(pm.output, "9"));

    // A huge exponent that would overflow ordinary evaluation still works.
    const RunResult big = run_cli({"powmod", "7, 1000000, 13"});
    CHECK(big.exit_code == 0);
    CHECK(contains(big.output, "9"));

    const RunResult inv = run_cli({"modinv", "3, 11"});
    CHECK(inv.exit_code == 0);
    CHECK(contains(inv.output, "4"));

    const RunResult crt = run_cli({"crt", "2, 3, 2; 3, 5, 7"});
    CHECK(crt.exit_code == 0);
    CHECK(contains(crt.output, "23 (mod 105)"));

    // Non-invertible input is an error (exit 3, an engine Error).
    const RunResult bad = run_cli({"modinv", "6, 9"});
    CHECK(bad.exit_code != 0);
}

TEST_CASE("cli: cfrac — rational, surd, and convergents") {
    const RunResult rat = run_cli({"cfrac", "355/113"});
    INFO(rat.output);
    CHECK(rat.exit_code == 0);
    CHECK(contains(rat.output, "[3; 7, 16]"));
    CHECK(contains(rat.output, "22/7"));

    const RunResult surd = run_cli({"cfrac", "sqrt(2)"});
    CHECK(surd.exit_code == 0);
    CHECK(contains(surd.output, "[1; (2)]"));
    CHECK(contains(surd.output, "17/12"));
}

TEST_CASE("cli: cancel — success, no-op, and usage error") {
    const RunResult ok = run_cli({"cancel", "(x^2 - 1)/(x - 1)"});
    INFO(ok.output);
    CHECK(ok.exit_code == 0);
    CHECK(contains(ok.output, "x + 1"));

    // A non-cancelling input prints the simplified input back, exit 0.
    const RunResult noop = run_cli({"cancel", "(x^2 + 1)/(x + 1)"});
    INFO(noop.output);
    CHECK(noop.exit_code == 0);
    CHECK(contains(noop.output, "(x^2 + 1)/(x + 1)"));

    // Naming a variable not free in the input is a usage error (exit 2).
    const RunResult bad = run_cli({"cancel", "(x^2 - 1)/(x - 1)", "z"});
    CHECK(bad.exit_code == 2);
    CHECK(contains(bad.output, "is not a free variable"));
}

TEST_CASE("cli: together — combine and no-op") {
    const RunResult ok = run_cli({"together", "1/x + 1/y"});
    INFO(ok.output);
    CHECK(ok.exit_code == 0);
    CHECK(contains(ok.output, "(x + y)/(x*y)"));

    // Nothing to combine (no symbolic denominator): input back, exit 0.
    const RunResult noop = run_cli({"together", "x + 1"});
    INFO(noop.output);
    CHECK(noop.exit_code == 0);
    CHECK(contains(noop.output, "x + 1"));
}

TEST_CASE("cli: solve exact quadratic") {
    const RunResult r = run_cli({"solve", "x^2 = 4", "x"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "x = -2\n"));
    CHECK(contains(r.output, "x = 2\n"));
    CHECK(contains(r.output, "method: quadratic formula"));
}

TEST_CASE("cli: solve infers a unique variable") {
    const RunResult r = run_cli({"solve", "2y + 3 = 7"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "y = 2\n"));
    CHECK(contains(r.output, "method: linear"));
}

TEST_CASE("cli: solve numeric fallback with --range") {
    const RunResult r = run_cli({"solve", "cos(x) = x", "x", "--range", "0", "2"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    REQUIRE(contains(r.output, "x ≈ "));
    const double root = number_after(r.output, "x ≈ ");
    CHECK(std::abs(root - 0.7390851332151607) < 1e-9);
    CHECK(contains(r.output, "method: numeric (Newton/bisection)"));
    CHECK(contains(r.output, "[0, 2]")); // interval warning reflects --range
}

TEST_CASE("cli: solve reports identities and contradictions") {
    const RunResult identity = run_cli({"solve", "x + 1 = x + 1", "x"});
    INFO(identity.output);
    CHECK(identity.exit_code == 0);
    CHECK(contains(identity.output, "true for all x"));

    const RunResult contradiction = run_cli({"solve", "x + 1 = x", "x"});
    INFO(contradiction.output);
    CHECK(contradiction.exit_code == 0);
    CHECK(contains(contradiction.output, "no real solutions"));
}

TEST_CASE("cli: solve trig equation prints the periodicity note") {
    const RunResult r = run_cli({"solve", "sin(x) = 1/2", "x"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "x = pi/6"));
    CHECK(contains(r.output, "principal solution"));
    CHECK(contains(r.output, "2*pi*n"));
}

TEST_CASE("cli: diff with explicit and inferred variable") {
    const RunResult explicit_var = run_cli({"diff", "sin(x^2)", "x"});
    INFO(explicit_var.output);
    CHECK(explicit_var.exit_code == 0);
    CHECK(explicit_var.output == "2*x*cos(x^2)\n");

    const RunResult inferred = run_cli({"diff", "sin(x^2)"});
    CHECK(inferred.exit_code == 0);
    CHECK(inferred.output == "2*x*cos(x^2)\n");
}

TEST_CASE("cli: integrate one-shot prints F(x) + C and the method") {
    const RunResult r = run_cli({"integrate", "x*sin(x)"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "-x*cos(x) + sin(x) + C"));
    CHECK(contains(r.output, "method: integration by parts"));

    const RunResult pf = run_cli({"integrate", "1/(x^2-1)", "x"});
    INFO(pf.output);
    CHECK(pf.exit_code == 0);
    CHECK(contains(pf.output, "+ C"));
    CHECK(contains(pf.output, "method: partial fractions"));
}

TEST_CASE("cli: integrate definite prints value = / value ≈") {
    const RunResult exact = run_cli({"integrate", "sin(x)", "--from", "0", "--to", "pi"});
    INFO(exact.output);
    CHECK(exact.exit_code == 0);
    CHECK(contains(exact.output, "value = 2\n"));
    CHECK(contains(exact.output, "method: FTC"));

    // e^(-x^2) is exact now (erf); sin(x)/x still needs the numeric path.
    const RunResult gauss =
        run_cli({"integrate", "e^(-x^2)", "x", "--from", "0", "--to", "1"});
    INFO(gauss.output);
    CHECK(gauss.exit_code == 0);
    CHECK(contains(gauss.output, "value = sqrt(pi)*erf(1)/2"));

    const RunResult numeric =
        run_cli({"integrate", "sin(x)/x", "x", "--from", "1", "--to", "2"});
    INFO(numeric.output);
    CHECK(numeric.exit_code == 0);
    CHECK(contains(numeric.output, "value ≈ 0.65932"));
    CHECK(contains(numeric.output, "method: numeric (adaptive Simpson)"));
}

TEST_CASE("cli: integrate Unsolved is an answer, exit 0") {
    const RunResult r = run_cli({"integrate", "e^(x^2)"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "unable to integrate"));
    CHECK(contains(r.output, "no applicable integration rule"));

    const RunResult gap = run_cli({"integrate", "1/x", "--from", "-1", "--to", "1"});
    INFO(gap.output);
    CHECK(gap.exit_code == 0);
    CHECK(contains(gap.output, "unable to integrate"));
    CHECK(contains(gap.output, "not evaluable"));
}

TEST_CASE("cli: integrate bounds that fold to non-finite values are Unsolved") {
    // "1/0" parses but blows up during constant folding; the §8b bounds
    // contract answers Unsolved + warning (exit 0), not a hard error.
    const RunResult r =
        run_cli({"integrate", "x", "x", "--from", "0", "--to", "1/0"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "unable to integrate"));
    CHECK(contains(r.output, "integration bounds must evaluate to finite numbers"));
}

TEST_CASE("cli: divergent definite integral is refused with a divergence warning") {
    const RunResult r =
        run_cli({"integrate", "1/(x-1)^2", "x", "--from", "0", "--to", "2.5"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "unable to integrate"));
    CHECK(contains(r.output, "may be divergent"));
    CHECK(!contains(r.output, "value"));
}

TEST_CASE("cli: integrate usage errors exit 2") {
    // --from without --to (and vice versa) is a usage error.
    const RunResult lone_from =
        run_cli({"integrate", "sin(x)", "--from", "0"}, "2>&1 1>/dev/null");
    INFO(lone_from.output);
    CHECK(lone_from.exit_code == 2);
    CHECK(contains(lone_from.output, "--from and --to must be given together"));

    const RunResult lone_to =
        run_cli({"integrate", "sin(x)", "--to", "1"}, "2>&1 1>/dev/null");
    CHECK(lone_to.exit_code == 2);

    // --from is integrate-only.
    const RunResult wrong_sub =
        run_cli({"diff", "sin(x)", "--from", "0", "--to", "1"}, "2>&1 1>/dev/null");
    CHECK(wrong_sub.exit_code == 2);

    // Ambiguous variable.
    const RunResult ambiguous =
        run_cli({"integrate", "x*y"}, "2>&1 1>/dev/null");
    INFO(ambiguous.output);
    CHECK(ambiguous.exit_code == 2);
    CHECK(contains(ambiguous.output, "cannot infer the variable for integrate"));
}

TEST_CASE("cli: REPL integrate command, indefinite and definite") {
    const RunResult r = run_repl(
        "integrate x*exp(x^2)\nintegrate sin(x), x, 0, pi\nintegrate x^2, 0, 1\nquit\n");
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "e^(x^2)/2 + C"));
    CHECK(contains(r.output, "value = 2"));
    // Three comma segments (bounds without a variable) is a usage error, and
    // the session stays alive.
    CHECK(contains(r.output,
                   "usage: integrate <expression>[, <variable>[, <lo>, <hi>]]"));
}

TEST_CASE("cli: eval with bindings") {
    const RunResult r = run_cli({"eval", "x^2 + y", "x=3", "y=0.5"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(r.output == "9.5\n");
}

TEST_CASE("cli: eval over the complex domain") {
    // A complex expression evaluates over C and prints a + b*i.
    CHECK(run_cli({"eval", "(2+3i)*(1-i)"}).output == "5 + i\n");
    CHECK(run_cli({"eval", "1/(1+i)"}).output == "0.5 - 0.5*i\n");
    CHECK(run_cli({"eval", "2*i"}).output == "2*i\n");
    CHECK(run_cli({"eval", "abs(3+4i)"}).output == "5\n"); // modulus
    // Euler's formula, chopped clean.
    CHECK(run_cli({"eval", "e^(i*pi)"}).output == "-1\n");
    CHECK(run_cli({"eval", "e^(i*pi/2)"}).output == "i\n");
}

TEST_CASE("cli: eval without bindings evaluates constants") {
    const RunResult r = run_cli({"eval", "e^2"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(std::abs(std::strtod(r.output.c_str(), nullptr) - 7.38905609893065) < 1e-9);
}

TEST_CASE("cli: subs substitutes an expression and simplifies") {
    const RunResult r = run_cli({"subs", "x^2 + y", "x=y+1"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(r.output == "(y + 1)^2 + y\n");
}

TEST_CASE("cli: subs applies multiple assignments left to right") {
    // x -> y first turns x + y into 2y; y -> 2 then gives 4.
    const RunResult r = run_cli({"subs", "x + y", "x=y", "y=2"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(r.output == "4\n");
}

TEST_CASE("cli: subs substitutes into both sides of an equation") {
    const RunResult r = run_cli({"subs", "x^2 = y", "x=z+1"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(r.output == "(z + 1)^2 = y\n");
}

TEST_CASE("cli: subs usage errors exit 2") {
    // Missing '=' in the assignment.
    const RunResult malformed = run_cli({"subs", "x^2", "y+1"}, "2>&1 1>/dev/null");
    INFO(malformed.output);
    CHECK(malformed.exit_code == 2);
    CHECK(contains(malformed.output, "malformed substitution 'y+1'"));

    // No assignment at all.
    const RunResult none = run_cli({"subs", "x^2"}, "2>&1 1>/dev/null");
    INFO(none.output);
    CHECK(none.exit_code == 2);
    CHECK(contains(none.output, "needs at least one name=expression"));

    // Constants cannot be substituted for (same doctrine as eval bindings).
    const RunResult constant = run_cli({"subs", "pi*x", "pi=3"}, "2>&1 1>/dev/null");
    INFO(constant.output);
    CHECK(constant.exit_code == 2);
    CHECK(contains(constant.output, "'pi' is a constant"));
}

TEST_CASE("cli: subs replacement parse error exits 1 with a caret") {
    const RunResult r = run_cli({"subs", "x^2", "x=(y+"}, "2>&1 1>/dev/null");
    INFO(r.output);
    CHECK(r.exit_code == 1);
    CHECK(contains(r.output, "error:"));
    CHECK(contains(r.output, "(y+")); // diagnostic echoes the replacement text
}

TEST_CASE("cli: collect with explicit and inferred variable") {
    const RunResult explicit_var = run_cli({"collect", "x*y + x*z + 1", "x"});
    INFO(explicit_var.output);
    CHECK(explicit_var.exit_code == 0);
    CHECK(explicit_var.output == "x*(y + z) + 1\n");

    const RunResult inferred = run_cli({"collect", "2*x + 3*x^2 + x + x^2"});
    INFO(inferred.output);
    CHECK(inferred.exit_code == 0);
    CHECK(inferred.output == "4*x^2 + 3*x\n");
}

TEST_CASE("cli: collect ambiguous variable exits 2 and lists the symbols") {
    const RunResult r = run_cli({"collect", "x*y + 1"}, "2>&1 1>/dev/null");
    INFO(r.output);
    CHECK(r.exit_code == 2);
    CHECK(contains(r.output, "cannot infer the variable for collect"));
    CHECK(contains(r.output, "x"));
    CHECK(contains(r.output, "y"));
}

TEST_CASE("cli: REPL subs and collect commands") {
    const RunResult r = run_repl("subs x^2 + y, x=y+1\n"
                                 "collect x*y + x*z + 1, x\n"
                                 "collect x*y + 1\n"
                                 "quit\n");
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "(y + 1)^2 + y"));
    CHECK(contains(r.output, "x*(y + z) + 1"));
    // The ambiguity diagnostic keeps the session alive.
    CHECK(contains(r.output, "cannot infer the variable for collect"));
}

TEST_CASE("cli: latex subcommand") {
    const RunResult r = run_cli({"latex", "sqrt(x)/2"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(r.output == "\\frac{\\sqrt{x}}{2}\n");
}

TEST_CASE("cli: --latex flag switches output style") {
    const RunResult r = run_cli({"diff", "sin(x)", "x", "--latex"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "\\cos\\left(x\\right)"));
}

TEST_CASE("cli: --version and --help") {
    const RunResult version = run_cli({"--version"});
    INFO(version.output);
    CHECK(version.exit_code == 0);
    CHECK(contains(version.output, "0.6.0"));

    const RunResult help = run_cli({"--help"});
    CHECK(help.exit_code == 0);
    CHECK(contains(help.output, "usage"));
    CHECK(contains(help.output, "--range"));
}

// ---------------------------------------------------------------------------
// Error handling and exit codes
// ---------------------------------------------------------------------------

TEST_CASE("cli: parse error exits 1 with a caret diagnostic on stderr") {
    const RunResult stderr_only = run_cli({"simplify", "(x + 1"}, "2>&1 1>/dev/null");
    INFO(stderr_only.output);
    CHECK(stderr_only.exit_code == 1);
    CHECK(contains(stderr_only.output, "error:"));
    CHECK(contains(stderr_only.output, "    (x + 1\n"));
    CHECK(contains(stderr_only.output, "\n    ^")); // caret under the '('

    const RunResult stdout_only = run_cli({"simplify", "(x + 1"}, "2>/dev/null");
    CHECK(stdout_only.exit_code == 1);
    CHECK(stdout_only.output.empty()); // nothing on stdout
}

TEST_CASE("cli: caret span underlines the offending token") {
    const RunResult r = run_cli({"simplify", "\\fraq{1}{2} + x"}, "2>&1 1>/dev/null");
    INFO(r.output);
    CHECK(r.exit_code == 1);
    CHECK(contains(r.output, "\\fraq"));
    CHECK(contains(r.output, "\n    ^~~~~")); // 5-byte span of '\fraq'
}

TEST_CASE("cli: math errors exit 1") {
    const RunResult overflow = run_cli({"simplify", "2^200"});
    INFO(overflow.output);
    CHECK(overflow.exit_code == 1);
    CHECK(contains(overflow.output, "error:"));

    const RunResult unbound = run_cli({"eval", "x^2"});
    INFO(unbound.output);
    CHECK(unbound.exit_code == 1);
    CHECK(contains(unbound.output, "x")); // EvalError names the symbol

    const RunResult div_zero = run_cli({"eval", "1/x", "x=0"});
    CHECK(div_zero.exit_code == 1);
}

TEST_CASE("cli: usage errors exit 2") {
    const RunResult unknown = run_cli({"frobnicate", "x"});
    INFO(unknown.output);
    CHECK(unknown.exit_code == 2);
    CHECK(contains(unknown.output, "frobnicate"));

    const RunResult missing_range = run_cli({"solve", "x = 1", "x", "--range", "5"});
    INFO(missing_range.output);
    CHECK(missing_range.exit_code == 2);

    const RunResult bad_binding = run_cli({"eval", "x", "x=abc"});
    INFO(bad_binding.output);
    CHECK(bad_binding.exit_code == 2);

    const RunResult missing_expr = run_cli({"simplify"});
    CHECK(missing_expr.exit_code == 2);
}

TEST_CASE("cli: ambiguous variable exits 2 and lists the symbols") {
    const RunResult r = run_cli({"solve", "x + y = 1"}, "2>&1 1>/dev/null");
    INFO(r.output);
    CHECK(r.exit_code == 2);
    CHECK(contains(r.output, "x"));
    CHECK(contains(r.output, "y"));

    const RunResult diff_r = run_cli({"diff", "x*y + z"}, "2>&1 1>/dev/null");
    INFO(diff_r.output);
    CHECK(diff_r.exit_code == 2);
    CHECK(contains(diff_r.output, "x"));
    CHECK(contains(diff_r.output, "y"));
    CHECK(contains(diff_r.output, "z"));
}

// ---------------------------------------------------------------------------
// REPL (piped stdin behaves exactly like a terminal session)
// ---------------------------------------------------------------------------

TEST_CASE("cli: piped REPL session with recovery from an error") {
    const RunResult r = run_repl("2x + 3x\n"
                                 "solve x^2 = 4, x\n"
                                 "1 +\n"
                                 "diff sin(x), x\n"
                                 "x^2 = 9\n"
                                 "eval x^2 + 1, x=3\n"
                                 "latex sqrt(x)\n"
                                 "debug 2 + 3x\n"
                                 "help\n"
                                 "quit\n");
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "MathSolver"));
    CHECK(contains(r.output, ">>> "));
    CHECK(contains(r.output, "5*x"));                  // bare expression -> simplify
    CHECK(contains(r.output, "x = -2"));               // solve command
    CHECK(contains(r.output, "x = 2"));
    CHECK(contains(r.output, "error:"));               // '1 +' diagnostic ...
    CHECK(contains(r.output, "cos(x)"));               // ... and the session kept going
    CHECK(contains(r.output, "x = 3"));                // bare equation -> auto-solve
    CHECK(contains(r.output, "10"));                   // eval command
    CHECK(contains(r.output, "\\sqrt{x}"));            // latex command
    CHECK(contains(r.output, "(add 2 (mul 3 x))"));    // debug command
    CHECK(contains(r.output, "solve <equation>"));     // help text
}

TEST_CASE("cli: REPL exits cleanly on end of input") {
    const RunResult r = run_repl("1 + 1\n");
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "2"));
}

TEST_CASE("cli: REPL asks for a variable when the equation has several") {
    const RunResult r = run_repl("x + y = 1\nx + 4 = 6\nexit\n");
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "solve <equation>, <variable>"));
    CHECK(contains(r.output, "x = 2")); // session stayed alive and solved
}

// ---------------------------------------------------------------------------
// Regression tests for the reviewed CLI/parser defects (findings 5-10)
// ---------------------------------------------------------------------------

TEST_CASE("cli: deeply nested input fails cleanly and keeps the REPL alive") {
    // Field reproducer: thousands of nested parens used to SIGSEGV (exit 139)
    // and take the whole session down. Now: a clean exit-1 diagnostic one-shot,
    // and a REPL that survives and recovers on the next line.
    const std::string deep = std::string(7000, '(') + "x" + std::string(7000, ')');

    const RunResult one_shot = run_cli({"simplify", deep}, "2>&1 1>/dev/null");
    CHECK(one_shot.exit_code == 1); // clean error, not 139 (SIGSEGV)
    CHECK(contains(one_shot.output, "too deeply nested"));

    const RunResult session = run_repl(deep + "\n6 * 7\nquit\n");
    INFO(session.output);
    CHECK(session.exit_code == 0);
    CHECK(contains(session.output, "too deeply nested"));
    CHECK(contains(session.output, "42")); // recovered and evaluated 6*7
}

TEST_CASE("cli: --range rejects non-finite and overflowing bounds") {
    const RunResult infinite =
        run_cli({"solve", "cos(x) = x", "--range", "-inf", "inf"}, "2>&1 1>/dev/null");
    INFO(infinite.output);
    CHECK(infinite.exit_code == 2);
    CHECK(contains(infinite.output, "finite"));

    // -1e308 .. 1e308: both bounds are finite, but their difference is inf.
    const RunResult wide =
        run_cli({"solve", "cos(x) = x", "--range", "-1e308", "1e308"}, "2>&1 1>/dev/null");
    INFO(wide.output);
    CHECK(wide.exit_code == 2);
    CHECK(contains(wide.output, "finite"));

    // NaN gets its own "must be numbers" message, not "LO must be less than HI".
    const RunResult nan =
        run_cli({"solve", "cos(x) = x", "--range", "nan", "5"}, "2>&1 1>/dev/null");
    INFO(nan.output);
    CHECK(nan.exit_code == 2);
    CHECK(contains(nan.output, "must be numbers"));
    CHECK(!contains(nan.output, "less than"));

    // A valid finite window still solves the same equation.
    const RunResult ok = run_cli({"solve", "cos(x) = x", "--range", "-10", "10"});
    INFO(ok.output);
    CHECK(ok.exit_code == 0);
    CHECK(contains(ok.output, "x ≈ "));
}

TEST_CASE("cli: eval rejects names that can never lex as a bound variable") {
    const RunResult constant = run_cli({"eval", "pi", "pi=3"}, "2>&1 1>/dev/null");
    INFO(constant.output);
    CHECK(constant.exit_code == 2);
    CHECK(contains(constant.output, "constant"));

    const RunResult euler = run_cli({"eval", "e", "e=3"}, "2>&1 1>/dev/null");
    CHECK(euler.exit_code == 2);
    CHECK(contains(euler.output, "constant"));

    const RunResult multi = run_cli({"eval", "foo", "foo=3"}, "2>&1 1>/dev/null");
    INFO(multi.output);
    CHECK(multi.exit_code == 2);
    CHECK(contains(multi.output, "not a bindable variable"));

    // Valid bindings still work: single letters, subscripts, greek names.
    CHECK(run_cli({"eval", "x", "x=3"}).output == "3\n");
    CHECK(run_cli({"eval", "x_1", "x_1=2"}).output == "2\n");
    CHECK(run_cli({"eval", "alpha", "alpha=1"}).output == "1\n");
}

TEST_CASE("cli: unexpected non-ASCII byte reports a valid, whole-character error") {
    // '¤' (U+00A4 = 0xC2 0xA4) must be reported as the full two-byte sequence
    // (as a hex escape, so stderr stays valid UTF-8) with a span covering both.
    const RunResult r = run_cli({"simplify", "\xC2\xA4"}, "2>&1 1>/dev/null");
    INFO(r.output);
    CHECK(r.exit_code == 1);
    CHECK(contains(r.output, "\\xC2\\xA4")); // whole character, not a lone 0xC2
    CHECK(contains(r.output, "\n    ^~"));    // caret spans both bytes
}

TEST_CASE("cli: caret diagnostic stays aligned across tabs and newlines") {
    // A tab must collapse to one column so the caret lands under the offender.
    const RunResult tabbed = run_cli({"simplify", "a\t@"}, "2>&1 1>/dev/null");
    INFO(tabbed.output);
    CHECK(tabbed.exit_code == 1);
    CHECK(contains(tabbed.output, "    a @\n      ^"));

    // A newline must not split the echoed line; it renders as a visible "\n".
    const RunResult newlined = run_cli({"simplify", "a\n@"}, "2>&1 1>/dev/null");
    INFO(newlined.output);
    CHECK(newlined.exit_code == 1);
    CHECK(contains(newlined.output, "    a\\n@\n       ^"));
}

// ---------------------------------------------------------------------------
// Linear systems (DESIGN.md §9b): solve with a top-level ';' in the input
// ---------------------------------------------------------------------------

TEST_CASE("cli: solve routes a ';'-separated system to the system solver") {
    const RunResult r = run_cli({"solve", "x + y = 3; x - y = 1", "x", "y"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "x = 2\n"));
    CHECK(contains(r.output, "y = 1\n"));
    CHECK(contains(r.output, "method: gaussian elimination"));
}

TEST_CASE("cli: system variables are inferred from the free symbols") {
    // Two equations, two free symbols: the union {x, y} is the default.
    const RunResult r = run_cli({"solve", "x + y = 3; x - y = 1"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "x = 2\n"));
    CHECK(contains(r.output, "y = 1\n"));
}

TEST_CASE("cli: underdetermined system prints pivot values and a free line") {
    const RunResult r = run_cli({"solve", "x + y = 3; 2x + 2y = 6", "x", "y"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "x = -y + 3\n"));  // pivot references the free var
    CHECK(contains(r.output, "free: y\n"));
    CHECK(contains(r.output, "method: gaussian elimination"));
}

TEST_CASE("cli: inconsistent system is an answer, not an error") {
    const RunResult r = run_cli({"solve", "x + y = 1; x + y = 2", "x", "y"});
    INFO(r.output);
    CHECK(r.exit_code == 0);  // a definitive "no solution" IS an answer
    CHECK(contains(r.output, "no solution (inconsistent system)"));
}

TEST_CASE("cli: nonlinear system reports the linearity warning") {
    const RunResult r = run_cli({"solve", "x*y = 1; x - y = 0", "x", "y"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "unable to solve the system"));
    CHECK(contains(r.output,
                   "warning: system is not linear in the requested variables"));
}

TEST_CASE("cli: ambiguous system variables exit 2 and list the symbols") {
    // Three free symbols but only two equations: inference must refuse.
    const RunResult r =
        run_cli({"solve", "x + y + z = 1; x - y = 2"}, "2>&1 1>/dev/null");
    INFO(r.output);
    CHECK(r.exit_code == 2);
    CHECK(contains(r.output, "usage error:"));
    CHECK(contains(r.output, "x"));
    CHECK(contains(r.output, "y"));
    CHECK(contains(r.output, "z"));
}

TEST_CASE("cli: constants and function names are rejected as system variables") {
    // Regression: `solve "x=1; y=2" pi` used to pass the constant through as
    // a variable (free: pi plus baffling warnings). Same doctrine as eval
    // bindings: exit 2 with a usage error.
    const RunResult r1 = run_cli({"solve", "x=1; y=2", "pi"}, "2>&1 1>/dev/null");
    INFO(r1.output);
    CHECK(r1.exit_code == 2);
    CHECK(contains(r1.output, "usage error:"));
    CHECK(contains(r1.output, "'pi' is a constant"));

    const RunResult r2 = run_cli({"solve", "x=1; y=2", "sin"}, "2>&1 1>/dev/null");
    INFO(r2.output);
    CHECK(r2.exit_code == 2);
    CHECK(contains(r2.output, "usage error:"));
    CHECK(contains(r2.output, "'sin' is not a valid variable name"));
}

TEST_CASE("cli: single-equation solve is unaffected by system routing") {
    // No ';' in the input: the §9 single-equation path, byte for byte.
    const RunResult r = run_cli({"solve", "x + y = 3", "x"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "x = -y + 3\n"));
    CHECK(contains(r.output, "method: linear"));
    CHECK(!contains(r.output, "gaussian"));
}

TEST_CASE("cli: REPL solves a system with comma-separated variables") {
    const RunResult r = run_repl("solve x+y=3; x-y=1, x, y\nquit\n");
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "x = 2"));
    CHECK(contains(r.output, "y = 1"));
    CHECK(contains(r.output, "method: gaussian elimination"));
}

// ---------------------------------------------------------------------------
// REPL variable assignment (docs/proposals/variable-assignment.md; contract
// condensed in DESIGN.md §10). All piped-REPL sessions: the environment is
// REPL-only state, so every case here goes through run_repl.
// ---------------------------------------------------------------------------

TEST_CASE("repl assign: definition echoes the canonical plain form") {
    const RunResult r = run_repl("a := 2\n"
                                 "b := 2/4\n"
                                 "x_{max} := 10\n"
                                 "E_1 := x + y = 3\n"
                                 "quit\n");
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "a := 2\n"));
    CHECK(contains(r.output, "b := 1/2\n"));       // parse-time canonicalization
    CHECK(contains(r.output, "x_{max} := 10\n"));  // re-parseable spelling
    CHECK(contains(r.output, "E_1 := x + y = 3\n"));
}

TEST_CASE("repl assign: lazy chain resolves at use and follows redefinition") {
    const RunResult r = run_repl("f := g + 1\n"  // g not defined yet — fine
                                 "g := x^2\n"
                                 "f\n"
                                 "g := x^3\n"
                                 "f\n"
                                 "diff f, x\n"
                                 "quit\n");
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "x^2 + 1"));
    CHECK(contains(r.output, "x^3 + 1"));  // redefining g updates f
    CHECK(contains(r.output, "3*x^2"));    // diff resolves f (excluding x)
}

TEST_CASE("repl assign: resolution is independent of definition order") {
    const RunResult forward = run_repl("f := g + 1\ng := x^2\nf + y\nquit\n");
    const RunResult backward = run_repl("g := x^2\nf := g + 1\nf + y\nquit\n");
    INFO(forward.output);
    INFO(backward.output);
    CHECK(forward.exit_code == 0);
    CHECK(backward.exit_code == 0);
    // Same env in shuffled definition orders -> the identical resolved line.
    CHECK(contains(forward.output, "x^2 + y + 1\n"));
    CHECK(contains(backward.output, "x^2 + y + 1\n"));
}

TEST_CASE("repl assign: self-reference and indirect cycles are rejected, session alive") {
    const RunResult r = run_repl("a := a + 1\n"
                                 "a := b + 1\n"
                                 "b := a^2\n"
                                 "a := 5\n"  // still works after both errors
                                 "a\n"
                                 "quit\n");
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "error: 'a' cannot be defined in terms of itself"));
    CHECK(contains(r.output, "error: assignment would create a cycle: b -> a -> b"));
    CHECK(contains(r.output, "5"));
}

TEST_CASE("repl assign: invalid targets get the spec diagnostics") {
    const RunResult r = run_repl("E1 := 2\n"
                                 "speed := 5\n"
                                 "pi := 3\n"
                                 "e := 2.7\n"
                                 "sin := x\n"
                                 "x :=\n"
                                 "x + 1\n"  // session still alive
                                 "quit\n");
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output,
                   "error: assignment target must be a single variable name "
                   "(e.g. x, alpha, E_1) — 'E1' reads as E*1; did you mean E_1?"));
    CHECK(contains(r.output, "variables are single letters (a-z), Greek names, "
                             "or subscripted (v_1)"));
    CHECK(contains(r.output, "assignment targets follow the same rule — try a "
                             "subscripted name like s_max := 5"));
    CHECK(contains(r.output, "error: cannot assign to the constant 'pi'"));
    CHECK(contains(r.output, "error: cannot assign to the constant 'e'"));
    CHECK(contains(r.output, "error: cannot assign to the function name 'sin'"));
    CHECK(contains(r.output, "error: assignment needs a value (e.g. x := 2)"));
    CHECK(contains(r.output, "x + 1"));
}

TEST_CASE("repl assign: vars, unset, and clear manage the environment") {
    const RunResult r = run_repl("vars\n"  // empty environment
                                 "a := 2\n"
                                 "f := g + 1\n"
                                 "E_1 := x + y = 3\n"
                                 "vars\n"
                                 "unset q\n"  // unknown name is a note, not an error
                                 "unset f\n"
                                 "vars\n"
                                 "clear\n"
                                 "vars\n"
                                 "clear\n"  // empty clear still reports
                                 "f\n"      // f is an ordinary symbol again
                                 "quit\n");
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "no variables defined"));
    // Definition order: a before f before E_1.
    const std::size_t a_pos = r.output.find("a := 2\nf := g + 1\nE_1 := x + y = 3");
    CHECK(a_pos != std::string::npos);
    CHECK(contains(r.output, "note: 'q' is not defined"));
    CHECK(contains(r.output, "cleared 2 assignment(s)"));
    CHECK(contains(r.output, "cleared 0 assignment(s)"));
    CHECK(contains(r.output, ">>> f\n"));  // bare f prints itself after clear
    CHECK(!contains(r.output, "error:"));
}

TEST_CASE("repl assign: solving for an assigned variable warns and proceeds") {
    const RunResult r = run_repl("x := 3\n"
                                 "solve x^2 = 9, x\n"
                                 "unset x\n"
                                 "solve x^2 = 9, x\n"
                                 "quit\n");
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "x = -3"));
    CHECK(contains(r.output, "x = 3"));
    CHECK(contains(r.output,
                   "warning: 'x' has an assigned value (x := 3), which is "
                   "ignored while solving for it; 'unset x' removes the "
                   "assignment"));
    // After unset the warning is gone: exactly one occurrence.
    const std::size_t first = r.output.find("has an assigned value");
    REQUIRE(first != std::string::npos);
    CHECK(r.output.find("has an assigned value", first + 1) == std::string::npos);
}

TEST_CASE("repl assign: diff/integrate/collect exclude the variable and warn") {
    const RunResult r = run_repl("x := 3\n"
                                 "diff x^2, x\n"
                                 "collect x*y + x*z, x\n"
                                 "integrate x^2, x\n"
                                 "quit\n");
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "2*x"));         // not d/dx of 9 == 0
    CHECK(contains(r.output, "x*(y + z)"));
    CHECK(contains(r.output, "x^3/3 + C"));
    // One warning per command.
    std::size_t count = 0;
    for (std::size_t pos = r.output.find("has an assigned value");
         pos != std::string::npos;
         pos = r.output.find("has an assigned value", pos + 1)) {
        ++count;
    }
    CHECK(count == 3);
}

TEST_CASE("repl assign: bare equation disambiguates, and truths are evaluated") {
    const RunResult r = run_repl("m := 2\n"
                                 "m*x = 6\n"     // one symbol left -> solve for x
                                 "a := 2\n"
                                 "a + 1 = 3\n"   // none left, true
                                 "a + 1 = 4\n"   // none left, false
                                 "x + y = 1\n"   // several left -> today's prompt
                                 "quit\n");
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "x = 3"));
    CHECK(contains(r.output, "method: linear"));
    CHECK(contains(r.output, "equation holds (identity)"));
    CHECK(contains(r.output, "equation is false (contradiction)"));
    CHECK(contains(r.output, "use: solve <equation>, <variable>"));
}

TEST_CASE("repl assign: a 66-deep acyclic chain resolves (depth guard is cycle-only)") {
    // Regression: a fixed depth bound of 64 misdiagnosed legal deep lazy
    // chains as "internal error: assignment cycle detected". The guard must
    // fire only on true cycles (which the visiting set detects).
    std::string in;
    for (int i = 1; i < 66; ++i) {
        in += "c_" + std::to_string(i) + " := c_" + std::to_string(i + 1) + " + 1\n";
    }
    in += "c_66 := 1\n"
          "c_1\n"
          "quit\n";
    const RunResult r = run_repl(in);
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, ">>> 66\n"));  // c_1 = 1 + 65
    CHECK(!contains(r.output, "cycle"));
    CHECK(!contains(r.output, "error:"));
}

TEST_CASE("repl assign: inexact truth tests answer with a caveat, not a certainty") {
    // Regression: a symbol-free difference the simplifier cannot fold to an
    // exact Number was judged by |difference| < 1e-12 and printed as
    // "identity" — sin(1e-7)^2 is nonzero, so that claim was false. The
    // numeric path must hedge; only the exact-fold path may say
    // identity/contradiction.
    const RunResult r = run_repl("sin(1/10000000)^2 = 0\n"
                                 "sin(1) = 2\n"
                                 "2/4 = 1/2\n"
                                 "1 = 2\n"
                                 "quit\n");
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "equation holds numerically (lhs - rhs ≈ "));
    CHECK(contains(r.output, "; not verified exactly)"));
    CHECK(contains(r.output, "equation is false numerically (lhs - rhs ≈ "));
    // Exact folds keep the spec's certainty words.
    CHECK(contains(r.output, "equation holds (identity)"));
    CHECK(contains(r.output, "equation is false (contradiction)"));
    // The inexact line never claims identity: exactly one identity print.
    const std::size_t first = r.output.find("(identity)");
    REQUIRE(first != std::string::npos);
    CHECK(r.output.find("(identity)", first + 1) == std::string::npos);
}

TEST_CASE("repl assign: unset accepts the spelling that vars displays") {
    // Regression: `vars` prints `x_{max} := 10` but `unset x_{max}` said
    // "not defined" — only the internal spelling x_max worked. Both must.
    const RunResult r = run_repl("x_{max} := 10\n"
                                 "unset x_{max}\n"
                                 "vars\n"
                                 "y_{min} := 1\n"
                                 "unset y_min\n"  // internal spelling still works
                                 "vars\n"
                                 "unset z_{top}\n"
                                 "quit\n");
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(!contains(r.output, "'x_{max}' is not defined"));
    CHECK(!contains(r.output, "'y_min' is not defined"));
    const std::size_t count =
        [&] {
            std::size_t n = 0;
            for (std::size_t pos = r.output.find("no variables defined");
                 pos != std::string::npos;
                 pos = r.output.find("no variables defined", pos + 1)) {
                ++n;
            }
            return n;
        }();
    CHECK(count == 2);  // both unsets actually removed their binding
    // Unknown names still get the note, echoing the name as typed.
    CHECK(contains(r.output, "note: 'z_{top}' is not defined"));
}

TEST_CASE("repl assign: solve infers the variable even when it is assigned") {
    // Regression: `x := 3` then `solve x^2 = 9` resolved x away and errored
    // with "the input has no free symbols" while the bare equation form
    // answered. With one free symbol in the unresolved input, solve treats
    // it as the requested variable: exclude, solve, warn (§7 doctrine).
    const RunResult r = run_repl("x := 3\n"
                                 "solve x^2 = 9\n"
                                 "solve 1 = 2\n"  // symbol-free input keeps its error
                                 "quit\n");
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "x = -3"));
    CHECK(contains(r.output, "x = 3"));
    CHECK(contains(r.output, "warning: 'x' has an assigned value (x := 3), "
                             "which is ignored while solving for it"));
    CHECK(contains(r.output, "no free symbols"));
}

TEST_CASE("repl assign: eval and subs explicit bindings shadow with a note") {
    const RunResult r = run_repl("x := 3\n"
                                 "eval x^2 + y, y=1\n"  // env supplies x
                                 "eval x + 1, x=10\n"   // explicit x wins
                                 "subs x^2, x=u\n"      // explicit substitution wins
                                 "subs x^2 + z, z=1\n"  // env still applies to x
                                 "quit\n");
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "10\n"));  // 3^2 + 1
    CHECK(contains(r.output, "11\n"));
    CHECK(contains(r.output, "note: binding x=10 overrides the assignment "
                             "x := 3 for this command"));
    CHECK(contains(r.output, "u^2"));
    CHECK(contains(r.output, "note: binding x=u overrides the assignment "
                             "x := 3 for this command"));
    CHECK(contains(r.output, "10\n"));  // 3^2 + 1 again via z=1
}

TEST_CASE("repl assign: latex and debug are display verbs and never resolve") {
    const RunResult r = run_repl("f := x^2\n"
                                 "latex f\n"
                                 "debug f + 1\n"
                                 "f\n"  // the computing path still resolves
                                 "quit\n");
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, ">>> f\n"));           // latex f prints f, not x^2
    CHECK(contains(r.output, "(add 1 f)"));         // debug shows the input as typed
    CHECK(contains(r.output, "x^2"));               // bare f resolves
    CHECK(!contains(r.output, "x^{2}"));            // latex never saw the value
}

TEST_CASE("repl assign: equation-valued names only stand where equations may") {
    const RunResult r = run_repl("E_1 := x + y = 3\n"
                                 "E_2 := x - y = 1\n"
                                 "E_1 + 1\n"        // inside an expression: error
                                 "h := E_1 + 2\n"   // lazy: definition is legal...
                                 "h\n"              // ...use is the same error
                                 "solve E_1; E_2, x, y\n"
                                 "solve E_1, x\n"   // single whole-segment use
                                 "quit\n");
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "error: 'E_1' names an equation and cannot be "
                             "used inside an expression"));
    CHECK(contains(r.output, "h := E_1 + 2"));  // definition echoed
    CHECK(contains(r.output, "x = 2"));
    CHECK(contains(r.output, "y = 1"));
    CHECK(contains(r.output, "method: gaussian elimination"));
    CHECK(contains(r.output, "x = -y + 3"));    // solve E_1, x (linear in x)
}

TEST_CASE("repl assign: definite-integral bounds resolve from the environment") {
    const RunResult r = run_repl("a := 2\n"
                                 "integrate x^2, x, 0, a\n"
                                 "quit\n");
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "value = 8/3"));
    CHECK(contains(r.output, "method: FTC"));
}

TEST_CASE("repl assign: overflow during resolution keeps the session alive") {
    const RunResult r = run_repl("k := 10^18\n"
                                 "k^2\n"  // (10^18)^2 overflows 64-bit rationals
                                 "6 * 7\n"
                                 "quit\n");
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "error:"));
    CHECK(contains(r.output, "overflow"));
    CHECK(contains(r.output, "42"));  // recovered
}

namespace {

struct ParityVector {
    std::vector<std::string> defs;      // `name := value` lines, table order
    std::string input;                  // probed expression
    std::vector<std::string> excluded;  // designated symbols (may be empty)
    std::string csv;                    // parents-first subs CSV ("-" = none)
    std::string expected;               // simplified resolved plain output
};

std::vector<std::string> split_on(const std::string& s, char sep) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == sep) {
            parts.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return parts;
}

/// tests/resolution_vectors.tsv — the shared table also consumed by
/// tools/web_vars_test.mjs (variable-assignment spec §10).
std::vector<ParityVector> load_parity_vectors() {
    std::ifstream in(MATHSOLVER_PARITY_VECTORS);
    REQUIRE(in.is_open());
    std::vector<ParityVector> vectors;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const std::vector<std::string> cols = split_on(line, '\t');
        REQUIRE(cols.size() == 5);
        ParityVector v;
        v.defs = split_on(cols[0], ';');
        v.input = cols[1];
        if (cols[2] != "-") {
            v.excluded = split_on(cols[2], ',');
        }
        v.csv = cols[3];
        v.expected = cols[4];
        vectors.push_back(std::move(v));
    }
    REQUIRE(!vectors.empty());
    return vectors;
}

/// The REPL probe for one vector: the bare input when nothing is excluded,
/// or `subs input, v=v, ...` — an explicit no-op substitution per excluded
/// name is exactly the §7 exclusion mechanism (explicit names shadow).
std::string parity_probe(const ParityVector& v) {
    if (v.excluded.empty()) {
        return v.input;
    }
    std::string probe = "subs " + v.input;
    for (const std::string& name : v.excluded) {
        probe += ", " + name + "=" + name;
    }
    return probe;
}

}  // namespace

TEST_CASE("repl assign: resolution parity vectors, forward and reversed order") {
    // Order-independence (§5.2): the same environment defined in any order
    // resolves identically. The TS resolver runs the identical table in
    // tools/web_vars_test.mjs.
    for (const ParityVector& v : load_parity_vectors()) {
        const std::string probe = parity_probe(v);
        for (const bool reversed : {false, true}) {
            std::string script;
            if (reversed) {
                for (auto it = v.defs.rbegin(); it != v.defs.rend(); ++it) {
                    script += *it + "\n";
                }
            } else {
                for (const std::string& def : v.defs) {
                    script += def + "\n";
                }
            }
            script += probe + "\nquit\n";
            const RunResult r = run_repl(script);
            INFO("probe: " << probe << (reversed ? " (env reversed)" : ""));
            INFO(r.output);
            CHECK(r.exit_code == 0);
            CHECK(contains(r.output, v.expected + "\n"));
        }
    }
}

TEST_CASE("repl assign: resolution is subs (parity vectors, §10 property)") {
    // Applying the same bindings via one `subs` invocation in parents-first
    // order — the CSV the table shares with the TS resolver — must print the
    // same output as environment resolution.
    for (const ParityVector& v : load_parity_vectors()) {
        if (!v.excluded.empty() || v.csv == "-") {
            continue;
        }
        const RunResult r = run_repl("subs " + v.input + ", " + v.csv + "\nquit\n");
        INFO("subs " << v.input << ", " << v.csv);
        INFO(r.output);
        CHECK(r.exit_code == 0);
        CHECK(contains(r.output, v.expected + "\n"));
    }
}

TEST_CASE("repl assign: one-shot subcommands stay stateless") {
    // ':' keeps its lex error outside the REPL — assignment is REPL/web-only.
    const RunResult r = run_cli({"simplify", "x := 3"}, "2>&1 1>/dev/null");
    INFO(r.output);
    CHECK(r.exit_code == 1);
    CHECK(contains(r.output, "unexpected character ':'"));
}

TEST_CASE("repl assign: full session mirrors the spec's worked transcript") {
    // docs/proposals/variable-assignment.md §12.1 (the session defines seven
    // bindings by the time `clear` runs).
    const RunResult r = run_repl("a := 2\n"
                                 "a^3 + a\n"
                                 "f := g + 1\n"
                                 "g := x^2\n"
                                 "f\n"
                                 "diff f, x\n"
                                 "g := sin(x)\n"
                                 "diff f, x\n"
                                 "a := a + 1\n"
                                 "b := c + 1\n"
                                 "c := b^2\n"
                                 "vars\n"
                                 "x := 3\n"
                                 "solve x^2 = 9, x\n"
                                 "unset x\n"
                                 "m := 2\n"
                                 "m*x = 6\n"
                                 "E_1 := x + y = 3\n"
                                 "E_2 := x - y = 1\n"
                                 "solve E_1; E_2, x, y\n"
                                 "latex f\n"
                                 "subs f, g=t\n"
                                 "integrate x^2, x, 0, a\n"
                                 "clear\n"
                                 "f\n"
                                 "quit\n");
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "a := 2\n"));
    CHECK(contains(r.output, "10\n"));                     // a^3 + a
    CHECK(contains(r.output, "x^2 + 1"));                  // f under g := x^2
    CHECK(contains(r.output, "2*x"));                      // diff f, x
    CHECK(contains(r.output, "cos(x)"));                   // after g := sin(x)
    CHECK(contains(r.output, "'a' cannot be defined in terms of itself"));
    CHECK(contains(r.output, "assignment would create a cycle: c -> b -> c"));
    // vars lists in definition order.
    CHECK(contains(r.output, "a := 2\nf := g + 1\ng := sin(x)\nb := c + 1"));
    CHECK(contains(r.output, "x = -3"));
    CHECK(contains(r.output, "x = 3"));
    CHECK(contains(r.output, "'x' has an assigned value (x := 3)"));
    CHECK(contains(r.output, "method: linear"));           // m*x = 6 -> x = 3
    CHECK(contains(r.output, "x = 2\ny = 1\nmethod: gaussian elimination"));
    CHECK(contains(r.output, ">>> f\n"));                  // latex f -> f
    CHECK(contains(r.output, "t + 1"));                    // subs f, g=t
    CHECK(contains(r.output, "value = 8/3"));
    CHECK(contains(r.output, "cleared 7 assignment(s)"));
}

TEST_CASE("cli: '--' ends option parsing so '--x' can be passed as input") {
    const RunResult escaped = run_cli({"simplify", "--", "--x"});
    INFO(escaped.output);
    CHECK(escaped.exit_code == 0);
    CHECK(escaped.output == "x\n"); // '--x' is a double unary minus of x

    // Without the escape hatch, a leading '--' remains a usage error.
    const RunResult rejected = run_cli({"simplify", "--x"}, "2>&1 1>/dev/null");
    CHECK(rejected.exit_code == 2);
    CHECK(contains(rejected.output, "unknown option"));
}

TEST_CASE("cli: stirling prints the classic series with accuracy notes") {
    const RunResult r = run_cli({"stirling", "x", "3"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "1/(12*x)"));
    CHECK(contains(r.output, "1/(360*x^3)"));
    CHECK(contains(r.output, "1/(1260*x^5)"));
    CHECK(contains(r.output, "ln Gamma(10)"));

    const RunResult bad = run_cli({"stirling", "x+1"}, "2>&1 1>/dev/null");
    CHECK(bad.exit_code == 2);
    CHECK(contains(bad.output, "variable name"));
}

TEST_CASE("cli: seq recognizes patterns and predicts terms") {
    const RunResult fib = run_cli({"seq", "0", "1", "1", "2", "3", "5", "8"});
    INFO(fib.output);
    CHECK(fib.exit_code == 0);
    CHECK(contains(fib.output, "Fibonacci"));
    CHECK(contains(fib.output, "a(n+2) = a(n+1) + a(n)"));
    CHECK(contains(fib.output, "next: 13, 21, 34"));
    CHECK(contains(fib.output, "sqrt(5)"));

    const RunResult sq = run_cli({"seq", "1", "4", "9", "16", "25"});
    CHECK(contains(sq.output, "n^2 + 2*n + 1"));

    const RunResult bad = run_cli({"seq", "1", "2", "x", "4"}, "2>&1 1>/dev/null");
    CHECK(bad.exit_code == 2);
    CHECK(contains(bad.output, "exact numbers"));
}

TEST_CASE("cli: pade approximant") {
    // exp(x) [2/2] = (12 + 6x + x^2)/(12 - 6x + x^2); printed with 1/12 scaling.
    const RunResult r = run_cli({"pade", "exp(x)", "2", "2"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "x/2"));   // numerator/denominator both carry x/2
    CHECK(contains(r.output, "/12"));   // the x^2/12 terms

    // A non-integer degree is a usage error (exit code 2).
    const RunResult bad = run_cli({"pade", "exp(x)", "2", "x"}, "2>&1 1>/dev/null");
    CHECK(bad.exit_code == 2);

    // REPL form with an explicit variable.
    const RunResult repl = run_repl("pade exp(t), 1, 1, t\nquit\n");
    CHECK(contains(repl.output, "t/2"));
}

TEST_CASE("cli: rootcount and isolate") {
    const RunResult c = run_cli({"rootcount", "(x-1)(x-2)(x-3)"});
    INFO(c.output);
    CHECK(c.exit_code == 0);
    CHECK(contains(c.output, "3 distinct real roots"));

    // Interval count: only +sqrt(2) lies in (0, 5].
    const RunResult ci = run_cli({"rootcount", "x^2 - 2", "x", "0", "5"});
    CHECK(contains(ci.output, "1 distinct real root in (0, 5]"));

    // Exact rational roots reported exactly.
    const RunResult iso = run_cli({"isolate", "2x^2 - 3x + 1"});
    CHECK(iso.exit_code == 0);
    CHECK(contains(iso.output, "x = 1/2"));
    CHECK(contains(iso.output, "x = 1"));

    // Irrational root approximated (plastic number ~1.3247).
    const RunResult plastic = run_cli({"isolate", "x^3 - x - 1"});
    CHECK(contains(plastic.output, "1.32471"));

    // Equation form via the REPL (roots of x^2 = 2).
    const RunResult repl = run_repl("isolate x^2 = 2\nquit\n");
    CHECK(contains(repl.output, "1.41421"));

    // Symbolic coefficients are a usage-style error.
    const RunResult bad = run_cli({"rootcount", "x^2 + a"}, "2>&1 1>/dev/null");
    CHECK(bad.exit_code != 0);
}
