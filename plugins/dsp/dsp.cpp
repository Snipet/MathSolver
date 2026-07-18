// DSP filter-design plugin (docs/PLUGINS.md).
//
// Native numeric implementations that would be slow or impractical to express
// through the CAS: IIR filter design and frequency-response evaluation. The
// numerics live in dsp_design.{hpp,cpp} (unit-tested directly); this file is
// the command layer — argument parsing and block-based JSON rendering.
//
// Commands:
//   dsp.butter <type>, <order>, <fc>[, <f2>], <fs>
//   dsp.cheby1 <type>, <order>, <ripple dB>, <fc>[, <f2>], <fs>
//   dsp.cheby2 <type>, <order>, <atten dB>, <fc>[, <f2>], <fs>
//       <type> is lowpass|highpass|bandpass|bandstop (lp/hp/bp/bs/notch);
//       band types take two edges f1, f2. Result blocks: design summary with
//       measured edge gains, the biquad cascade (a0 = 1), and magnitude +
//       phase responses with the edges marked.
//   dsp.freqz <fs Hz>, <b0>,<b1>,<b2>,<a1>,<a2> [, ...more groups of 5]
//       Magnitude, phase, and group delay of an arbitrary biquad cascade.

#include "dsp_design.hpp"
#include "mathsolver/plugin.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace mathsolver::plugins;
using dsp::Biquad;
using dsp::DesignSpec;
using dsp::Family;
using dsp::Kind;

constexpr int k_max_order = 12;
constexpr int k_max_freqz_sections = 16;
constexpr int k_response_points = 300;

std::optional<double> parse_double(const std::string& s) {
    try {
        std::size_t pos = 0;
        const double v = std::stod(s, &pos);
        while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos])) != 0) {
            ++pos;
        }
        if (pos != s.size()) {
            return std::nullopt;
        }
        return v;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<int> parse_int(const std::string& s) {
    const auto v = parse_double(s);
    if (!v || *v != std::floor(*v)) {
        return std::nullopt;
    }
    return static_cast<int>(*v);
}

std::optional<Kind> parse_kind(const std::string& s) {
    if (s == "lowpass" || s == "lp") return Kind::Lowpass;
    if (s == "highpass" || s == "hp") return Kind::Highpass;
    if (s == "bandpass" || s == "bp") return Kind::Bandpass;
    if (s == "bandstop" || s == "bs" || s == "notch" || s == "bandreject")
        return Kind::Bandstop;
    return std::nullopt;
}

bool is_band(Kind k) {
    return k == Kind::Bandpass || k == Kind::Bandstop;
}

const char* kind_label(Kind k) {
    switch (k) {
        case Kind::Lowpass: return "low-pass";
        case Kind::Highpass: return "high-pass";
        case Kind::Bandpass: return "band-pass";
        case Kind::Bandstop: return "band-stop";
    }
    return "?";
}

const char* family_label(Family f) {
    switch (f) {
        case Family::Butterworth: return "Butterworth";
        case Family::Cheby1: return "Chebyshev I";
        case Family::Cheby2: return "Chebyshev II";
        case Family::Elliptic: return "Elliptic";
    }
    return "?";
}

// --- response sampling and block rendering ---------------------------------

struct ResponseGrid {
    std::vector<double> freqs;
    std::vector<double> mag_db;
    std::vector<double> phase_deg; // unwrapped
    std::vector<double> group_delay;
};

template <typename Eval> // Eval: double f -> dsp::ResponsePoint
ResponseGrid sample_response_grid(Eval&& eval, double fs) {
    ResponseGrid g;
    const double lo = fs / 2.0 * 1e-3;
    const double hi = fs / 2.0 * 0.999;
    double prev_phase = 0.0;
    bool have_prev = false;
    for (int i = 0; i < k_response_points; ++i) {
        const double t = static_cast<double>(i) / (k_response_points - 1);
        const double f = lo * std::pow(hi / lo, t);
        const dsp::ResponsePoint r = eval(f);
        g.freqs.push_back(f);
        g.mag_db.push_back(r.mag_db);
        // Unwrap: shift by whole turns to sit nearest the previous sample.
        double ph = r.phase_rad;
        if (have_prev && std::isfinite(ph)) {
            ph += 2.0 * std::numbers::pi *
                  std::round((prev_phase - ph) / (2.0 * std::numbers::pi));
        }
        if (std::isfinite(ph)) {
            prev_phase = ph;
            have_prev = true;
        }
        g.phase_deg.push_back(ph * 180.0 / std::numbers::pi);
        g.group_delay.push_back(r.group_delay);
    }
    return g;
}

