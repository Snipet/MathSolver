// Probability-distributions plugin (docs/PLUGINS.md): PMFs / PDFs, CDFs, and
// the inverse normal for the distributions taught in a first statistics course.
// Each command reports the queried value and plots the distribution, with a
// marker at the query point.
//
//   prob.normalpdf x[, mu, sigma]      prob.normalcdf x[, mu, sigma]  (P(X<=x))
//   prob.invnorm p[, mu, sigma]        (quantile: the x with P(X<=x)=p)
//   prob.binompdf n, p, k              prob.binomcdf n, p, k
//   prob.poissonpdf lambda, k          prob.poissoncdf lambda, k

#include "prob_core.hpp"

#include "mathsolver/plugin.hpp"

#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace mathsolver::plugins;
namespace pr = mathsolver::plugins::prob;

// --- argument + JSON helpers ------------------------------------------------

double arg_or(const std::vector<std::string>& args, std::size_t i, double fallback) {
    if (i >= args.size()) return fallback;
    const auto v = parse_number(args[i]);
    if (!v) throw std::runtime_error(std::format("'{}' is not a number", args[i]));
    return *v;
}

double req(const std::vector<std::string>& args, std::size_t i, const char* what) {
    if (i >= args.size()) throw std::runtime_error(std::format("missing {}", what));
    return arg_or(args, i, 0.0);
}

int req_int(const std::vector<std::string>& args, std::size_t i, const char* what) {
    const double v = req(args, i, what);
    if (std::fabs(v - std::round(v)) > 1e-9)
        throw std::runtime_error(std::format("{} must be a whole number, got {:.6g}", what, v));
    return static_cast<int>(std::llround(v));
}

std::string num(double v) { return std::format("{:.6g}", v); }

std::string jarr(const std::vector<double>& v) {
    std::string out = "[";
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i > 0) out += ",";
        out += jnum(v[i]);
    }
    return out + "]";
}

std::string kv_block(const std::vector<std::pair<std::string, std::string>>& items) {
    std::string out = "{\"type\":\"kv\",\"items\":[";
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) out += ",";
        out += std::format("[{},{}]", jstr(items[i].first), jstr(items[i].second));
    }
    return out + "]}";
}

// A single-series chart, optionally drawn as markers, with one vertical marker
// line at the query point.
std::string series_block(const std::string& title, const std::string& xlabel,
                         const std::string& ylabel, const std::vector<double>& x,
                         const std::string& label, const std::vector<double>& ys, bool points,
                         double vline_x, const std::string& vline_label) {
    return std::format(
        "{{\"type\":\"series\",\"title\":{},\"xlabel\":{},\"ylabel\":{},\"x\":{},"
        "\"series\":[{{\"label\":{},\"ys\":{}{}}}],"
        "\"vlines\":[{{\"x\":{},\"label\":{}}}]}}",
        jstr(title), jstr(xlabel), jstr(ylabel), jarr(x), jstr(label), jarr(ys),
        points ? ",\"points\":true" : "", jnum(vline_x), jstr(vline_label));
}

std::string envelope(const std::string& title, const std::vector<std::string>& blocks) {
    std::string b = "[";
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        if (i > 0) b += ",";
        b += blocks[i];
    }
    b += "]";
    return std::format("{{\"ok\":true,\"title\":{},\"blocks\":{}}}", jstr(title), b);
}

// Sampled normal PDF curve over [mu - 4σ, mu + 4σ].
std::string normal_curve(double mu, double sigma, double mark, const std::string& mark_label) {
    constexpr int n = 201;
    const double lo = mu - 4.0 * sigma;
    const double hi = mu + 4.0 * sigma;
    std::vector<double> x(n);
    std::vector<double> y(n);
    for (int i = 0; i < n; ++i) {
        x[i] = lo + (hi - lo) * i / (n - 1);
        y[i] = pr::normal_pdf(x[i], mu, sigma);
    }
    return series_block("Normal density", "x", "f(x)", x, "pdf", y, false, mark, mark_label);
}

// Sampled PMF (stem-style markers) over k = 0..kmax.
std::string pmf_curve(int kmax, const std::string& title, double mark,
                      const std::string& mark_label, const auto& pmf) {
    std::vector<double> ks;
    std::vector<double> ps;
    for (int k = 0; k <= kmax; ++k) {
        ks.push_back(k);
        ps.push_back(pmf(k));
    }
    return series_block(title, "k", "P(X = k)", ks, "pmf", ps, true, mark, mark_label);
}

