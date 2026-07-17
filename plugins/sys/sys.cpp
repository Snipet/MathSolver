// Continuous-time LTI systems plugin (docs/PLUGINS.md).
//
// Transfer-function analysis in the Laplace domain, built on the CAS for
// polynomial input and on native numerics (sys_lti.{hpp,cpp}) for roots,
// responses, and simulation.
//
// Commands:
//   sys.tf <num poly in s>, <den poly in s>
//       Analyze H(s): poles/zeros (with damping), stability, DC gain,
//       pole-zero map, Bode magnitude/phase, step + impulse response.
//   sys.ode <LTI ODE in y and u>       e.g.  sys.ode y'' + 3y' + 2y = u' + u
//       Convert to H(s) (zero initial conditions) and run the same analysis.
//   sys.c2d <num>, <den>, <fs Hz>
//       Discretize via the bilinear transform into digital biquads (reusing
//       the dsp plugin's tested zpk machinery) and show both responses.

#include "../dsp/dsp_design.hpp"
#include "mathsolver/plugin.hpp"
#include "sys_lti.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace mathsolver::plugins;
using sys::cd;
using sys::RationalTF;

constexpr int k_bode_points = 300;

std::string jnum_array(const std::vector<double>& v) {
    std::string out = "[";
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i > 0) out += ",";
        out += jnum(v[i]);
    }
    return out + "]";
}

/// "s^2 + 3 s + 2" from ascending coefficients (for display only).
std::string poly_text(const std::vector<double>& c) {
    std::string out;
    for (std::size_t k = c.size(); k-- > 0;) {
        if (std::abs(c[k]) < 1e-12 && c.size() > 1) {
            continue;
        }
        const bool neg = c[k] < 0;
        const double mag = std::abs(c[k]);
        if (out.empty()) {
            out += neg ? "-" : "";
        } else {
            out += neg ? " - " : " + ";
        }
        const bool unit = std::abs(mag - 1.0) < 1e-12 && k > 0;
        if (!unit) {
            out += std::format("{:g}", mag);
        }
        if (k > 0) {
            if (!unit) out += " ";
            out += k == 1 ? "s" : std::format("s^{}", k);
        }
    }
    return out.empty() ? "0" : out;
}

// --- analysis blocks --------------------------------------------------------

struct Analysis {
    std::vector<cd> poles;
    std::vector<cd> zeros;
    bool stable = true;
    bool marginal = false;
    double slowest = 0.0; ///< min |Re p| over stable poles (settling rate)
    double fastest = 1.0; ///< max |p| (dynamics scale)
};

Analysis analyze(const RationalTF& tf) {
    Analysis a;
    a.poles = sys::poly_roots(tf.den);
    a.zeros = sys::poly_roots(tf.num);
    double slowest = 1e300;
    for (const cd& p : a.poles) {
        if (p.real() > 1e-9) {
            a.stable = false;
        } else if (p.real() > -1e-9) {
            a.marginal = true;
        } else {
            slowest = std::min(slowest, -p.real());
        }
        a.fastest = std::max(a.fastest, std::abs(p));
    }
    for (const cd& z : a.zeros) {
        a.fastest = std::max(a.fastest, std::abs(z));
    }
    a.slowest = slowest == 1e300 ? 1.0 : slowest;
    return a;
}

std::string roots_table_block(const Analysis& a) {
    std::string rows = "[";
    bool first = true;
    const auto add_rows = [&](const std::vector<cd>& roots, const char* type) {
        for (const cd& r : roots) {
            if (!first) rows += ",";
            first = false;
            const double wn = std::abs(r);
            // Damping ratio is meaningful for complex pairs and stable reals.
            const double zeta = wn > 1e-12 ? -r.real() / wn : 0.0;
            rows += std::format("[{},{},{},{},{}]", jstr(type), jnum(r.real()),
                                jnum(r.imag()), jnum(wn), jnum(zeta));
        }
    };
    add_rows(a.poles, "pole");
    add_rows(a.zeros, "zero");
    rows += "]";
    return std::format(
        "{{\"type\":\"table\",\"title\":\"Poles and zeros\","
        "\"columns\":[\"type\",\"Re\",\"Im\",\"|s| (rad/s)\",\"damping\"],"
        "\"rows\":{}}}",
        rows);
}

