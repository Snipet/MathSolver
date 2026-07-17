#pragma once

// Continuous-time LTI system numerics, separated from the plugin command
// layer so the native test suite can verify the math directly
// (tests/test_plugin_sys.cpp).
//
// A transfer function is a real rational H(s) = num(s)/den(s), stored as
// ascending coefficient vectors (num[k] multiplies s^k). Polynomials are
// extracted from CAS expressions via repeated differentiation, so any
// polynomial spelling works ("(s+1)(s+2)" as well as "s^2 + 3s + 2").

#include <complex>
#include <stdexcept>
#include <string>
#include <vector>

namespace mathsolver::plugins::sys {

using cd = std::complex<double>;

class SysError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

struct RationalTF {
    std::vector<double> num; ///< ascending; empty is invalid
    std::vector<double> den; ///< ascending; normalized so den.back() == 1
};

/// Parse `text` as a polynomial in s with numeric coefficients (degree <= 12)
/// via the CAS: c_k = f^(k)(0) / k!. Throws SysError with a usable message.
std::vector<double> poly_from_expr(const std::string& text, int max_degree = 12);

/// Build a proper transfer function from numerator/denominator polynomial
/// text; normalizes the denominator monic. Throws SysError (zero denominator,
/// improper H, non-polynomials).
RationalTF make_tf(const std::string& num_text, const std::string& den_text);

/// Parse an LTI ODE in y (output) and u (input), primes for derivatives:
///   "y'' + 3y' + 2y = u' + u",  "2y' - y = 3u"
/// Coefficients are decimal numbers; y-terms build the denominator, u-terms
/// the numerator. Throws SysError on malformed input, missing y, constant
/// terms, or an improper result.
RationalTF ode_to_tf(const std::string& equation);

/// All roots of the polynomial (Durand-Kerner). Leading/trailing handling:
/// high-order coefficients that are exactly zero are trimmed first.
std::vector<cd> poly_roots(const std::vector<double>& coeffs);

/// H evaluated at a complex point.
cd tf_eval(const RationalTF& tf, cd s);

/// H(0) — may be non-finite (pole at the origin).
double dc_gain(const RationalTF& tf);

/// Classical stability margins of an open-loop H(s), from the jw response.
/// Absent optionals mean the corresponding crossing does not exist in the
/// scanned range (e.g. |H| never reaches 0 dB).
struct Margins {
    /// Gain margin in dB at the phase crossover (-180 deg), and its frequency.
    struct GM {
        double db;
        double freq;
    };
    /// Phase margin in degrees at the gain crossover (|H| = 1), and its
    /// frequency.
    struct PM {
        double deg;
        double freq;
    };
    std::vector<GM> gain;
    std::vector<PM> phase;
};
Margins compute_margins(const RationalTF& tf, double wmin, double wmax);

/// Closed-loop transfer function of G under gain-K unity feedback:
/// T = K G / (1 + K G). Throws SysError if the closed loop degenerates.
RationalTF feedback_unity(const RationalTF& g, double k);

/// Root locus: closed-loop pole sets for each gain in `gains`
/// (poles of den + K num). gains must be positive ascending.
std::vector<std::vector<cd>> root_locus(const RationalTF& g,
                                        const std::vector<double>& gains);

/// Fixed-step RK4 simulation of the step and impulse responses on [0, T]
/// (controllable canonical state-space; the impulse response is the unforced
/// response from x0 = B, plus a D*delta(t) pass-through when biproper).
struct TimeSim {
    std::vector<double> t;
    std::vector<double> step;
    std::vector<double> impulse;
    bool biproper = false; ///< D != 0: impulse carries an extra delta(t).
};
TimeSim simulate(const RationalTF& tf, double horizon, int points = 400);

} // namespace mathsolver::plugins::sys
