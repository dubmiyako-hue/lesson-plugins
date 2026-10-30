#include <lesson/Format.h>
#include <charconv>
#include <cmath>
#include <system_error>

namespace lesson {

std::string toFixed(double v, int digits) {
    // ECMA-262 Number.prototype.toFixed: the n with n / 10^digits closest to the exact value, the larger n on a tie. The
    // exact value printed to digits + 40 places decides: a double near a tie of 10^-digits differs from it by far more
    // than 10^-(digits + 40), so the first dropped digit is 5 or more exactly when the value is at or past the tie.
    // NaN, the infinities and 1e21 and above are String(v) in JavaScript; -0 has no sign there.
    if (std::isnan(v)) return "NaN";
    if (std::isinf(v)) return v > 0.0 ? "Infinity" : "-Infinity";
    if (std::fabs(v) >= 1e21) {
        char shortest[64];
        const auto printed = std::to_chars(shortest, shortest + sizeof shortest, v);
        return std::string(shortest, printed.ptr);
    }
    const bool negative = v < 0.0;
    const double x = std::fabs(v);
    char buffer[512];
    const auto result = std::to_chars(buffer, buffer + sizeof buffer, x, std::chars_format::fixed, digits + 40);
    if (result.ec != std::errc {}) return {};
    const std::string printed(buffer, result.ptr);
    const auto dot = printed.find('.');
    std::string kept = printed.substr(0, dot) + printed.substr(dot + 1, static_cast<std::size_t>(digits));
    if (printed[dot + 1 + static_cast<std::size_t>(digits)] >= '5') {
        auto i = kept.size();
        while (i > 0) {
            --i;
            if (kept[i] != '9') {
                ++kept[i];
                break;
            }
            kept[i] = '0';
            if (i == 0) kept.insert(kept.begin(), '1');
        }
    }
    std::string text = negative ? "-" : "";
    const auto integerDigits = kept.size() - static_cast<std::size_t>(digits);
    text += kept.substr(0, integerDigits);
    if (digits > 0) {
        text += '.';
        text += kept.substr(integerDigits);
    }
    return text;
}

double jsRound(double v) noexcept {
    // The integer closest to v, the one toward +inf on a tie. v - floor(v) is exact here, unlike v + 0.5.
    if (!std::isfinite(v)) return v;
    double r = std::floor(v);
    if (v - r >= 0.5) r += 1.0;
    return r;
}

namespace {
std::string integerText(double v) {
    // String(Math.round(v)) for the values sig3 sees (below 10^15).
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof buffer, static_cast<long long>(jsRound(v)));
    return std::string(buffer, result.ptr);
}
// Three significant digits (params.ts sig3).
std::string sig3(double v) {
    if (v >= 99.95) return integerText(v);
    if (v >= 9.995) return toFixed(v, 1);
    return toFixed(v, 2);
}
}

std::string fmtDb(double v) {
    const auto t = toFixed(std::fabs(v), 1);
    const char* sign = t == "0.0" ? "" : v > 0.0 ? "+" : "-";
    return sign + t + " dB";
}

std::string fmtHz(double v) {
    return v >= 999.5 ? sig3(v / 1000.0) + " kHz" : sig3(v) + " Hz";
}

std::string fmtMs(double v) {
    return v >= 999.5 ? sig3(v / 1000.0) + " s" : sig3(v) + " ms";
}

std::string fmtRatio(double v) {
    return toFixed(v, 1) + ":1";
}

std::string fmtQ(double v) {
    return toFixed(v, 2);
}

std::string formatUnit(Unit unit, double v) {
    switch (unit) {
    case Unit::Db: return fmtDb(v);
    case Unit::Hz: return fmtHz(v);
    case Unit::Ms: return fmtMs(v);
    case Unit::Ratio: return fmtRatio(v);
    case Unit::Q: return fmtQ(v);
    case Unit::None: break;
    }
    return toFixed(v, 2);
}

std::string showValue(const Range& r, Unit unit, double v) {
    if (!r.zero.empty() && v <= 0.0) return std::string(r.zero);
    if (!r.infText.empty() && v >= r.infValue) return std::string(r.infText);
    return formatUnit(unit, v);
}

} // namespace lesson
