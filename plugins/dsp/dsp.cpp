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

ResponseGrid sample_response(const std::vector<Biquad>& sections, double fs) {
    ResponseGrid g;
    const double lo = fs / 2.0 * 1e-3;
    const double hi = fs / 2.0 * 0.999;
    double prev_phase = 0.0;
    bool have_prev = false;
    for (int i = 0; i < k_response_points; ++i) {
        const double t = static_cast<double>(i) / (k_response_points - 1);
        const double f = lo * std::pow(hi / lo, t);
        const dsp::ResponsePoint r = dsp::response_at(sections, f, fs);
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

    if (fa.family == Family::Cheby1) {
        const auto rp = parse_double(args[2]);
        if (!rp || !(*rp > 0.0) || *rp > 12.0) {
            return error_json(std::format(
                "passband ripple must be in (0, 12] dB, got '{}'", args[2]));
        }
        spec.ripple_db = *rp;
    } else if (fa.family == Family::Cheby2) {
        const auto rs = parse_double(args[2]);
        if (!rs || *rs < 10.0 || *rs > 120.0) {
            return error_json(std::format(
                "stopband attenuation must be in [10, 120] dB, got '{}'", args[2]));
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
    if (fa.family == Family::Cheby1) {
        kv += std::format(",[\"Passband ripple\",\"{} dB\"]", spec.ripple_db);
    } else if (fa.family == Family::Cheby2) {
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
        "{{\"ok\":true,\"title\":{},\"blocks\":[{},{},{},{},{}]}}",
        jstr(std::format("{} {} — order {}, {} @ {} Hz", family_label(fa.family),
                         kind_label(*kind), *order, title_freqs, spec.fs)),
        kv, sections_table_block(sections),
        series_block("Magnitude response", "gain (dB)", g.freqs, g.mag_db, "|H(f)|",
                     marks),
        series_block("Phase response", "phase (deg)", g.freqs, g.phase_deg,
                     "arg H(f)", marks),
        k_cascade_note);
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
        "{{\"ok\":true,\"title\":{},\"blocks\":[{},{},{},{}]}}",
        jstr(std::format("Frequency response — {} biquad section{} @ {} Hz",
                         sections.size(), sections.size() == 1 ? "" : "s", *fs)),
        sections_table_block(sections),
        series_block("Magnitude response", "gain (dB)", g.freqs, g.mag_db, "|H(f)|",
                     {}),
        series_block("Phase response", "phase (deg)", g.freqs, g.phase_deg,
                     "arg H(f)", {}),
        series_block("Group delay", "delay (samples)", g.freqs, g.group_delay,
                     "tau(f)", {}));
}

class DspPlugin final : public Plugin {
  public:
    std::string_view name() const override { return "dsp"; }
    std::string_view version() const override { return "0.2.0"; }
    std::string_view summary() const override {
        return "DSP filter design: Butterworth/Chebyshev IIR biquad cascades, "
               "frequency/phase/group-delay responses";
    }
    std::vector<CommandInfo> commands() const override {
        return {
            {"butter", "Butterworth low/high/band-pass/band-stop design",
             "dsp.butter <type>, <order 1-12>, <fc>[, <f2>], <fs>   (type: "
             "lowpass|highpass|bandpass|bandstop)"},
            {"cheby1", "Chebyshev I design (equiripple passband)",
             "dsp.cheby1 <type>, <order 1-12>, <ripple dB>, <fc>[, <f2>], <fs>"},
            {"cheby2", "Chebyshev II design (equiripple stopband)",
             "dsp.cheby2 <type>, <order 1-12>, <atten dB>, <fc>[, <f2>], <fs>"},
            {"freqz", "Magnitude/phase/group delay of a biquad cascade",
             "dsp.freqz <fs Hz>, <b0>,<b1>,<b2>,<a1>,<a2> [, ...more groups of 5]"},
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