std::string pzmap_block(const Analysis& a) {
    const auto coords = [](const std::vector<cd>& roots, bool imag) {
        std::vector<double> out;
        for (const cd& r : roots) {
            out.push_back(imag ? r.imag() : r.real());
        }
        return out;
    };
    // Two point series over a shared x sample list is not possible (series
    // share x), so emit poles and zeros as separate series with the union x
    // and nulls where the other series has no sample. Simpler: one series per
    // root set with its own x is unsupported — instead encode each set with
    // x = Re, ys = Im as a scatter.
    std::string series = "[";
    const std::vector<double> px = coords(a.poles, false);
    const std::vector<double> py = coords(a.poles, true);
    const std::vector<double> zx = coords(a.zeros, false);
    const std::vector<double> zy = coords(a.zeros, true);
    // Pack: x list = poles' Re then zeros' Re; each series carries nulls
    // outside its own range so the chart scatters them independently.
    std::string xs = "[";
    std::string pys = "[";
    std::string zys = "[";
    for (std::size_t i = 0; i < px.size() + zx.size(); ++i) {
        if (i > 0) {
            xs += ",";
            pys += ",";
            zys += ",";
        }
        if (i < px.size()) {
            xs += jnum(px[i]);
            pys += jnum(py[i]);
            zys += "null";
        } else {
            xs += jnum(zx[i - px.size()]);
            pys += "null";
            zys += jnum(zy[i - px.size()]);
        }
    }
    xs += "]";
    pys += "]";
    zys += "]";
    series += std::format(
        "{{\"label\":\"poles\",\"ys\":{},\"points\":true,\"shape\":\"x\"}}", pys);
    if (!zx.empty()) {
        series += std::format(
            ",{{\"label\":\"zeros\",\"ys\":{},\"points\":true,\"shape\":\"o\"}}",
            zys);
    }
    series += "]";
    return std::format(
        "{{\"type\":\"series\",\"title\":\"Pole-zero map (s-plane)\","
        "\"xlabel\":\"Re\",\"ylabel\":\"Im\",\"x\":{},\"series\":{},"
        "\"vlines\":[{{\"x\":0,\"label\":\"j\\u03c9\"}}]}}",
        xs, series);
}

struct BodeGrid {
    std::vector<double> w, mag_db, phase_deg;
};

BodeGrid bode(const RationalTF& tf, const Analysis& a) {
    BodeGrid g;
    double wmin = 1e300;
    double wmax = 0.0;
    const auto widen = [&](const std::vector<cd>& roots) {
        for (const cd& r : roots) {
            const double m = std::abs(r);
            if (m > 1e-9) {
                wmin = std::min(wmin, m);
                wmax = std::max(wmax, m);
            }
        }
    };
    widen(a.poles);
    widen(a.zeros);
    if (wmin > wmax) {
        wmin = 0.1;
        wmax = 10.0;
    }
    wmin /= 100.0;
    wmax *= 100.0;
    double prev_phase = 0.0;
    bool have_prev = false;
    for (int i = 0; i < k_bode_points; ++i) {
        const double t = static_cast<double>(i) / (k_bode_points - 1);
        const double w = wmin * std::pow(wmax / wmin, t);
        const cd h = sys::tf_eval(tf, cd{0.0, w});
        g.w.push_back(w);
        g.mag_db.push_back(20.0 * std::log10(std::abs(h)));
        double ph = std::arg(h);
        if (have_prev) {
            ph += 2.0 * std::numbers::pi *
                  std::round((prev_phase - ph) / (2.0 * std::numbers::pi));
        }
        if (std::isfinite(ph)) {
            prev_phase = ph;
            have_prev = true;
        }
        g.phase_deg.push_back(ph * 180.0 / std::numbers::pi);
    }
    return g;
}

std::string bode_blocks(const RationalTF& tf, const Analysis& a) {
    const BodeGrid g = bode(tf, a);
    return std::format(
        "{{\"type\":\"series\",\"title\":\"Bode magnitude\","
        "\"xlabel\":\"\\u03c9 (rad/s)\",\"ylabel\":\"|H| (dB)\",\"logx\":true,"
        "\"x\":{},\"series\":[{{\"label\":\"|H(j\\u03c9)|\",\"ys\":{}}}]}},"
        "{{\"type\":\"series\",\"title\":\"Bode phase\","
        "\"xlabel\":\"\\u03c9 (rad/s)\",\"ylabel\":\"phase (deg)\",\"logx\":true,"
        "\"x\":{},\"series\":[{{\"label\":\"arg H(j\\u03c9)\",\"ys\":{}}}]}}",
        jnum_array(g.w), jnum_array(g.mag_db), jnum_array(g.w),
        jnum_array(g.phase_deg));
}

