// DSP filter-design numerics (dsp_design.hpp).
//
// Everything below works on {zeros, poles, gain} in the analog s-plane until
// the bilinear transform, then groups the digital poles/zeros into biquads.
// The transform formulas are the standard ones (identical to scipy's
// lp2lp_zpk / lp2hp_zpk / lp2bp_zpk / lp2bs_zpk / bilinear_zpk).

#include "dsp_design.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <format>
#include <numbers>
#include <optional>

namespace mathsolver::plugins::dsp {

namespace {

using cd = std::complex<double>;

cd prod_neg(const std::vector<cd>& v) {
    cd out{1.0, 0.0};
    for (const cd& x : v) {
        out *= -x;
    }
    return out;
}

// --- normalized analog low-pass prototypes (cutoff 1 rad/s) ----------------

Zpk proto_butter(int n) {
    Zpk s;
    for (int k = 0; k < n; ++k) {
        const double theta = std::numbers::pi * (2.0 * k + 1.0) / (2.0 * n);
        s.p.push_back(cd{-std::sin(theta), std::cos(theta)});
    }
    s.k = prod_neg(s.p).real(); // == 1, spelled out for symmetry
    return s;
}

Zpk proto_cheby1(int n, double ripple_db) {
    const double eps = std::sqrt(std::pow(10.0, ripple_db / 10.0) - 1.0);
    const double mu = std::asinh(1.0 / eps) / n;
    Zpk s;
    for (int k = 0; k < n; ++k) {
        const double theta = std::numbers::pi * (2.0 * k + 1.0) / (2.0 * n);
        s.p.push_back(cd{-std::sinh(mu) * std::sin(theta),
                         std::cosh(mu) * std::cos(theta)});
    }
    s.k = prod_neg(s.p).real();
    if (n % 2 == 0) {
        s.k /= std::sqrt(1.0 + eps * eps); // even orders sit -rp at DC
    }
    return s;
}

Zpk proto_cheby2(int n, double atten_db) {
    const double eps = 1.0 / std::sqrt(std::pow(10.0, atten_db / 10.0) - 1.0);
    const double mu = std::asinh(1.0 / eps) / n;
    Zpk s;
    for (int k = 0; k < n; ++k) {
        const double theta = std::numbers::pi * (2.0 * k + 1.0) / (2.0 * n);
        // Poles: reciprocals of Chebyshev-I-style poles.
        s.p.push_back(1.0 / cd{-std::sinh(mu) * std::sin(theta),
                               std::cosh(mu) * std::cos(theta)});
        // Zeros: on the imaginary axis at j / cos(theta); the middle theta of
        // an odd order (cos = 0) is a zero at infinity and is skipped.
        if (std::abs(std::cos(theta)) > 1e-12) {
            s.z.push_back(cd{0.0, 1.0 / std::cos(theta)});
        }
    }
    s.k = (prod_neg(s.p) / prod_neg(s.z)).real(); // H(0) = 1
    return s;
}

// --- Jacobi elliptic machinery (Orfanidis' Landen-recursion formulation) ----
//
// Arguments of cd/sn below are NORMALIZED: the true argument is u * K(k).
// The descending Landen sequence k -> k_1 -> ... -> k_M -> ~0 turns the
// functions into cos/sin at the last level plus M ascending Gauss transforms.

std::vector<double> landen_seq(double k) {
    std::vector<double> seq;
    while (k > 1e-14 && seq.size() < 32) {
        const double kp = std::sqrt(1.0 - k * k);
        k = (1.0 - kp) / (1.0 + kp);
        seq.push_back(k);
    }
    return seq;
}

cd jacobi_cd(cd u, const std::vector<double>& seq) {
    cd w = std::cos(u * (std::numbers::pi / 2.0));
    for (auto it = seq.rbegin(); it != seq.rend(); ++it) {
        w = (1.0 + *it) * w / (1.0 + *it * w * w);
    }
    return w;
}

cd jacobi_sn(cd u, const std::vector<double>& seq) {
    cd w = std::sin(u * (std::numbers::pi / 2.0));
    for (auto it = seq.rbegin(); it != seq.rend(); ++it) {
        w = (1.0 + *it) * w / (1.0 + *it * w * w);
    }
    return w;
}

/// Inverse sn with normalized result: sn(asn(w) * K, k) = w.
cd jacobi_asn(cd w, double k) {
    const std::vector<double> seq = landen_seq(k);
    double prev = k;
    for (const double ki : seq) {
        w = 2.0 * w / ((1.0 + ki) * (1.0 + std::sqrt(1.0 - prev * prev * w * w)));
        prev = ki;
    }
    return (2.0 / std::numbers::pi) * std::asin(w);
}

/// Solve the elliptic degree equation for the selectivity k given the order
/// and the ripple ratio k1 = eps_p / eps_s (Orfanidis eq. 47).
double ellip_degree(int n, double k1) {
    const double k1p = std::sqrt(1.0 - k1 * k1);
    const std::vector<double> seq = landen_seq(k1p);
    double kp = std::pow(k1p, n);
    for (int i = 1; i <= n / 2; ++i) {
        const double ui = (2.0 * i - 1.0) / n;
        kp *= std::pow(jacobi_sn(cd{ui, 0.0}, seq).real(), 4);
    }
    return std::sqrt(1.0 - kp * kp);
}

/// Elliptic (Cauer) low-pass prototype, passband edge 1 rad/s: -ripple_db at
/// the passband edge, equiripple in both bands, first touching -atten_db at
/// the stopband edge 1/k rad/s.
Zpk proto_elliptic(int n, double ripple_db, double atten_db) {
    const double ep = std::sqrt(std::pow(10.0, ripple_db / 10.0) - 1.0);
    const double es = std::sqrt(std::pow(10.0, atten_db / 10.0) - 1.0);
    const double k1 = ep / es;
    const double k = ellip_degree(n, k1);
    const std::vector<double> seq = landen_seq(k);

    // Pole offset: v0 = -j asn(j / ep, k1) / n.
    const cd v0c = jacobi_asn(cd{0.0, 1.0 / ep}, k1) / static_cast<double>(n);
    const double v0 = v0c.imag();

    Zpk s;
    for (int i = 1; i <= n / 2; ++i) {
        const double ui = (2.0 * i - 1.0) / n;
        // Zeros at j / (k cd(u_i)) and conjugate.
        const double zeta = jacobi_cd(cd{ui, 0.0}, seq).real();
        s.z.push_back(cd{0.0, 1.0 / (k * zeta)});
        s.z.push_back(cd{0.0, -1.0 / (k * zeta)});
        // Poles at j cd((u_i - j v0)) and conjugate.
        const cd p = cd{0.0, 1.0} * jacobi_cd(cd{ui, -v0}, seq);
        s.p.push_back(p);
        s.p.push_back(std::conj(p));
    }
    if (n % 2 == 1) {
        s.p.push_back(cd{0.0, 1.0} * jacobi_sn(cd{0.0, v0}, seq)); // real, < 0
    }
    const double h0 = n % 2 == 1 ? 1.0 : std::pow(10.0, -ripple_db / 20.0);
    s.k = h0 * (prod_neg(s.p) / prod_neg(s.z)).real();
    return s;
}

// --- analog frequency transforms -------------------------------------------

Zpk lp2lp(const Zpk& s, double wo) {
    Zpk out;
    for (const cd& z : s.z) out.z.push_back(z * wo);
    for (const cd& p : s.p) out.p.push_back(p * wo);
    out.k = s.k * std::pow(wo, static_cast<double>(s.p.size() - s.z.size()));
    return out;
}

Zpk lp2hp(const Zpk& s, double wo) {
    const std::size_t degree = s.p.size() - s.z.size();
    Zpk out;
    out.k = s.k * (prod_neg(s.z) / prod_neg(s.p)).real();
    for (const cd& z : s.z) out.z.push_back(wo / z);
    for (const cd& p : s.p) out.p.push_back(wo / p);
    out.z.insert(out.z.end(), degree, cd{0.0, 0.0});
    return out;
}

Zpk lp2bp(const Zpk& s, double wo, double bw) {
    const std::size_t degree = s.p.size() - s.z.size();
    Zpk out;
    const auto split = [&](const cd& x, std::vector<cd>& dst) {
        const cd xl = x * (bw / 2.0);
        const cd d = std::sqrt(xl * xl - wo * wo);
        dst.push_back(xl + d);
        dst.push_back(xl - d);
    };
    for (const cd& z : s.z) split(z, out.z);
    for (const cd& p : s.p) split(p, out.p);
    out.z.insert(out.z.end(), degree, cd{0.0, 0.0});
    out.k = s.k * std::pow(bw, static_cast<double>(degree));
    return out;
}

Zpk lp2bs(const Zpk& s, double wo, double bw) {
    const std::size_t degree = s.p.size() - s.z.size();
    Zpk out;
    out.k = s.k * (prod_neg(s.z) / prod_neg(s.p)).real();
    const auto split = [&](const cd& x, std::vector<cd>& dst) {
        const cd xh = (bw / 2.0) / x;
        const cd d = std::sqrt(xh * xh - wo * wo);
        dst.push_back(xh + d);
        dst.push_back(xh - d);
    };
    for (const cd& z : s.z) split(z, out.z);
    for (const cd& p : s.p) split(p, out.p);
    for (std::size_t i = 0; i < degree; ++i) {
        out.z.push_back(cd{0.0, wo});
        out.z.push_back(cd{0.0, -wo});
    }
    return out;
}

// --- bilinear transform (s -> z) -------------------------------------------

Zpk bilinear(const Zpk& s, double fs) {
    const double K = 2.0 * fs;
    const std::size_t degree = s.p.size() - s.z.size();
    Zpk out;
    cd num{1.0, 0.0};
    cd den{1.0, 0.0};
    for (const cd& z : s.z) num *= (K - z);
    for (const cd& p : s.p) den *= (K - p);
    out.k = s.k * (num / den).real();
    for (const cd& z : s.z) out.z.push_back((K + z) / (K - z));
    for (const cd& p : s.p) out.p.push_back((K + p) / (K - p));
    out.z.insert(out.z.end(), degree, cd{-1.0, 0.0}); // zeros at infinity -> -1
    return out;
}

// --- biquad sectioning ------------------------------------------------------

constexpr double k_imag_tol = 1e-10;

bool is_real(const cd& x) {
    return std::abs(x.imag()) <= k_imag_tol * (1.0 + std::abs(x));
}

/// Split a conjugate-symmetric set into upper-half-plane representatives and
/// reals (each conjugate pair contributes one entry to `pairs`).
void split_conjugates(const std::vector<cd>& v, std::vector<cd>& pairs,
                      std::vector<double>& reals) {
    for (const cd& x : v) {
        if (is_real(x)) {
            reals.push_back(x.real());
        } else if (x.imag() > 0.0) {
            pairs.push_back(x);
        }
    }
}

struct SectionRoots {
    // 0, 1, or 2 roots as polynomial coefficients [1, c1, c2] (c2 = 0 when
    // fewer than 2 roots; c1 = c2 = 0 when none).
    double c1 = 0.0, c2 = 0.0;
    int count = 0;
};

SectionRoots from_pair(const cd& x) {
    return {-2.0 * x.real(), std::norm(x), 2};
}

SectionRoots from_reals(double a, double b) {
    return {-(a + b), a * b, 2};
}

SectionRoots from_real(double a) {
    return {-a, 0.0, 1};
}

/// Group digital poles/zeros into biquads. Pole units are formed first
/// (conjugate pairs, then reals two at a time); each takes the nearest
/// available zeros of matching count. The overall gain lands in the first
/// section's numerator. Sections are ordered least-peaked first.
std::vector<Biquad> zpk_to_sos(const Zpk& s) {
    std::vector<cd> pole_pairs;
    std::vector<double> pole_reals;
    split_conjugates(s.p, pole_pairs, pole_reals);
    std::vector<cd> zero_pairs;
    std::vector<double> zero_reals;
    split_conjugates(s.z, zero_pairs, zero_reals);

    // Most-peaked pole units (largest |p|) pick their zeros first.
    std::sort(pole_pairs.begin(), pole_pairs.end(),
              [](const cd& a, const cd& b) { return std::abs(a) > std::abs(b); });
    std::sort(pole_reals.begin(), pole_reals.end(),
              [](double a, double b) { return std::abs(a) > std::abs(b); });

    struct Unit {
        SectionRoots den;
        cd rep;      // representative pole location for zero matching
        bool single; // one-pole section
    };
    std::vector<Unit> units;
    for (const cd& p : pole_pairs) {
        units.push_back({from_pair(p), p, false});
    }
    for (std::size_t i = 0; i + 1 < pole_reals.size(); i += 2) {
        units.push_back({from_reals(pole_reals[i], pole_reals[i + 1]),
                         cd{pole_reals[i], 0.0}, false});
    }
    if (pole_reals.size() % 2 == 1) {
        const double r = pole_reals.back();
        units.push_back({from_real(r), cd{r, 0.0}, true});
    }

    const auto take_nearest_zero_pair = [&](const cd& rep) -> SectionRoots {
        std::size_t best = zero_pairs.size();
        double best_d = 0.0;
        for (std::size_t i = 0; i < zero_pairs.size(); ++i) {
            const double d = std::abs(zero_pairs[i] - rep);
            if (best == zero_pairs.size() || d < best_d) {
                best = i;
                best_d = d;
            }
        }
        if (best == zero_pairs.size()) {
            return {};
        }
        const SectionRoots out = from_pair(zero_pairs[best]);
        zero_pairs.erase(zero_pairs.begin() + static_cast<std::ptrdiff_t>(best));
        return out;
    };
    const auto take_nearest_real_zero = [&](const cd& rep) -> std::optional<double> {
        std::size_t best = zero_reals.size();
        double best_d = 0.0;
        for (std::size_t i = 0; i < zero_reals.size(); ++i) {
            const double d = std::abs(cd{zero_reals[i], 0.0} - rep);
            if (best == zero_reals.size() || d < best_d) {
                best = i;
                best_d = d;
            }
        }
        if (best == zero_reals.size()) {
            return std::nullopt;
        }
        const double out = zero_reals[best];
        zero_reals.erase(zero_reals.begin() + static_cast<std::ptrdiff_t>(best));
        return out;
    };

    std::vector<Biquad> sections;
    for (const Unit& u : units) {
        SectionRoots num;
        if (u.single) {
            if (const auto r = take_nearest_real_zero(u.rep)) {
                num = from_real(*r);
            }
        } else {
            // Prefer a conjugate zero pair; fall back to (up to) two reals.
            if (!zero_pairs.empty()) {
                num = take_nearest_zero_pair(u.rep);
            } else if (const auto r1 = take_nearest_real_zero(u.rep)) {
                const auto r2 = take_nearest_real_zero(u.rep);
                num = r2 ? from_reals(*r1, *r2) : from_real(*r1);
            }
        }
        sections.push_back(Biquad{1.0, num.c1, num.c2, u.den.c1, u.den.c2});
    }

    // Least-peaked first for a well-scaled cascade; gain into section 1.
    std::reverse(sections.begin(), sections.end());
    if (!sections.empty()) {
        sections.front().b0 *= s.k;
        sections.front().b1 *= s.k;
        sections.front().b2 *= s.k;
    }
    return sections;
}

double prewarp(double f, double fs) {
    return 2.0 * fs * std::tan(std::numbers::pi * f / fs);
}

} // namespace

Zpk bilinear_zpk(const Zpk& analog, double fs) {
    return bilinear(analog, fs);
}

std::vector<Biquad> zpk_to_biquads(const Zpk& digital) {
    return zpk_to_sos(digital);
}

std::vector<Biquad> design_iir(const DesignSpec& spec) {
    if (spec.order < 1) {
        throw DesignError("order must be at least 1");
    }
    if (!(spec.fs > 0.0)) {
        throw DesignError("sample rate must be positive");
    }

    Zpk proto;
    switch (spec.family) {
        case Family::Butterworth: proto = proto_butter(spec.order); break;
        case Family::Cheby1: proto = proto_cheby1(spec.order, spec.ripple_db); break;
        case Family::Cheby2: proto = proto_cheby2(spec.order, spec.atten_db); break;
        case Family::Elliptic:
            if (!(spec.atten_db > spec.ripple_db)) {
                throw DesignError(
                    "stopband attenuation must exceed the passband ripple");
            }
            proto = proto_elliptic(spec.order, spec.ripple_db, spec.atten_db);
            break;
    }

    Zpk analog;
    if (spec.kind == Kind::Lowpass || spec.kind == Kind::Highpass) {
        if (!(spec.f1 > 0.0 && spec.f1 < spec.fs / 2.0)) {
            throw DesignError(std::format(
                "cutoff must lie in (0, fs/2): fc = {} Hz, fs/2 = {} Hz", spec.f1,
                spec.fs / 2.0));
        }
        const double wo = prewarp(spec.f1, spec.fs);
        analog = spec.kind == Kind::Lowpass ? lp2lp(proto, wo) : lp2hp(proto, wo);
    } else {
        if (!(spec.f1 > 0.0 && spec.f2 > spec.f1 && spec.f2 < spec.fs / 2.0)) {
            throw DesignError(std::format(
                "band edges must satisfy 0 < f1 < f2 < fs/2: f1 = {} Hz, f2 = {} "
                "Hz, fs/2 = {} Hz",
                spec.f1, spec.f2, spec.fs / 2.0));
        }
        const double w1 = prewarp(spec.f1, spec.fs);
        const double w2 = prewarp(spec.f2, spec.fs);
        const double wo = std::sqrt(w1 * w2);
        const double bw = w2 - w1;
        analog = spec.kind == Kind::Bandpass ? lp2bp(proto, wo, bw)
                                             : lp2bs(proto, wo, bw);
    }

    return zpk_to_sos(bilinear(analog, spec.fs));
}

std::vector<Biquad> design_butter(bool highpass, int order, double fc, double fs) {
    DesignSpec spec;
    spec.family = Family::Butterworth;
    spec.kind = highpass ? Kind::Highpass : Kind::Lowpass;
    spec.order = order;
    spec.f1 = fc;
    spec.fs = fs;
    return design_iir(spec);
}

ResponsePoint response_at(const std::vector<Biquad>& sections, double f, double fs) {
    const double w = 2.0 * std::numbers::pi * f / fs;
    const cd z1 = std::polar(1.0, -w);
    const cd z2 = std::polar(1.0, -2.0 * w);
    cd h{1.0, 0.0};
    double phase = 0.0;
    double gd = 0.0;
    for (const Biquad& s : sections) {
        const cd num = s.b0 + s.b1 * z1 + s.b2 * z2;
        const cd den = 1.0 + s.a1 * z1 + s.a2 * z2;
        h *= num / den;
        phase += std::arg(num) - std::arg(den);
        // Group delay -d(arg)/dw per section: Re(N'/N) - Re(D'/D) with
        // N' = sum k b_k e^{-jkw} (sign folded in).
        const cd dnum = s.b1 * z1 + 2.0 * s.b2 * z2;
        const cd dden = s.a1 * z1 + 2.0 * s.a2 * z2;
        gd += (dnum / num).real() - (dden / den).real();
    }
    return ResponsePoint{20.0 * std::log10(std::abs(h)), phase, gd};
}

double magnitude_db(const std::vector<Biquad>& sections, double f, double fs) {
    return response_at(sections, f, fs).mag_db;
}

// --- FIR (windowed sinc) ----------------------------------------------------

namespace {

double sinc(double x) {
    if (std::abs(x) < 1e-12) {
        return 1.0;
    }
    return std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
}

/// Modified Bessel I0 by power series (converges fast for the beta range).
double bessel_i0(double x) {
    double sum = 1.0;
    double term = 1.0;
    for (int m = 1; m < 64; ++m) {
        term *= (x / 2.0) * (x / 2.0) / (m * static_cast<double>(m));
        sum += term;
        if (term < 1e-18 * sum) {
            break;
        }
    }
    return sum;
}

double window_at(FirWindow w, double beta, int n, int taps) {
    const double t = static_cast<double>(n) / (taps - 1); // 0..1
    switch (w) {
        case FirWindow::Rect: return 1.0;
        case FirWindow::Hann:
            return 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * t);
        case FirWindow::Hamming:
            return 0.54 - 0.46 * std::cos(2.0 * std::numbers::pi * t);
        case FirWindow::Blackman:
            return 0.42 - 0.5 * std::cos(2.0 * std::numbers::pi * t) +
                   0.08 * std::cos(4.0 * std::numbers::pi * t);
        case FirWindow::Kaiser: {
            const double m = (taps - 1) / 2.0;
            const double r = (n - m) / m;
            return bessel_i0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) /
                   bessel_i0(beta);
        }
    }
    return 1.0;
}