ResponseGrid sample_response(const std::vector<Biquad>& sections, double fs) {
    return sample_response_grid(
        [&](double f) { return dsp::response_at(sections, f, fs); }, fs);
}

std::string jnum_array(const std::vector<double>& v) {
    std::string out = "[";
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i > 0) out += ",";
        out += jnum(v[i]); // non-finite -> null -> chart gap
    }
    return out + "]";
}

struct VLine {
    double x;
    std::string label;
};

std::string vlines_field(const std::vector<VLine>& vlines) {
    if (vlines.empty()) {
        return "";
    }
    std::string out = ",\"vlines\":[";
    for (std::size_t i = 0; i < vlines.size(); ++i) {
        if (i > 0) out += ",";
        out += std::format("{{\"x\":{},\"label\":{}}}", jnum(vlines[i].x),
                           jstr(vlines[i].label));
    }
    return out + "]";
}

std::string series_block(const std::string& title, const std::string& ylabel,
                         const std::vector<double>& x, const std::vector<double>& y,
                         const std::string& label,
                         const std::vector<VLine>& vlines) {
    return std::format(
        "{{\"type\":\"series\",\"title\":{},"
        "\"xlabel\":\"frequency (Hz)\",\"ylabel\":{},\"logx\":true,"
        "\"x\":{},\"series\":[{{\"label\":{},\"ys\":{}}}]{}}}",
        jstr(title), jstr(ylabel), jnum_array(x), jstr(label), jnum_array(y),
        vlines_field(vlines));
}

std::string sections_table_block(const std::vector<Biquad>& sections) {
    std::string rows = "[";
    for (std::size_t i = 0; i < sections.size(); ++i) {
        const Biquad& s = sections[i];
        if (i > 0) rows += ",";
        rows += std::format("[{},{},{},{},{},{}]", i + 1, jnum(s.b0), jnum(s.b1),
                            jnum(s.b2), jnum(s.a1), jnum(s.a2));
    }
    rows += "]";
    return std::format(
        "{{\"type\":\"table\",\"title\":\"Biquad cascade (a0 = 1)\","
        "\"columns\":[\"section\",\"b0\",\"b1\",\"b2\",\"a1\",\"a2\"],"
        "\"rows\":{}}}",
        rows);
}

/// Impulse + step response on one linear-time chart. The length adapts to
/// the slowest pole's decay (to 0.1% of scale), clamped to [32, 160] samples.
std::string time_response_block(const std::vector<Biquad>& sections) {
    double rmax = 0.0;
    for (const Biquad& s : sections) {
        const double disc = s.a1 * s.a1 - 4.0 * s.a2;
        if (disc < 0.0) {
            rmax = std::max(rmax, std::sqrt(s.a2));
        } else {
            const double sq = std::sqrt(disc);
            rmax = std::max({rmax, std::abs((-s.a1 + sq) / 2.0),
                             std::abs((-s.a1 - sq) / 2.0)});
        }
    }
    int n = 48;
    if (rmax > 0.0 && rmax < 1.0) {
        n = static_cast<int>(std::ceil(std::log(1e-3) / std::log(rmax)));
    }
    n = std::clamp(n, 32, 160);

    const std::vector<double> impulse = dsp::impulse_response(sections, n);
    std::vector<double> step(impulse.size());
    double acc = 0.0;
    for (std::size_t i = 0; i < impulse.size(); ++i) {
        acc += impulse[i];
        step[i] = acc;
    }
    std::vector<double> x(impulse.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] = static_cast<double>(i);
    }
    return std::format(
        "{{\"type\":\"series\",\"title\":\"Time response\","
        "\"xlabel\":\"n (samples)\",\"ylabel\":\"amplitude\","
        "\"x\":{},\"series\":[{{\"label\":\"impulse h[n]\",\"ys\":{}}},"
        "{{\"label\":\"step s[n]\",\"ys\":{}}}]}}",
        jnum_array(x), jnum_array(impulse), jnum_array(step));
}