// --- commands ---------------------------------------------------------------

std::string cmd_normalpdf(const std::vector<std::string>& args) {
    const double x = req(args, 0, "x");
    const double mu = arg_or(args, 1, 0.0);
    const double sigma = arg_or(args, 2, 1.0);
    if (!(sigma > 0.0)) throw std::runtime_error("the standard deviation must be positive");
    const double f = pr::normal_pdf(x, mu, sigma);
    const std::string kv = kv_block({{"distribution", std::format("Normal(mu = {}, sigma = {})", num(mu), num(sigma))},
                                     {"x", num(x)},
                                     {"pdf  f(x)", num(f)}});
    return envelope("normal pdf", {kv, normal_curve(mu, sigma, x, std::format("x = {}", num(x)))});
}

std::string cmd_normalcdf(const std::vector<std::string>& args) {
    const double x = req(args, 0, "x");
    const double mu = arg_or(args, 1, 0.0);
    const double sigma = arg_or(args, 2, 1.0);
    if (!(sigma > 0.0)) throw std::runtime_error("the standard deviation must be positive");
    const double p = pr::normal_cdf(x, mu, sigma);
    const std::string kv = kv_block({{"distribution", std::format("Normal(mu = {}, sigma = {})", num(mu), num(sigma))},
                                     {"x", num(x)},
                                     {"P(X <= x)", num(p)},
                                     {"P(X > x)", num(1.0 - p)}});
    return envelope("normal cdf", {kv, normal_curve(mu, sigma, x, std::format("x = {}", num(x)))});
}

std::string cmd_invnorm(const std::vector<std::string>& args) {
    const double p = req(args, 0, "p");
    const double mu = arg_or(args, 1, 0.0);
    const double sigma = arg_or(args, 2, 1.0);
    if (!(p > 0.0 && p < 1.0)) throw std::runtime_error("the probability p must be strictly between 0 and 1");
    if (!(sigma > 0.0)) throw std::runtime_error("the standard deviation must be positive");
    const double x = pr::inv_norm(p, mu, sigma);
    const std::string kv = kv_block({{"distribution", std::format("Normal(mu = {}, sigma = {})", num(mu), num(sigma))},
                                     {"p", num(p)},
                                     {"x  (P(X <= x) = p)", num(x)}});
    return envelope("inverse normal", {kv, normal_curve(mu, sigma, x, std::format("x = {}", num(x)))});
}

std::string cmd_binompdf(const std::vector<std::string>& args) {
    const int n = req_int(args, 0, "n");
    const double p = req(args, 1, "p");
    const int k = req_int(args, 2, "k");
    if (n < 0) throw std::runtime_error("n must be non-negative");
    if (!(p >= 0.0 && p <= 1.0)) throw std::runtime_error("the success probability p must be in [0, 1]");
    const double f = pr::binom_pmf(n, p, k);
    const std::string kv = kv_block({{"distribution", std::format("Binomial(n = {}, p = {})", n, num(p))},
                                     {"k", std::format("{}", k)},
                                     {"P(X = k)", num(f)}});
    const auto pmf = [&](int kk) { return pr::binom_pmf(n, p, kk); };
    return envelope("binomial pmf", {kv, pmf_curve(n, "Binomial pmf", k, std::format("k = {}", k), pmf)});
}

std::string cmd_binomcdf(const std::vector<std::string>& args) {
    const int n = req_int(args, 0, "n");
    const double p = req(args, 1, "p");
    const int k = req_int(args, 2, "k");
    if (n < 0) throw std::runtime_error("n must be non-negative");
    if (!(p >= 0.0 && p <= 1.0)) throw std::runtime_error("the success probability p must be in [0, 1]");
    const double c = pr::binom_cdf(n, p, k);
    const std::string kv = kv_block({{"distribution", std::format("Binomial(n = {}, p = {})", n, num(p))},
                                     {"k", std::format("{}", k)},
                                     {"P(X <= k)", num(c)},
                                     {"P(X > k)", num(1.0 - c)}});
    const auto pmf = [&](int kk) { return pr::binom_pmf(n, p, kk); };
    return envelope("binomial cdf", {kv, pmf_curve(n, "Binomial pmf", k, std::format("k = {}", k), pmf)});
}