/// Windowed ideal low-pass taps (gain not yet normalized).
std::vector<double> raw_lowpass(int taps, double fc, double fs, FirWindow w,
                                double beta) {
    const double m = (taps - 1) / 2.0;
    std::vector<double> h(static_cast<std::size_t>(taps));
    for (int n = 0; n < taps; ++n) {
        h[static_cast<std::size_t>(n)] = 2.0 * fc / fs * sinc(2.0 * fc / fs * (n - m)) *
                                         window_at(w, beta, n, taps);
    }
    return h;
}

/// Real linear-phase amplitude at f (the response with the e^{-jwM} delay
/// factored out): A(f) = sum h[n] cos(w (n - M)).
double fir_amplitude(const std::vector<double>& h, double f, double fs) {
    const double w = 2.0 * std::numbers::pi * f / fs;
    const double m = (static_cast<double>(h.size()) - 1.0) / 2.0;
    double a = 0.0;
    for (std::size_t n = 0; n < h.size(); ++n) {
        a += h[n] * std::cos(w * (static_cast<double>(n) - m));
    }
    return a;
}

} // namespace

std::vector<double> design_fir(const FirSpec& spec) {
    if (spec.taps < 5 || spec.taps > 255) {
        throw DesignError("taps must be in [5, 255]");
    }
    if (!(spec.fs > 0.0)) {
        throw DesignError("sample rate must be positive");
    }
    const bool band = spec.kind == Kind::Bandpass || spec.kind == Kind::Bandstop;
    if (!(spec.f1 > 0.0 && spec.f1 < spec.fs / 2.0) ||
        (band && !(spec.f2 > spec.f1 && spec.f2 < spec.fs / 2.0))) {
        throw DesignError(
            band ? "band edges must satisfy 0 < f1 < f2 < fs/2"
                 : "cutoff must lie in (0, fs/2)");
    }
    if ((spec.kind == Kind::Highpass || spec.kind == Kind::Bandstop) &&
        spec.taps % 2 == 0) {
        throw DesignError(
            "high-pass and band-stop FIR filters need an odd tap count "
            "(a type-I linear-phase filter)");
    }

    const int taps = spec.taps;
    const std::size_t mid = static_cast<std::size_t>((taps - 1) / 2);
    std::vector<double> h;
    switch (spec.kind) {
        case Kind::Lowpass:
            h = raw_lowpass(taps, spec.f1, spec.fs, spec.window, spec.kaiser_beta);
            break;
        case Kind::Highpass: {
            // Spectral inversion of the complementary low-pass: delta - h_lp.
            h = raw_lowpass(taps, spec.f1, spec.fs, spec.window, spec.kaiser_beta);
            for (double& v : h) v = -v;
            h[mid] += 1.0;
            break;
        }
        case Kind::Bandpass: {
            h = raw_lowpass(taps, spec.f2, spec.fs, spec.window, spec.kaiser_beta);
            const std::vector<double> lo =
                raw_lowpass(taps, spec.f1, spec.fs, spec.window, spec.kaiser_beta);
            for (std::size_t n = 0; n < h.size(); ++n) h[n] -= lo[n];
            break;
        }
        case Kind::Bandstop: {
            // delta - bandpass = lowpass(f1) + spectrally-inverted lowpass(f2).
            h = raw_lowpass(taps, spec.f1, spec.fs, spec.window, spec.kaiser_beta);
            const std::vector<double> hi =
                raw_lowpass(taps, spec.f2, spec.fs, spec.window, spec.kaiser_beta);
            for (std::size_t n = 0; n < h.size(); ++n) h[n] -= hi[n];
            h[mid] += 1.0;
            break;
        }
    }

    // Normalize to exactly unity at the band's reference frequency.
    double ref = 0.0;
    switch (spec.kind) {
        case Kind::Lowpass:
        case Kind::Bandstop: ref = 0.0; break;
        case Kind::Highpass: ref = spec.fs / 2.0; break;
        case Kind::Bandpass: ref = (spec.f1 + spec.f2) / 2.0; break;
    }
    const double a = fir_amplitude(h, ref, spec.fs);
    if (std::abs(a) > 1e-12) {
        for (double& v : h) v /= a;
    }
    return h;
}