constexpr const char* k_cascade_note =
    "{\"type\":\"text\",\"lines\":[\"Cascade the sections in order; each row "
    "is y[n] = b0 x[n] + b1 x[n-1] + b2 x[n-2] - a1 y[n-1] - a2 y[n-2]. Copy "
    "on the table exports full-precision coefficients.\"]}";

// --- the design commands (shared skeleton) ---------------------------------

struct FamilyArgs {
    Family family;
    /// Number of leading args before the frequencies (type, order[, param]).
    int pre_freq_args;
    const char* usage;
};

std::string design_command(const FamilyArgs& fa, const std::vector<std::string>& args) {
    const int pre = fa.pre_freq_args;
    // type, order[, param], fc, fs  (+1 arg when a band type).
    if (static_cast<int>(args.size()) < pre + 2 ||
        static_cast<int>(args.size()) > pre + 3) {
        return error_json(std::format("usage: {}", fa.usage));
    }
    const auto kind = parse_kind(args[0]);
    if (!kind) {
        return error_json(std::format(
            "unknown filter type '{}': expected lowpass, highpass, bandpass, or "
            "bandstop",
            args[0]));
    }
    const bool band = is_band(*kind);
    const int expected = pre + (band ? 3 : 2);
    if (static_cast<int>(args.size()) != expected) {
        return error_json(std::format(
            "{} {} takes {} arguments ({}), got {}", family_label(fa.family),
            kind_label(*kind), expected, fa.usage, args.size()));
    }
    const auto order = parse_int(args[1]);
    if (!order || *order < 1 || *order > k_max_order) {
        return error_json(std::format("order must be an integer in [1, {}], got '{}'",
                                      k_max_order, args[1]));
    }

    DesignSpec spec;
    spec.family = fa.family;
    spec.kind = *kind;
    spec.order = *order;

    if (fa.family == Family::Cheby1 || fa.family == Family::Elliptic) {
        const auto rp = parse_double(args[2]);
        if (!rp || !(*rp > 0.0) || *rp > 12.0) {
            return error_json(std::format(
                "passband ripple must be in (0, 12] dB, got '{}'", args[2]));
        }
        spec.ripple_db = *rp;
    }
    if (fa.family == Family::Cheby2 || fa.family == Family::Elliptic) {
        const int ai = fa.family == Family::Elliptic ? 3 : 2;
        const auto rs = parse_double(args[ai]);
        if (!rs || *rs < 10.0 || *rs > 120.0) {
            return error_json(std::format(
                "stopband attenuation must be in [10, 120] dB, got '{}'", args[ai]));
        }
        spec.atten_db = *rs;
    }

    const int fi = pre; // index of the first frequency argument
    const auto f1 = parse_double(args[fi]);
    const auto f2 = band ? parse_double(args[fi + 1]) : std::optional<double>{0.0};
    const auto fs = parse_double(args[fi + (band ? 2 : 1)]);
    if (!f1 || !(*f1 > 0.0)) {
        return error_json(
            std::format("frequency must be a positive number, got '{}'", args[fi]));
    }
    if (band && (!f2 || !(*f2 > 0.0))) {
        return error_json(std::format("frequency must be a positive number, got '{}'",
                                      args[fi + 1]));
    }
    if (!fs || !(*fs > 0.0)) {
        return error_json(std::format("sample rate must be a positive number, got '{}'",
                                      args[fi + (band ? 2 : 1)]));
    }
    spec.f1 = *f1;
    spec.f2 = band ? *f2 : 0.0;
    spec.fs = *fs;

    std::vector<Biquad> sections;
    try {
        sections = dsp::design_iir(spec);
    } catch (const dsp::DesignError& e) {
        return error_json(e.what());
    }

    // Design summary with exact measured gains at the specified edges.
    std::string kv = std::format(
        "{{\"type\":\"kv\",\"items\":[[\"Family\",{}],[\"Type\",{}],"
        "[\"Order\",\"{}\"],[\"Sections\",\"{}\"]",
        jstr(family_label(fa.family)), jstr(kind_label(*kind)), *order,
        sections.size());
    if (fa.family == Family::Cheby1 || fa.family == Family::Elliptic) {
        kv += std::format(",[\"Passband ripple\",\"{} dB\"]", spec.ripple_db);
    }
    if (fa.family == Family::Cheby2 || fa.family == Family::Elliptic) {
        kv += std::format(",[\"Stopband attenuation\",\"{} dB\"]", spec.atten_db);
    }
    std::vector<VLine> marks;
    std::string title_freqs;
    if (band) {
        kv += std::format(
            ",[\"Edges\",\"{} – {} Hz\"],[\"Gain at f1\",\"{:.2f} dB\"],"
            "[\"Gain at f2\",\"{:.2f} dB\"]",
            spec.f1, spec.f2, dsp::magnitude_db(sections, spec.f1, spec.fs),
            dsp::magnitude_db(sections, spec.f2, spec.fs));
        marks = {{spec.f1, "f1"}, {spec.f2, "f2"}};
        title_freqs = std::format("{}–{} Hz", spec.f1, spec.f2);
    } else {
        kv += std::format(",[\"Cutoff\",\"{} Hz\"],[\"Gain at cutoff\",\"{:.2f} dB\"]",
                          spec.f1, dsp::magnitude_db(sections, spec.f1, spec.fs));
        marks = {{spec.f1, "fc"}};
        title_freqs = std::format("fc {} Hz", spec.f1);
    }
    kv += std::format(",[\"Sample rate\",\"{} Hz\"]]}}", spec.fs);

    const ResponseGrid g = sample_response(sections, spec.fs);
    return std::format(
        "{{\"ok\":true,\"title\":{},\"blocks\":[{},{},{},{},{},{}]}}",
        jstr(std::format("{} {} — order {}, {} @ {} Hz", family_label(fa.family),
                         kind_label(*kind), *order, title_freqs, spec.fs)),
        kv, sections_table_block(sections),
        series_block("Magnitude response", "gain (dB)", g.freqs, g.mag_db, "|H(f)|",
                     marks),
        series_block("Phase response", "phase (deg)", g.freqs, g.phase_deg,
                     "arg H(f)", marks),
        time_response_block(sections), k_cascade_note);
}