std::string time_blocks(const RationalTF& tf, const Analysis& a) {
    // Horizon: settle the slowest stable mode; bounded for unstable systems.
    const double horizon = a.stable && !a.marginal ? 8.0 / a.slowest
                                                   : 20.0 / a.fastest;
    const sys::TimeSim sim = sys::simulate(tf, horizon);
    std::string extra;
    if (sim.biproper) {
        extra =
            ",{\"type\":\"text\",\"lines\":[\"Biproper system: the impulse "
            "response additionally carries a D\\u00b7\\u03b4(t) direct "
            "feedthrough term not drawn above.\"]}";
    }
    return std::format(
        "{{\"type\":\"series\",\"title\":\"Time response\","
        "\"xlabel\":\"t (s)\",\"ylabel\":\"amplitude\","
        "\"x\":{},\"series\":[{{\"label\":\"step\",\"ys\":{}}},"
        "{{\"label\":\"impulse\",\"ys\":{}}}]}}{}",
        jnum_array(sim.t), jnum_array(sim.step), jnum_array(sim.impulse), extra);
}

std::string analysis_result(const std::string& title, const RationalTF& tf,
                            const std::string& origin_note) {
    const Analysis a = analyze(tf);
    const double dc = sys::dc_gain(tf);
    const char* verdict = !a.stable   ? "unstable (pole in the right half-plane)"
                          : a.marginal ? "marginally stable (pole on the jω axis)"
                                       : "stable";
    std::string kv = std::format(
        "{{\"type\":\"kv\",\"items\":[[\"H(s)\",{}],[\"Order\",\"{}\"],"
        "[\"Stability\",{}],[\"DC gain\",{}]{}]}}",
        jstr(std::format("({}) / ({})", poly_text(tf.num), poly_text(tf.den))),
        tf.den.size() - 1, jstr(verdict),
        jstr(std::isfinite(dc) ? std::format("{:g}", dc) : "infinite (pole at s = 0)"),
        origin_note.empty()
            ? ""
            : std::format(",[\"Derived from\",{}]", jstr(origin_note)));
    return std::format("{{\"ok\":true,\"title\":{},\"blocks\":[{},{},{},{},{}]}}",
                       jstr(title), kv, roots_table_block(a), pzmap_block(a),
                       bode_blocks(tf, a), time_blocks(tf, a));
}

// --- commands ---------------------------------------------------------------

std::string cmd_tf(const std::vector<std::string>& args) {
    if (args.size() != 2) {
        return error_json("usage: sys.tf <numerator poly in s>, <denominator poly in s>");
    }
    try {
        const RationalTF tf = sys::make_tf(args[0], args[1]);
        return analysis_result(
            std::format("H(s) = ({}) / ({})", poly_text(tf.num), poly_text(tf.den)),
            tf, "");
    } catch (const sys::SysError& e) {
        return error_json(e.what());
    }
}

std::string cmd_ode(const std::vector<std::string>& args) {
    if (args.empty()) {
        return error_json(
            "usage: sys.ode <LTI ODE>, e.g. sys.ode y'' + 3y' + 2y = u' + u");
    }
    // The ODE contains no commas; re-join defensively in case of stray splits.
    std::string text = args[0];
    for (std::size_t i = 1; i < args.size(); ++i) {
        text += "," + args[i];
    }
    try {
        const RationalTF tf = sys::ode_to_tf(text);
        return analysis_result(
            std::format("H(s) = ({}) / ({})", poly_text(tf.num), poly_text(tf.den)),
            tf, text);
    } catch (const sys::SysError& e) {
        return error_json(e.what());
    }
}

