#include "mathsolver/discriminant.hpp"

#include <format>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "mathsolver/parser.hpp"
#include "mathsolver/printer.hpp"
#include "mathsolver/rational.hpp"
#include "mathsolver/simplify.hpp"

namespace mathsolver {

namespace {

/// Root nature from the discriminant's sign, per degree.
std::string classify(int degree, const Rational& disc) {
    const int sign = disc.is_zero() ? 0 : (disc.is_negative() ? -1 : 1);
    if (degree == 2) {
        return sign > 0   ? "two distinct real roots"
               : sign == 0 ? "one repeated real root"
                           : "two complex-conjugate roots";
    }
    if (degree == 3) {
        return sign > 0   ? "three distinct real roots"
               : sign == 0 ? "a repeated root"
                           : "one real root and two complex-conjugate roots";
    }
    // Degree 4: the sign is less decisive than for 2 and 3.
    return sign > 0   ? "either four distinct real roots or two conjugate pairs"
           : sign == 0 ? "a repeated root"
                       : "two distinct real roots and one complex-conjugate pair";
}

} // namespace

DiscriminantResult discriminant(const Expr& f, std::string_view var) {
    DiscriminantResult r;
    const auto coeffs = polynomial_coefficients(simplify(f), var);
    if (!coeffs) {
        r.status = DiscriminantResult::Status::NotPolynomial;
        r.message = std::format("discriminant: not a polynomial in {}", var);
        return r;
    }
    const int n = static_cast<int>(coeffs->size()) - 1;
    r.degree = n;
    if (n < 1) {
        r.status = DiscriminantResult::Status::DegreeTooLow;
        r.message = "discriminant: needs a polynomial of degree >= 1";
        return r;
    }
    if (n == 1) { // deg-1 discriminant is 1 by convention
        r.status = DiscriminantResult::Status::Ok;
        r.value = make_num(1);
        return r;
    }
    if (n > 4) {
        r.status = DiscriminantResult::Status::DegreeUnsupported;
        r.message = "discriminant: supported for degree 2–4";
        return r;
    }

    // Each coefficient is parenthesized so it can be dropped into the formula.
    auto C = [&](int i) { return "(" + to_string((*coeffs)[i], PrintStyle::Plain) + ")"; };
    std::vector<std::string> terms;
    auto term = [&](const char* coeff,
                    std::initializer_list<std::pair<std::string, int>> factors) {
        std::string t = coeff;
        for (const auto& [base, power] : factors) {
            t += "*" + base;
            if (power > 1) t += "^" + std::to_string(power);
        }
        terms.push_back(t);
    };

    if (n == 2) {
        const std::string a = C(2), b = C(1), c = C(0);
        term("1", {{b, 2}});
        term("-4", {{a, 1}, {c, 1}});
    } else if (n == 3) {
        const std::string a = C(3), b = C(2), c = C(1), d = C(0);
        term("18", {{a, 1}, {b, 1}, {c, 1}, {d, 1}});
        term("-4", {{b, 3}, {d, 1}});
        term("1", {{b, 2}, {c, 2}});
        term("-4", {{a, 1}, {c, 3}});
        term("-27", {{a, 2}, {d, 2}});
    } else { // n == 4
        const std::string a = C(4), b = C(3), c = C(2), d = C(1), e = C(0);
        term("256", {{a, 3}, {e, 3}});
        term("-192", {{a, 2}, {b, 1}, {d, 1}, {e, 2}});
        term("-128", {{a, 2}, {c, 2}, {e, 2}});
        term("144", {{a, 2}, {c, 1}, {d, 2}, {e, 1}});
        term("-27", {{a, 2}, {d, 4}});
        term("144", {{a, 1}, {b, 2}, {c, 1}, {e, 2}});
        term("-6", {{a, 1}, {b, 2}, {d, 2}, {e, 1}});
        term("-80", {{a, 1}, {b, 1}, {c, 2}, {d, 1}, {e, 1}});
        term("18", {{a, 1}, {b, 1}, {c, 1}, {d, 3}});
        term("16", {{a, 1}, {c, 4}, {e, 1}});
        term("-4", {{a, 1}, {c, 3}, {d, 2}});
        term("-27", {{b, 4}, {e, 2}});
        term("18", {{b, 3}, {c, 1}, {d, 1}, {e, 1}});
        term("-4", {{b, 3}, {d, 3}});
        term("-4", {{b, 2}, {c, 3}, {e, 1}});
        term("1", {{b, 2}, {c, 2}, {d, 2}});
    }

    // Join sign-aware so a leading '-' becomes a subtraction.
    std::string formula;
    for (std::size_t i = 0; i < terms.size(); ++i) {
        const std::string& t = terms[i];
        if (i == 0) {
            formula = t;
        } else if (!t.empty() && t[0] == '-') {
            formula += " - " + t.substr(1);
        } else {
            formula += " + " + t;
        }
    }

    r.value = simplify(parse_expression(formula));
    r.status = DiscriminantResult::Status::Ok;
    if (r.value->kind() == Kind::Number) {
        r.root_nature = classify(n, r.value->number());
    }
    return r;
}

} // namespace mathsolver