// --- FIR (windowed sinc) ----------------------------------------------------

std::optional<dsp::FirWindow> parse_window(const std::string& s) {
    if (s == "rect" || s == "rectangular" || s == "boxcar") return dsp::FirWindow::Rect;
    if (s == "hann" || s == "hanning") return dsp::FirWindow::Hann;
    if (s == "hamming") return dsp::FirWindow::Hamming;
    if (s == "blackman") return dsp::FirWindow::Blackman;
    if (s == "kaiser") return dsp::FirWindow::Kaiser;
    return std::nullopt;
}

const char* window_label(dsp::FirWindow w) {
    switch (w) {
        case dsp::FirWindow::Rect: return "rectangular";
        case dsp::FirWindow::Hann: return "Hann";
        case dsp::FirWindow::Hamming: return "Hamming";
        case dsp::FirWindow::Blackman: return "Blackman";
        case dsp::FirWindow::Kaiser: return "Kaiser";
    }
    return "?";
}

std::string fir_taps_table_block(const std::vector<double>& h) {
    std::string rows = "[";
    for (std::size_t n = 0; n < h.size(); ++n) {
        if (n > 0) rows += ",";
        rows += std::format("[{},{}]", n, jnum(h[n]));
    }
    rows += "]";
    return std::format(
        "{{\"type\":\"table\",\"title\":\"Coefficients\","
        "\"columns\":[\"n\",\"h[n]\"],\"rows\":{}}}",
        rows);
}

std::string fir_time_block(const std::vector<double>& h) {
    const std::size_t n = h.size() + 16;
    std::vector<double> x(n);
    std::vector<double> impulse(n, 0.0);
    std::vector<double> step(n);
    double acc = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = static_cast<double>(i);
        if (i < h.size()) impulse[i] = h[i];
        acc += impulse[i];
        step[i] = acc;
    }
    return std::format(
        "{{\"type\":\"series\",\"title\":\"Time response\","
        "\"xlabel\":\"n (samples)\",\"ylabel\":\"amplitude\","
        "\"x\":{},\"series\":[{{\"label\":\"impulse h[n]\",\"ys\":{}}},"
        "{{\"label\":\"step s[n]\",\"ys\":{}}}]}}",
        jnum_array(x), jnum_array(impulse), jnum_array(step));
}

