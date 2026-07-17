// DSP filter-design plugin (docs/PLUGINS.md).
//
// Native numeric implementations that would be slow or impractical to express
// through the CAS: IIR filter design and frequency-response evaluation. The
// numerics live in dsp_design.{hpp,cpp} (unit-tested directly); this file is
// the command layer — argument parsing and block-based JSON rendering.
//
// Commands:
//   dsp.butter <lowpass|highpass>, <order 1-12>, <fc Hz>, <fs Hz>
//       Butterworth design via the analog prototype + bilinear transform
//       (cutoff prewarped). Result: the biquad cascade (a0 = 1 per section)
//       and the sampled magnitude response.
//   dsp.freqz <fs Hz>, <b0>,<b1>,<b2>,<a1>,<a2> [, ...more groups of 5]
//       Magnitude response of an arbitrary biquad cascade.

#include "dsp_design.hpp"
#include "mathsolver/plugin.hpp"

#include <cctype>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace mathsolver::plugins;
using dsp::Biquad;

constexpr int k_max_order = 12;
constexpr int k_response_points = 240;

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

/// Log-spaced response grid over (0, fs/2), rendered as a "series" block.
std::string response_block(const std::vector<Biquad>& sections, double fs) {
    const double lo = fs / 2.0 * 1e-3;
    const double hi = fs / 2.0 * 0.999;
    std::string xs = "[";
    std::string ys = "[";
    for (int i = 0; i < k_response_points; ++i) {
        const double t = static_cast<double>(i) / (k_response_points - 1);
        const double f = lo * std::pow(hi / lo, t);
        if (i > 0) {
            xs += ",";
            ys += ",";
        }
        xs += jnum(f);
        ys += jnum(dsp::magnitude_db(sections, f, fs)); // non-finite -> null gap
    }
    xs += "]";
    ys += "]";
    return std::format(
        "{{\"type\":\"series\",\"title\":\"Magnitude response\","
        "\"xlabel\":\"frequency (Hz)\",\"ylabel\":\"gain (dB)\",\"logx\":true,"
        "\"x\":{},\"series\":[{{\"label\":\"|H(f)|\",\"ys\":{}}}]}}",
        xs, ys);
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

std::string butter(const std::vector<std::string>& args) {
    if (args.size() != 4) {
        return error_json(
            "usage: dsp.butter <lowpass|highpass>, <order 1-12>, <fc Hz>, <fs Hz>");
    }
    const std::string& type = args[0];
    const bool highpass = type == "highpass" || type == "hp";
    const bool lowpass = type == "lowpass" || type == "lp";
    if (!highpass && !lowpass) {
        return error_json(std::format(
            "unknown filter type '{}': expected lowpass or highpass", type));
    }
    const auto order = parse_int(args[1]);
    if (!order || *order < 1 || *order > k_max_order) {
        return error_json(std::format("order must be an integer in [1, {}], got '{}'",
                                      k_max_order, args[1]));
    }
    const auto fc = parse_double(args[2]);
    const auto fs = parse_double(args[3]);
    if (!fc || !(*fc > 0.0)) {
        return error_json(std::format("cutoff fc must be a positive number, got '{}'",
                                      args[2]));
    }
    if (!fs || !(*fs > 0.0)) {
        return error_json(std::format("sample rate fs must be a positive number, got '{}'",
                                      args[3]));
    }
    if (!(*fc < *fs / 2.0)) {
        return error_json(std::format(
            "cutoff must be below Nyquist: fc = {} Hz, fs/2 = {} Hz", *fc, *fs / 2.0));
    }

    const std::vector<Biquad> sections = dsp::design_butter(highpass, *order, *fc, *fs);
    const double at_fc = dsp::magnitude_db(sections, *fc, *fs);

    const std::string kv = std::format(
        "{{\"type\":\"kv\",\"items\":[[\"Family\",\"Butterworth\"],"
        "[\"Type\",{}],[\"Order\",\"{}\"],[\"Cutoff\",\"{} Hz\"],"
        "[\"Sample rate\",\"{} Hz\"],[\"Gain at cutoff\",\"{:.2f} dB\"]]}}",
        jstr(highpass ? "high-pass" : "low-pass"), *order, *fc, *fs, at_fc);

    const std::string note =
        "{\"type\":\"text\",\"lines\":[\"Cascade the sections in order; each row "
        "is y[n] = b0 x[n] + b1 x[n-1] + b2 x[n-2] - a1 y[n-1] - a2 y[n-2].\"]}";

    return std::format(
        "{{\"ok\":true,\"title\":{},\"blocks\":[{},{},{},{}]}}",
        jstr(std::format("Butterworth {} — order {}, fc {} Hz @ {} Hz",
                         highpass ? "high-pass" : "low-pass", *order, *fc, *fs)),
        kv, sections_table_block(sections), response_block(sections, *fs), note);
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
    return std::format(
        "{{\"ok\":true,\"title\":{},\"blocks\":[{},{}]}}",
        jstr(std::format("Frequency response — {} biquad section{} @ {} Hz",
                         sections.size(), sections.size() == 1 ? "" : "s", *fs)),
        sections_table_block(sections), response_block(sections, *fs));
}

class DspPlugin final : public Plugin {
  public:
    std::string_view name() const override { return "dsp"; }
    std::string_view version() const override { return "0.1.0"; }
    std::string_view summary() const override {
        return "DSP filter design: IIR biquad cascades and frequency responses";
    }
    std::vector<CommandInfo> commands() const override {
        return {
            {"butter", "Design a Butterworth low-/high-pass as a biquad cascade",
             "dsp.butter <lowpass|highpass>, <order 1-12>, <fc Hz>, <fs Hz>"},
            {"freqz", "Magnitude response of a biquad cascade",
             "dsp.freqz <fs Hz>, <b0>,<b1>,<b2>,<a1>,<a2> [, ...more groups of 5]"},
        };
    }
    std::string invoke(std::string_view command,
                       const std::vector<std::string>& args) const override {
        try {
            if (command == "butter") {
                return butter(args);
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