std::string cmd_c2d(const std::vector<std::string>& args) {
    if (args.size() != 3) {
        return error_json("usage: sys.c2d <num poly in s>, <den poly in s>, <fs Hz>");
    }
    double fs = 0.0;
    try {
        std::size_t pos = 0;
        fs = std::stod(args[2], &pos);
        if (pos != args[2].size() || !(fs > 0.0)) {
            throw std::invalid_argument("fs");
        }
    } catch (const std::exception&) {
        return error_json(
            std::format("sample rate must be a positive number, got '{}'", args[2]));
    }
    try {
        const RationalTF tf = sys::make_tf(args[0], args[1]);
        const Analysis a = analyze(tf);
        // Analog zpk -> bilinear -> biquads, via the dsp plugin's machinery.
        dsp::Zpk analog;
        analog.z = a.zeros;
        analog.p = a.poles;
        analog.k = tf.num.back(); // den is monic
        const std::vector<dsp::Biquad> sections =
            dsp::zpk_to_biquads(dsp::bilinear_zpk(analog, fs));

        std::string rows = "[";
        for (std::size_t i = 0; i < sections.size(); ++i) {
            const dsp::Biquad& s = sections[i];
            if (i > 0) rows += ",";
            rows += std::format("[{},{},{},{},{},{}]", i + 1, jnum(s.b0), jnum(s.b1),
                                jnum(s.b2), jnum(s.a1), jnum(s.a2));
        }
        rows += "]";
        const std::string table = std::format(
            "{{\"type\":\"table\",\"title\":\"Digital biquad cascade (a0 = 1)\","
            "\"columns\":[\"section\",\"b0\",\"b1\",\"b2\",\"a1\",\"a2\"],"
            "\"rows\":{}}}",
            rows);

        // Digital magnitude response vs the analog one, over f in Hz.
        std::string xs = "[";
        std::string dig = "[";
        std::string ana = "[";
        const double lo = fs / 2.0 * 1e-3;
        const double hi = fs / 2.0 * 0.999;
        for (int i = 0; i < k_bode_points; ++i) {
            const double t = static_cast<double>(i) / (k_bode_points - 1);
            const double f = lo * std::pow(hi / lo, t);
            if (i > 0) {
                xs += ",";
                dig += ",";
                ana += ",";
            }
            xs += jnum(f);
            dig += jnum(dsp::magnitude_db(sections, f, fs));
            ana += jnum(20.0 * std::log10(std::abs(
                       sys::tf_eval(tf, cd{0.0, 2.0 * std::numbers::pi * f}))));
        }
        xs += "]";
        dig += "]";
        ana += "]";
        const std::string chart = std::format(
            "{{\"type\":\"series\",\"title\":\"Magnitude response\","
            "\"xlabel\":\"frequency (Hz)\",\"ylabel\":\"gain (dB)\",\"logx\":true,"
            "\"x\":{},\"series\":[{{\"label\":\"digital H(z)\",\"ys\":{}}},"
            "{{\"label\":\"analog H(s)\",\"ys\":{}}}]}}",
            xs, dig, ana);

        const std::string kv = std::format(
            "{{\"type\":\"kv\",\"items\":[[\"H(s)\",{}],[\"Sample rate\",\"{} Hz\"],"
            "[\"Method\",\"bilinear transform\"],[\"Sections\",\"{}\"]]}}",
            jstr(std::format("({}) / ({})", poly_text(tf.num), poly_text(tf.den))),
            fs, sections.size());
        const char* note =
            "{\"type\":\"text\",\"lines\":[\"The bilinear transform warps "
            "frequency: the analog and digital curves diverge toward Nyquist. "
            "Feed the sections to dsp.freqz for phase and group delay.\"]}";
        return std::format(
            "{{\"ok\":true,\"title\":{},\"blocks\":[{},{},{},{}]}}",
            jstr(std::format("Discretized H(z) @ {} Hz", fs)), kv, table, chart,
            note);
    } catch (const sys::SysError& e) {
        return error_json(e.what());
    }
}

class SysPlugin final : public Plugin {
  public:
    std::string_view name() const override { return "sys"; }
    std::string_view version() const override { return "0.1.0"; }
    std::string_view summary() const override {
        return "Continuous-time LTI systems: transfer functions, ODE -> H(s), "
               "poles/zeros, Bode, step/impulse, discretization";
    }
    std::vector<CommandInfo> commands() const override {
        return {
            {"tf", "Analyze a transfer function H(s) = num/den",
             "sys.tf <num poly in s>, <den poly in s>   e.g. sys.tf s+1, s^2+3s+2"},
            {"ode", "Convert an LTI ODE to H(s) and analyze it",
             "sys.ode <ODE in y and u>   e.g. sys.ode y'' + 3y' + 2y = u' + u"},
            {"c2d", "Discretize H(s) to digital biquads (bilinear)",
             "sys.c2d <num poly in s>, <den poly in s>, <fs Hz>"},
        };
    }
    std::string invoke(std::string_view command,
                       const std::vector<std::string>& args) const override {
        try {
            if (command == "tf") return cmd_tf(args);
            if (command == "ode") return cmd_ode(args);
            if (command == "c2d") return cmd_c2d(args);
            return error_json(std::format("sys has no command '{}'", command));
        } catch (const std::exception& e) {
            return error_json(std::format("sys internal error: {}", e.what()));
        } catch (...) {
            return error_json("sys internal error");
        }
    }
};

} // namespace

namespace mathsolver::plugins {

std::unique_ptr<Plugin> make_sys_plugin() {
    return std::make_unique<SysPlugin>();
}

} // namespace mathsolver::plugins