std::string fir(const std::vector<std::string>& args) {
    constexpr const char* usage =
        "usage: dsp.fir <type>, <taps>, <fc>[, <f2>], <fs>"
        "[, <window>[, <kaiser beta>]]   (windows: rect, hann, hamming, "
        "blackman, kaiser)";
    if (args.size() < 4) {
        return error_json(usage);
    }
    const auto kind = parse_kind(args[0]);
    if (!kind) {
        return error_json(std::format(
            "unknown filter type '{}': expected lowpass, highpass, bandpass, or "
            "bandstop",
            args[0]));
    }
    const bool band = is_band(*kind);
    const std::size_t base = band ? 5 : 4; // type, taps, fc[, f2], fs
    if (args.size() < base || args.size() > base + 2) {
        return error_json(usage);
    }
    const auto taps = parse_int(args[1]);
    if (!taps) {
        return error_json(std::format("taps must be an integer, got '{}'", args[1]));
    }
    dsp::FirSpec spec;
    spec.kind = *kind;
    spec.taps = *taps;
    const auto f1 = parse_double(args[2]);
    const auto f2 = band ? parse_double(args[3]) : std::optional<double>{0.0};
    const auto fs = parse_double(args[band ? 4 : 3]);
    if (!f1 || (band && !f2) || !fs) {
        return error_json("frequencies must be numbers");
    }
    spec.f1 = *f1;
    spec.f2 = band ? *f2 : 0.0;
    spec.fs = *fs;
    if (args.size() > base) {
        const auto w = parse_window(args[base]);
        if (!w) {
            return error_json(std::format(
                "unknown window '{}': expected rect, hann, hamming, blackman, or "
                "kaiser",
                args[base]));
        }
        spec.window = *w;
    }
    if (args.size() > base + 1) {
        if (spec.window != dsp::FirWindow::Kaiser) {
            return error_json("a beta parameter is only meaningful for the kaiser window");
        }
        const auto beta = parse_double(args[base + 1]);
        if (!beta || *beta < 0.0 || *beta > 30.0) {
            return error_json(std::format("kaiser beta must be in [0, 30], got '{}'",
                                          args[base + 1]));
        }
        spec.kaiser_beta = *beta;
    }

    std::vector<double> h;
    try {
        h = dsp::design_fir(spec);
    } catch (const dsp::DesignError& e) {
        return error_json(e.what());
    }

    const auto mag = [&](double f) { return dsp::fir_response_at(h, f, spec.fs).mag_db; };
    std::string window_text = window_label(spec.window);
    if (spec.window == dsp::FirWindow::Kaiser) {
        window_text += std::format(" (beta {})", spec.kaiser_beta);
    }
    std::string kv = std::format(
        "{{\"type\":\"kv\",\"items\":[[\"Design\",\"FIR windowed-sinc\"],"
        "[\"Type\",{}],[\"Taps\",\"{}\"],[\"Window\",{}]",
        jstr(kind_label(*kind)), spec.taps, jstr(window_text));
    std::vector<VLine> marks;
    std::string title_freqs;
    if (band) {
        kv += std::format(
            ",[\"Edges\",\"{} – {} Hz\"],[\"Gain at f1\",\"{:.2f} dB\"],"
            "[\"Gain at f2\",\"{:.2f} dB\"]",
            spec.f1, spec.f2, mag(spec.f1), mag(spec.f2));
        marks = {{spec.f1, "f1"}, {spec.f2, "f2"}};
        title_freqs = std::format("{}–{} Hz", spec.f1, spec.f2);
    } else {
        kv += std::format(",[\"Cutoff\",\"{} Hz\"],[\"Gain at cutoff\",\"{:.2f} dB\"]",
                          spec.f1, mag(spec.f1));
        marks = {{spec.f1, "fc"}};
        title_freqs = std::format("fc {} Hz", spec.f1);
    }
    kv += std::format(
        ",[\"Group delay\",\"{} samples (linear phase)\"],[\"Sample rate\",\"{} "
        "Hz\"]]}}",
        (spec.taps - 1) / 2.0, spec.fs);

    const ResponseGrid g = sample_response_grid(
        [&](double f) { return dsp::fir_response_at(h, f, spec.fs); }, spec.fs);
    return std::format(
        "{{\"ok\":true,\"title\":{},\"blocks\":[{},{},{},{}]}}",
        jstr(std::format("FIR {} — {} taps ({}), {} @ {} Hz", kind_label(*kind),
                         spec.taps, window_text, title_freqs, spec.fs)),
        kv, fir_taps_table_block(h),
        series_block("Magnitude response", "gain (dB)", g.freqs, g.mag_db, "|H(f)|",
                     marks),
        fir_time_block(h));
}

