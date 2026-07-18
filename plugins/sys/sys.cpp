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

struct WRange {
    double wmin, wmax;
};

WRange bode_range(const Analysis& a) {
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
    return {wmin / 100.0, wmax * 100.0};
}

BodeGrid bode(const RationalTF& tf, const Analysis& a) {
    BodeGrid g;
    const auto [wmin, wmax] = bode_range(a);
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

std::string bode_blocks(const RationalTF& tf, const Analysis& a,
                        const std::string& vlines_json) {
    const BodeGrid g = bode(tf, a);
    return std::format(
        "{{\"type\":\"series\",\"title\":\"Bode magnitude\","
        "\"xlabel\":\"\\u03c9 (rad/s)\",\"ylabel\":\"|H| (dB)\",\"logx\":true,"
        "\"x\":{},\"series\":[{{\"label\":\"|H(j\\u03c9)|\",\"ys\":{}}}]{}}},"
        "{{\"type\":\"series\",\"title\":\"Bode phase\","
        "\"xlabel\":\"\\u03c9 (rad/s)\",\"ylabel\":\"phase (deg)\",\"logx\":true,"
        "\"x\":{},\"series\":[{{\"label\":\"arg H(j\\u03c9)\",\"ys\":{}}}]{}}}",
        jnum_array(g.w), jnum_array(g.mag_db), vlines_json, jnum_array(g.w),
        jnum_array(g.phase_deg), vlines_json);
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

    // Classical margins (treating H as an open loop), with the crossover
    // frequencies marked on both Bode charts.
    const auto [wmin, wmax] = bode_range(a);
    const sys::Margins margins = sys::compute_margins(tf, wmin, wmax);
    std::string margin_rows;
    std::string vlines;
    if (!margins.gain.empty()) {
        const auto& gm = margins.gain.front();
        margin_rows += std::format(",[\"Gain margin\",\"{:.2f} dB @ {:.4g} rad/s\"]",
                                   gm.db, gm.freq);
        vlines += std::format("{{\"x\":{},\"label\":\"\\u03c9pc\"}}", jnum(gm.freq));
    } else {
        margin_rows += ",[\"Gain margin\",\"∞ (no -180° crossing)\"]";
    }
    if (!margins.phase.empty()) {
        const auto& pm = margins.phase.front();
        margin_rows += std::format(
            ",[\"Phase margin\",\"{:.1f}° @ {:.4g} rad/s\"]", pm.deg, pm.freq);
        if (!vlines.empty()) vlines += ",";
        vlines += std::format("{{\"x\":{},\"label\":\"\\u03c9gc\"}}", jnum(pm.freq));
    } else {
        margin_rows += ",[\"Phase margin\",\"∞ (no 0 dB crossing)\"]";
    }
    const std::string vlines_json =
        vlines.empty() ? "" : std::format(",\"vlines\":[{}]", vlines);

    std::string kv = std::format(
        "{{\"type\":\"kv\",\"items\":[[\"H(s)\",{}],[\"Order\",\"{}\"],"
        "[\"Stability\",{}],[\"DC gain\",{}]{}{}]}}",
        jstr(std::format("({}) / ({})", poly_text(tf.num), poly_text(tf.den))),
        tf.den.size() - 1, jstr(verdict),
        jstr(std::isfinite(dc) ? std::format("{:g}", dc) : "infinite (pole at s = 0)"),
        margin_rows,
        origin_note.empty()
            ? ""
            : std::format(",[\"Derived from\",{}]", jstr(origin_note)));
    return std::format("{{\"ok\":true,\"title\":{},\"blocks\":[{},{},{},{},{}]}}",
                       jstr(title), kv, roots_table_block(a), pzmap_block(a),
                       bode_blocks(tf, a, vlines_json), time_blocks(tf, a));
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

std::string cmd_feedback(const std::vector<std::string>& args) {
    if (args.size() != 2 && args.size() != 3) {
        return error_json(
            "usage: sys.feedback <num poly in s>, <den poly in s>[, <K>]   — "
            "closed loop K·G/(1 + K·G) under unity feedback");
    }
    double k = 1.0;
    if (args.size() == 3) {
        try {
            std::size_t pos = 0;
            k = std::stod(args[2], &pos);
            if (pos != args[2].size()) {
                throw std::invalid_argument("k");
            }
        } catch (const std::exception&) {
            return error_json(
                std::format("gain K must be a number, got '{}'", args[2]));
        }
    }
    try {
        const RationalTF g = sys::make_tf(args[0], args[1]);
        const RationalTF t = sys::feedback_unity(g, k);
        return analysis_result(
            std::format("Closed loop T(s) = ({}) / ({})", poly_text(t.num),
                        poly_text(t.den)),
            t,
            std::format("unity feedback around G(s) = ({}) / ({}) with K = {:g}",
                        poly_text(g.num), poly_text(g.den), k));
    } catch (const sys::SysError& e) {
        return error_json(e.what());
    }
}

std::string cmd_rlocus(const std::vector<std::string>& args) {
    if (args.size() != 2 && args.size() != 3) {
        return error_json(
            "usage: sys.rlocus <num poly in s>, <den poly in s>[, <K max>]");
    }
    double kmax = 100.0;
    if (args.size() == 3) {
        try {
            std::size_t pos = 0;
            kmax = std::stod(args[2], &pos);
            if (pos != args[2].size() || !(kmax > 0.0)) {
                throw std::invalid_argument("kmax");
            }
        } catch (const std::exception&) {
            return error_json(
                std::format("K max must be a positive number, got '{}'", args[2]));
        }
    }
    try {
        const RationalTF g = sys::make_tf(args[0], args[1]);
        const Analysis a = analyze(g);

        // Geometric gain sweep over four decades up to K max.
        std::vector<double> gains;
        constexpr int kSteps = 160;
        for (int i = 0; i < kSteps; ++i) {
            const double t = static_cast<double>(i) / (kSteps - 1);
            gains.push_back(kmax * std::pow(1e-4, 1.0 - t));
        }
        const auto branches = sys::root_locus(g, gains);

        // Scatter: locus points, then open-loop poles (x) and zeros (o),
        // packed with per-series nulls (series share one x list).
        std::vector<double> xs;
        std::vector<std::string> locus_ys, pole_ys, zero_ys;
        const auto push = [&](double x, double locus, double pole, double zero,
                              int which) {
            xs.push_back(x);
            locus_ys.push_back(which == 0 ? jnum(locus) : "null");
            pole_ys.push_back(which == 1 ? jnum(pole) : "null");
            zero_ys.push_back(which == 2 ? jnum(zero) : "null");
        };
        for (const auto& set : branches) {
            for (const cd& p : set) {
                push(p.real(), p.imag(), 0, 0, 0);
            }
        }
        for (const cd& p : a.poles) push(p.real(), 0, p.imag(), 0, 1);
        for (const cd& z : a.zeros) push(z.real(), 0, 0, z.imag(), 2);

        const auto join = [](const std::vector<std::string>& v) {
            std::string out = "[";
            for (std::size_t i = 0; i < v.size(); ++i) {
                if (i > 0) out += ",";
                out += v[i];
            }
            return out + "]";
        };
        std::string series = std::format(
            "{{\"label\":\"locus (K: {:g} → {:g})\",\"ys\":{},\"points\":true,"
            "\"shape\":\"o\"}},"
            "{{\"label\":\"open-loop poles\",\"ys\":{},\"points\":true,"
            "\"shape\":\"x\"}}",
            gains.front(), kmax, join(locus_ys), join(pole_ys));
        if (!a.zeros.empty()) {
            series += std::format(
                ",{{\"label\":\"open-loop zeros\",\"ys\":{},\"points\":true,"
                "\"shape\":\"o\"}}",
                join(zero_ys));
        }
        const std::string chart = std::format(
            "{{\"type\":\"series\",\"title\":\"Root locus (closed-loop poles as "
            "K grows)\",\"xlabel\":\"Re\",\"ylabel\":\"Im\",\"x\":{},"
            "\"series\":[{}],\"vlines\":[{{\"x\":0,\"label\":\"j\\u03c9\"}}]}}",
            jnum_array(xs), series);

        // Find the smallest swept K that destabilizes the loop, if any.
        std::string kcrit = "stable for the whole sweep";
        for (std::size_t gi = 0; gi < branches.size(); ++gi) {
            const bool unstable = std::any_of(
                branches[gi].begin(), branches[gi].end(),
                [](const cd& p) { return p.real() > 1e-9; });
            if (unstable) {
                kcrit = std::format("closed loop goes unstable near K ≈ {:.4g}",
                                    gains[gi]);
                break;
            }
        }
        const std::string kv = std::format(
            "{{\"type\":\"kv\",\"items\":[[\"G(s)\",{}],[\"Sweep\",\"K = {:g} … "
            "{:g} ({} points)\"],[\"Verdict\",{}]]}}",
            jstr(std::format("({}) / ({})", poly_text(g.num), poly_text(g.den))),
            gains.front(), kmax, kSteps, jstr(kcrit));
        return std::format(
            "{{\"ok\":true,\"title\":{},\"blocks\":[{},{}]}}",
            jstr(std::format("Root locus of G(s) = ({}) / ({})", poly_text(g.num),
                             poly_text(g.den))),
            kv, chart);
    } catch (const sys::SysError& e) {
        return error_json(e.what());
    }
}

// --- discrete-time analysis (z-domain) --------------------------------------

std::string zpoly_text(const std::vector<double>& c) {
    // Reuse poly_text but rename the variable s -> z.
    std::string s = poly_text(c);
    for (char& ch : s) {
        if (ch == 's') ch = 'z';
    }
    return s;
}

std::string z_roots_table(const Analysis& a) {
    std::string rows = "[";
    bool first = true;
    const auto add_rows = [&](const std::vector<cd>& roots, const char* type) {
        for (const cd& r : roots) {
            if (!first) rows += ",";
            first = false;
            rows += std::format("[{},{},{},{},{}]", jstr(type), jnum(r.real()),
                                jnum(r.imag()), jnum(std::abs(r)),
                                jnum(std::arg(r) * 180.0 / std::numbers::pi));
        }
    };
    add_rows(a.poles, "pole");
    add_rows(a.zeros, "zero");
    rows += "]";
    return std::format(
        "{{\"type\":\"table\",\"title\":\"Poles and zeros\","
        "\"columns\":[\"type\",\"Re\",\"Im\",\"|z|\",\"angle (deg)\"],"
        "\"rows\":{}}}",
        rows);
}

/// Pole-zero map with the unit circle drawn (equal aspect), poles as x and
/// zeros as o. The circle is a connected line traced by angle, so it renders
/// as a circle regardless of x monotonicity.
std::string z_pzmap_block(const Analysis& a) {
    constexpr int kCircle = 121;
    std::string xs = "[";
    std::string circle = "[";
    std::string poles = "[";
    std::string zeros = "[";
    const auto append = [&](std::string& dst, const std::string& v) {
        if (dst.size() > 1) dst += ",";
        dst += v;
    };
    for (int i = 0; i < kCircle; ++i) {
        const double th = 2.0 * std::numbers::pi * i / (kCircle - 1);
        append(xs, jnum(std::cos(th)));
        append(circle, jnum(std::sin(th)));
        append(poles, "null");
        append(zeros, "null");
    }
    for (const cd& p : a.poles) {
        append(xs, jnum(p.real()));
        append(circle, "null");
        append(poles, jnum(p.imag()));
        append(zeros, "null");
    }
    for (const cd& z : a.zeros) {
        append(xs, jnum(z.real()));
        append(circle, "null");
        append(poles, "null");
        append(zeros, jnum(z.imag()));
    }
    xs += "]";
    circle += "]";
    poles += "]";
    zeros += "]";
    std::string series = std::format(
        "[{{\"label\":\"unit circle\",\"ys\":{}}},"
        "{{\"label\":\"poles\",\"ys\":{},\"points\":true,\"shape\":\"x\"}}",
        circle, poles);
    if (!a.zeros.empty()) {
        series += std::format(
            ",{{\"label\":\"zeros\",\"ys\":{},\"points\":true,\"shape\":\"o\"}}",
            zeros);
    }
    series += "]";
    return std::format(
        "{{\"type\":\"series\",\"title\":\"Pole-zero map (z-plane)\","
        "\"xlabel\":\"Re\",\"ylabel\":\"Im\",\"equal\":true,\"x\":{},"
        "\"series\":{}}}",
        xs, series);
}

std::string z_response_blocks(const RationalTF& tf, double fs) {
    constexpr int kPoints = 300;
    std::vector<double> f, mag, phase;
    const double lo = fs / 2.0 * 1e-3;
    const double hi = fs / 2.0 * 0.999;
    double prev = 0.0;
    bool have = false;
    for (int i = 0; i < kPoints; ++i) {
        const double t = static_cast<double>(i) / (kPoints - 1);
        const double ff = lo * std::pow(hi / lo, t);
        const cd h = sys::tfz_eval(tf, ff, fs);
        f.push_back(ff);
        mag.push_back(20.0 * std::log10(std::abs(h)));
        double ph = std::arg(h);
        if (have) {
            ph += 2.0 * std::numbers::pi *
                  std::round((prev - ph) / (2.0 * std::numbers::pi));
        }
        if (std::isfinite(ph)) {
            prev = ph;
            have = true;
        }
        phase.push_back(ph * 180.0 / std::numbers::pi);
    }
    return std::format(
        "{{\"type\":\"series\",\"title\":\"Magnitude response\","
        "\"xlabel\":\"frequency (Hz)\",\"ylabel\":\"|H| (dB)\",\"logx\":true,"
        "\"x\":{},\"series\":[{{\"label\":\"|H(e^{{j\\u03c9}})|\",\"ys\":{}}}]}},"
        "{{\"type\":\"series\",\"title\":\"Phase response\","
        "\"xlabel\":\"frequency (Hz)\",\"ylabel\":\"phase (deg)\",\"logx\":true,"
        "\"x\":{},\"series\":[{{\"label\":\"arg H\",\"ys\":{}}}]}}",
        jnum_array(f), jnum_array(mag), jnum_array(f), jnum_array(phase));
}

std::string z_time_block(const RationalTF& tf) {
    const sys::DiscreteSim sim = sys::simulate_discrete(tf, 64);
    return std::format(
        "{{\"type\":\"series\",\"title\":\"Time response\","
        "\"xlabel\":\"n (samples)\",\"ylabel\":\"amplitude\","
        "\"x\":{},\"series\":[{{\"label\":\"step\",\"ys\":{}}},"
        "{{\"label\":\"impulse\",\"ys\":{}}}]}}",
        jnum_array(sim.n), jnum_array(sim.step), jnum_array(sim.impulse));
}

std::string cmd_tfz(const std::vector<std::string>& args) {
    if (args.size() != 3) {
        return error_json(
            "usage: sys.tfz <num poly in z>, <den poly in z>, <fs Hz>   "
            "(positive powers of z)");
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
        const RationalTF tf = sys::make_tfz(args[0], args[1]);
        Analysis a;
        a.poles = sys::poly_roots(tf.den);
        a.zeros = sys::poly_roots(tf.num);
        double max_r = 0.0;
        for (const cd& p : a.poles) {
            max_r = std::max(max_r, std::abs(p));
        }
        const bool stable = max_r < 1.0 - 1e-9;
        const bool marginal = std::abs(max_r - 1.0) <= 1e-9;
        const char* verdict = stable      ? "stable (all poles inside |z| = 1)"
                              : marginal   ? "marginally stable (pole on |z| = 1)"
                                           : "unstable (pole outside |z| = 1)";
        const double dc = sys::tfz_eval(tf, 0.0, fs).real(); // z = 1
        const std::string kv = std::format(
            "{{\"type\":\"kv\",\"items\":[[\"H(z)\",{}],[\"Order\",\"{}\"],"
            "[\"Stability\",{}],[\"Max |pole|\",\"{:.4g}\"],[\"DC gain\",\"{:g}\"],"
            "[\"Sample rate\",\"{} Hz\"]]}}",
            jstr(std::format("({}) / ({})", zpoly_text(tf.num), zpoly_text(tf.den))),
            tf.den.size() - 1, jstr(verdict), max_r, dc, fs);
        return std::format(
            "{{\"ok\":true,\"title\":{},\"blocks\":[{},{},{},{},{}]}}",
            jstr(std::format("H(z) = ({}) / ({}) @ {} Hz", zpoly_text(tf.num),
                             zpoly_text(tf.den), fs)),
            kv, z_roots_table(a), z_pzmap_block(a), z_response_blocks(tf, fs),
            z_time_block(tf));
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

/// sys.dde <rhs in t, x, xd>, <tau>, <phi(t)>, <T> — method-of-steps
/// simulation of x'(t) = f(t, x, x(t - tau)) with history phi.
std::string cmd_dde(const std::vector<std::string>& args) {
    if (args.size() != 4) {
        return error_json(
            "usage: sys.dde <x' = f(t, x, x_d)>, <tau>, <phi(t)>, <T>   "
            "(x_d is x(t - tau); e.g. sys.dde -x_d, 1, 1, 20)");
    }
    double tau = 0.0;
    double horizon = 0.0;
    try {
        std::size_t pos = 0;
        tau = std::stod(args[1], &pos);
        if (pos != args[1].size()) throw std::invalid_argument("tau");
        pos = 0;
        horizon = std::stod(args[3], &pos);
        if (pos != args[3].size()) throw std::invalid_argument("T");
    } catch (const std::exception&) {
        return error_json("tau and T must be numbers");
    }
    try {
        const sys::DdeResult r = sys::solve_dde(args[0], tau, args[2], horizon);
        const std::string kv = std::format(
            "{{\"type\":\"kv\",\"items\":[[\"Equation\",{}],"
            "[\"Delay tau\",{}],[\"History phi(t)\",{}],[\"Steps\",\"{}\"],"
            "[\"x(T)\",{}]]}}",
            jstr("x'(t) = " + args[0] + "  with x_d = x(t - tau)"),
            jstr(std::format("{:.6g}", tau)), jstr(args[2]), r.steps,
            jstr(std::format("{:.6g}", r.x.back())));
        const std::string chart = std::format(
            "{{\"type\":\"series\",\"title\":\"Delay response\","
            "\"xlabel\":\"t\",\"ylabel\":\"x(t)\",\"x\":{},"
            "\"series\":[{{\"label\":\"x(t)\",\"ys\":{}}}],"
            "\"vlines\":[{{\"x\":0,\"label\":\"t = 0\"}}]}}",
            jnum_array(r.t), jnum_array(r.x));
        return std::format(
            "{{\"ok\":true,\"title\":{},\"blocks\":[{},{}]}}",
            jstr(std::format("DDE x' = {} (tau = {:.4g})", args[0], tau)), kv,
            chart);
    } catch (const sys::SysError& e) {
        return error_json(e.what());
    }
}

class SysPlugin final : public Plugin {
  public:
    std::string_view name() const override { return "sys"; }
    std::string_view version() const override { return "0.4.0"; }
    std::string_view summary() const override {
        return "LTI systems: transfer functions (s and z), ODE -> H(s), "
               "feedback, margins, root locus, discretization, delay "
               "equations";
    }
    std::vector<CommandInfo> commands() const override {
        return {
            {"tf", "Analyze a transfer function H(s) = num/den (incl. margins)",
             "sys.tf <num poly in s>, <den poly in s>",
             "sys.tf s+1, s^2+3s+2"},
            {"ode", "Convert an LTI ODE to H(s) and analyze it",
             "sys.ode <ODE in y and u>",
             "sys.ode y'' + 3y' + 2y = u' + u"},
            {"feedback", "Closed loop K·G/(1 + K·G) under unity feedback",
             "sys.feedback <num poly in s>, <den poly in s>[, <K>]",
             "sys.feedback 1, s(s+1)(s+2), 2"},
            {"rlocus", "Root locus: closed-loop poles as K sweeps",
             "sys.rlocus <num poly in s>, <den poly in s>[, <K max>]",
             "sys.rlocus 1, s^3 + 3s^2 + 2s"},
            {"tfz", "Analyze a discrete transfer function H(z) (positive powers)",
             "sys.tfz <num poly in z>, <den poly in z>, <fs Hz>",
             "sys.tfz z, z^2 - 0.5z + 0.06, 8000"},
            {"c2d", "Discretize H(s) to digital biquads (bilinear)",
             "sys.c2d <num poly in s>, <den poly in s>, <fs Hz>",
             "sys.c2d 1, s+1, 100"},
            {"dde", "Delay differential equation x' = f(t, x, x(t - tau)) by "
                    "the method of steps",
             "sys.dde <f(t, x, x_d)>, <tau>, <phi(t)>, <T>",
             "sys.dde -x_d, 1, 1, 20"},
        };
    }
    std::string invoke(std::string_view command,
                       const std::vector<std::string>& args) const override {
        try {
            if (command == "tf") return cmd_tf(args);
            if (command == "ode") return cmd_ode(args);
            if (command == "feedback") return cmd_feedback(args);
            if (command == "rlocus") return cmd_rlocus(args);
            if (command == "tfz") return cmd_tfz(args);
            if (command == "c2d") return cmd_c2d(args);
            if (command == "dde") return cmd_dde(args);
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