ResponsePoint fir_response_at(const std::vector<double>& taps, double f, double fs) {
    const double w = 2.0 * std::numbers::pi * f / fs;
    cd h{0.0, 0.0};
    cd dh{0.0, 0.0}; // sum n h[n] e^{-jwn}
    for (std::size_t n = 0; n < taps.size(); ++n) {
        const cd e = std::polar(1.0, -w * static_cast<double>(n));
        h += taps[n] * e;
        dh += static_cast<double>(n) * taps[n] * e;
    }
    double phase = std::arg(h);
    return ResponsePoint{20.0 * std::log10(std::abs(h)), phase, (dh / h).real()};
}

// --- time responses ---------------------------------------------------------

std::vector<double> impulse_response(const std::vector<Biquad>& sections, int n) {
    std::vector<double> out(static_cast<std::size_t>(std::max(n, 0)), 0.0);
    // Direct-form II transposed state per section.
    std::vector<std::pair<double, double>> st(sections.size(), {0.0, 0.0});
    for (int i = 0; i < n; ++i) {
        double x = i == 0 ? 1.0 : 0.0;
        for (std::size_t s = 0; s < sections.size(); ++s) {
            const Biquad& q = sections[s];
            auto& [z1, z2] = st[s];
            const double y = q.b0 * x + z1;
            z1 = q.b1 * x - q.a1 * y + z2;
            z2 = q.b2 * x - q.a2 * y;
            x = y;
        }
        out[static_cast<std::size_t>(i)] = x;
    }
    return out;
}

} // namespace mathsolver::plugins::dsp