std::string remez(const std::vector<std::string>& args) {
    constexpr const char* usage =
        "usage: dsp.remez <type>, <taps>, <edges...>, <fs>[, <stop weight>]  "
        "(lowpass: fpass, fstop; highpass: fstop, fpass; bandpass: fstop1, "
        "fpass1, fpass2, fstop2; bandstop: fpass1, fstop1, fstop2, fpass2)";
    if (args.size() < 4) {
        return error_json(usage);
    }
    const auto kind = parse_kind(args[0]);
    if (!kind) {
        return error_json(std::format(
            "unknown filter type '{}': expected lowpass, highpass, bandpass, or "
            "bandstop",
            args[0]));
    }
    const bool band = is_band(*kind);
    const int nfreq = band ? 4 : 2;
    const std::size_t base = 2 + static_cast<std::size_t>(nfreq) + 1; // +type,taps,fs
    if (args.size() < base || args.size() > base + 1) {
        return error_json(usage);
    }
    const auto taps = parse_int(args[1]);
    if (!taps) {
        return error_json(std::format("taps must be an integer, got '{}'", args[1]));
    }
    // Edge frequencies (Hz), then the sample rate, then an optional weight.
    std::vector<double> edges;
    for (int i = 0; i < nfreq; ++i) {
        const auto f = parse_double(args[2 + static_cast<std::size_t>(i)]);
        if (!f) {
            return error_json("band edges must be numbers");
        }
        edges.push_back(*f);
    }
    const auto fs = parse_double(args[2 + static_cast<std::size_t>(nfreq)]);
    if (!fs || !(*fs > 0.0)) {
        return error_json("sample rate must be a positive number");
    }
    double stop_weight = 1.0;
    if (args.size() == base + 1) {
        const auto w = parse_double(args[base]);
        if (!w || !(*w > 0.0) || *w > 1000.0) {
            return error_json(std::format(
                "stop weight must be in (0, 1000], got '{}'", args[base]));
        }
        stop_weight = *w;
    }
    // Edges must be strictly increasing and inside (0, fs/2).
    for (std::size_t i = 0; i < edges.size(); ++i) {
        if (!(edges[i] > 0.0 && edges[i] < *fs / 2.0)) {
            return error_json("every band edge must lie in (0, fs/2)");
        }
        if (i > 0 && !(edges[i] > edges[i - 1])) {
            return error_json(
                "band edges must be strictly increasing (transition bands must "
                "not overlap)");
        }
    }

    // Assemble the approximation bands. The passband weight is fixed at 1 and
    // the stopband weight scales the ripple ratio (heavier = deeper stopband).
    const double n = *fs; // for normalizing f -> f/fs
    auto nf = [&](double f) { return f / n; };
    std::vector<dsp::RemezBand> bands;
    switch (*kind) {
        case Kind::Lowpass:
            bands = {{0.0, nf(edges[0]), 1.0, 1.0},
                     {nf(edges[1]), 0.5, 0.0, stop_weight}};
            break;
        case Kind::Highpass:
            bands = {{0.0, nf(edges[0]), 0.0, stop_weight},
                     {nf(edges[1]), 0.5, 1.0, 1.0}};
            break;
        case Kind::Bandpass:
            bands = {{0.0, nf(edges[0]), 0.0, stop_weight},
                     {nf(edges[1]), nf(edges[2]), 1.0, 1.0},
                     {nf(edges[3]), 0.5, 0.0, stop_weight}};
            break;
        case Kind::Bandstop:
            bands = {{0.0, nf(edges[0]), 1.0, 1.0},
                     {nf(edges[1]), nf(edges[2]), 0.0, stop_weight},
                     {nf(edges[3]), 0.5, 1.0, 1.0}};
            break;
    }

    dsp::RemezResult res;
    try {
        res = dsp::remez_fir(*taps, bands);
    } catch (const dsp::DesignError& e) {
        return error_json(e.what());
    }
    const std::vector<double>& h = res.taps;

    // Convert the converged deviation into engineering ripple figures. The
    // weighted error is uniform, so the passband ripple amplitude is δ and the
    // stopband ripple is δ / stop_weight.
    const double dp = res.deviation;                 // passband weight is 1
    const double ds = res.deviation / stop_weight;   // stopband amplitude
    const double pass_ripple_db = 20.0 * std::log10(1.0 + dp);
    const double stop_atten_db = ds > 0.0 ? -20.0 * std::log10(ds) : 0.0;

    std::string edge_text;
    for (std::size_t i = 0; i < edges.size(); ++i) {
        if (i > 0) edge_text += ", ";
        edge_text += std::format("{:g}", edges[i]);
    }
    std::string kv = std::format(
        "{{\"type\":\"kv\",\"items\":[[\"Design\",\"FIR equiripple "
        "(Parks–McClellan)\"],[\"Type\",{}],[\"Taps\",\"{}\"],"
        "[\"Band edges\",\"{} Hz\"],[\"Passband ripple\",\"{:.3f} dB\"],"
        "[\"Stopband atten.\",\"{:.1f} dB\"],[\"Weighted deviation\",\"{:.5f}\"],"
        "[\"Iterations\",\"{}{}\"],[\"Group delay\",\"{} samples (linear "
        "phase)\"],[\"Sample rate\",\"{:g} Hz\"]]}}",
        jstr(kind_label(*kind)), *taps, edge_text, pass_ripple_db, stop_atten_db,
        res.deviation, res.iterations, res.converged ? "" : " (not converged)",
        (*taps - 1) / 2.0, *fs);

    std::vector<VLine> marks;
    for (double e : edges) {
        marks.push_back({e, std::format("{:g}", e)});
    }

    const ResponseGrid g = sample_response_grid(
        [&](double f) { return dsp::fir_response_at(h, f, *fs); }, *fs);
    return std::format(
        "{{\"ok\":true,\"title\":{},\"blocks\":[{},{},{},{}]}}",
        jstr(std::format("Equiripple FIR {} — {} taps, edges {} Hz @ {:g} Hz",
                         kind_label(*kind), *taps, edge_text, *fs)),
        kv, fir_taps_table_block(h),
        series_block("Magnitude response", "gain (dB)", g.freqs, g.mag_db,
                     "|H(f)|", marks),
        fir_time_block(h));
}