// A sensible upper bound for a Poisson chart: mean + a few standard deviations.
int poisson_kmax(double lambda, int k) {
    const int spread = static_cast<int>(std::ceil(lambda + 4.0 * std::sqrt(lambda) + 5.0));
    int kmax = std::max(spread, k + 1);
    return std::min(kmax, 200);
}

std::string cmd_poissonpdf(const std::vector<std::string>& args) {
    const double lambda = req(args, 0, "lambda");
    const int k = req_int(args, 1, "k");
    if (!(lambda > 0.0)) throw std::runtime_error("the rate lambda must be positive");
    const double f = pr::poisson_pmf(lambda, k);
    const std::string kv = kv_block({{"distribution", std::format("Poisson(lambda = {})", num(lambda))},
                                     {"k", std::format("{}", k)},
                                     {"P(X = k)", num(f)}});
    const auto pmf = [&](int kk) { return pr::poisson_pmf(lambda, kk); };
    return envelope("poisson pmf",
                    {kv, pmf_curve(poisson_kmax(lambda, k), "Poisson pmf", k, std::format("k = {}", k), pmf)});
}

std::string cmd_poissoncdf(const std::vector<std::string>& args) {
    const double lambda = req(args, 0, "lambda");
    const int k = req_int(args, 1, "k");
    if (!(lambda > 0.0)) throw std::runtime_error("the rate lambda must be positive");
    const double c = pr::poisson_cdf(lambda, k);
    const std::string kv = kv_block({{"distribution", std::format("Poisson(lambda = {})", num(lambda))},
                                     {"k", std::format("{}", k)},
                                     {"P(X <= k)", num(c)},
                                     {"P(X > k)", num(1.0 - c)}});
    const auto pmf = [&](int kk) { return pr::poisson_pmf(lambda, kk); };
    return envelope("poisson cdf",
                    {kv, pmf_curve(poisson_kmax(lambda, k), "Poisson pmf", k, std::format("k = {}", k), pmf)});
}

class ProbPlugin final : public Plugin {
  public:
    std::string_view name() const override { return "prob"; }
    std::string_view version() const override { return "0.1.0"; }
    std::string_view summary() const override {
        return "probability distributions: normal (pdf/cdf/inverse), binomial, and "
               "Poisson pmf/cdf, each with the queried value and a plot";
    }
    std::vector<CommandInfo> commands() const override {
        return {
            {"normalpdf", "Normal density f(x)", "prob.normalpdf <x>[, <mu>, <sigma>]",
             "prob.normalpdf 1, 0, 1"},
            {"normalcdf", "Normal lower-tail probability P(X <= x)",
             "prob.normalcdf <x>[, <mu>, <sigma>]", "prob.normalcdf 1.96, 0, 1"},
            {"invnorm", "Inverse normal: the x with P(X <= x) = p",
             "prob.invnorm <p>[, <mu>, <sigma>]", "prob.invnorm 0.975, 0, 1"},
            {"binompdf", "Binomial P(X = k)", "prob.binompdf <n>, <p>, <k>",
             "prob.binompdf 10, 0.5, 4"},
            {"binomcdf", "Binomial P(X <= k)", "prob.binomcdf <n>, <p>, <k>",
             "prob.binomcdf 10, 0.5, 4"},
            {"poissonpdf", "Poisson P(X = k)", "prob.poissonpdf <lambda>, <k>",
             "prob.poissonpdf 3, 2"},
            {"poissoncdf", "Poisson P(X <= k)", "prob.poissoncdf <lambda>, <k>",
             "prob.poissoncdf 3, 2"},
        };
    }
    std::string invoke(std::string_view command,
                       const std::vector<std::string>& args) const override {
        try {
            if (command == "normalpdf") return cmd_normalpdf(args);
            if (command == "normalcdf") return cmd_normalcdf(args);
            if (command == "invnorm") return cmd_invnorm(args);
            if (command == "binompdf") return cmd_binompdf(args);
            if (command == "binomcdf") return cmd_binomcdf(args);
            if (command == "poissonpdf") return cmd_poissonpdf(args);
            if (command == "poissoncdf") return cmd_poissoncdf(args);
            return error_json(std::format("prob has no command '{}'", command));
        } catch (const std::exception& e) {
            return error_json(e.what());
        }
    }
};

} // namespace

namespace mathsolver::plugins {

std::unique_ptr<Plugin> make_prob_plugin() { return std::make_unique<ProbPlugin>(); }

} // namespace mathsolver::plugins