std::string freqz(const std::vector<std::string>& args) {
    if (args.size() < 6 || (args.size() - 1) % 5 != 0) {
        return error_json(
            "usage: dsp.freqz <fs Hz>, <b0>,<b1>,<b2>,<a1>,<a2> "
            "[, ...more groups of 5]");
    }
    const auto fs = parse_double(args[0]);
    if (!fs || !(*fs > 0.0)) {
        return error_json(std::format("sample rate fs must be a positive number, got '{}'",
                                      args[0]));
    }
    std::vector<Biquad> sections;
    for (std::size_t i = 1; i + 4 < args.size(); i += 5) {
        double c[5];
        for (int k = 0; k < 5; ++k) {
            const auto v = parse_double(args[i + k]);
            if (!v) {
                return error_json(std::format("'{}' is not a number", args[i + k]));
            }
            c[k] = *v;
        }
        sections.push_back(Biquad{c[0], c[1], c[2], c[3], c[4]});
    }
    if (sections.size() > k_max_freqz_sections) {
        return error_json(std::format("at most {} sections, got {}",
                                      k_max_freqz_sections, sections.size()));
    }
    const ResponseGrid g = sample_response(sections, *fs);
    return std::format(
        "{{\"ok\":true,\"title\":{},\"blocks\":[{},{},{},{},{}]}}",
        jstr(std::format("Frequency response — {} biquad section{} @ {} Hz",
                         sections.size(), sections.size() == 1 ? "" : "s", *fs)),
        sections_table_block(sections),
        series_block("Magnitude response", "gain (dB)", g.freqs, g.mag_db, "|H(f)|",
                     {}),
        series_block("Phase response", "phase (deg)", g.freqs, g.phase_deg,
                     "arg H(f)", {}),
        series_block("Group delay", "delay (samples)", g.freqs, g.group_delay,
                     "tau(f)", {}),
        time_response_block(sections));
}

class DspPlugin final : public Plugin {
  public:
    std::string_view name() const override { return "dsp"; }
    std::string_view version() const override { return "0.4.0"; }
    std::string_view summary() const override {
        return "DSP filter design: Butterworth/Chebyshev/elliptic IIR, "
               "windowed-sinc and equiripple (Parks–McClellan) FIR, frequency "
               "and time responses";
    }
    std::vector<CommandInfo> commands() const override {
        return {
            {"butter", "Butterworth low/high/band-pass/band-stop design",
             "dsp.butter <type>, <order 1-12>, <fc>[, <f2>], <fs>   (type: "
             "lowpass|highpass|bandpass|bandstop)",
             "dsp.butter lowpass, 4, 1000, 48000"},
            {"cheby1", "Chebyshev I design (equiripple passband)",
             "dsp.cheby1 <type>, <order 1-12>, <ripple dB>, <fc>[, <f2>], <fs>",
             "dsp.cheby1 bandpass, 3, 1, 500, 2000, 48000"},
            {"cheby2", "Chebyshev II design (equiripple stopband)",
             "dsp.cheby2 <type>, <order 1-12>, <atten dB>, <fc>[, <f2>], <fs>",
             "dsp.cheby2 lowpass, 4, 40, 2000, 48000"},
            {"ellip", "Elliptic (Cauer) design: sharpest transition per order",
             "dsp.ellip <type>, <order 1-12>, <ripple dB>, <atten dB>, <fc>[, "
             "<f2>], <fs>",
             "dsp.ellip lowpass, 5, 1, 60, 1000, 48000"},
            {"fir", "Linear-phase windowed-sinc FIR design",
             "dsp.fir <type>, <taps 5-255>, <fc>[, <f2>], <fs>[, <window>[, "
             "<kaiser beta>]]",
             "dsp.fir lowpass, 101, 1000, 48000, kaiser, 10"},
            {"remez", "Optimal equiripple FIR (Parks–McClellan)",
             "dsp.remez <type>, <taps 5-255 odd>, <edges...>, <fs>[, <stop "
             "weight>]   (lowpass: fpass, fstop; bandpass: fstop1, fpass1, "
             "fpass2, fstop2)",
             "dsp.remez lowpass, 31, 1000, 1500, 8000"},
            {"freqz", "Magnitude/phase/group delay/time response of a cascade",
             "dsp.freqz <fs Hz>, <b0>,<b1>,<b2>,<a1>,<a2> [, ...more groups of 5]",
             "dsp.freqz 48000, 0.2, 0.4, 0.2, -0.5, 0.3"},
        };
    }
    std::string invoke(std::string_view command,
                       const std::vector<std::string>& args) const override {
        try {
            if (command == "butter") {
                return design_command(
                    {Family::Butterworth, 2,
                     "dsp.butter <type>, <order>, <fc>[, <f2>], <fs>"},
                    args);
            }
            if (command == "cheby1") {
                return design_command(
                    {Family::Cheby1, 3,
                     "dsp.cheby1 <type>, <order>, <ripple dB>, <fc>[, <f2>], <fs>"},
                    args);
            }
            if (command == "cheby2") {
                return design_command(
                    {Family::Cheby2, 3,
                     "dsp.cheby2 <type>, <order>, <atten dB>, <fc>[, <f2>], <fs>"},
                    args);
            }
            if (command == "ellip") {
                return design_command(
                    {Family::Elliptic, 4,
                     "dsp.ellip <type>, <order>, <ripple dB>, <atten dB>, <fc>[, "
                     "<f2>], <fs>"},
                    args);
            }
            if (command == "fir") {
                return fir(args);
            }
            if (command == "remez") {
                return remez(args);
            }
            if (command == "freqz") {
                return freqz(args);
            }
            return error_json(std::format("dsp has no command '{}'", command));
        } catch (const std::exception& e) {
            return error_json(std::format("dsp internal error: {}", e.what()));
        } catch (...) {
            return error_json("dsp internal error");
        }
    }
};

} // namespace

namespace mathsolver::plugins {

std::unique_ptr<Plugin> make_dsp_plugin() {
    return std::make_unique<DspPlugin>();
}

} // namespace mathsolver::plugins
