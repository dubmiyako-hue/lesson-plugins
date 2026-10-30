#include <lesson/Format.h>
#include <lesson/Params.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <system_error>

namespace lesson {

namespace {
constexpr std::string_view kFastest = "最速";

// params.ts BAND_PARAMS and PARAMS.
constexpr Range kFreq { .min = 20.0, .max = 20000.0, .log = true };
constexpr Range kGain { .min = -18.0, .max = 18.0, .step = 0.5 };
constexpr Range kQ { .min = 0.3, .max = 12.0, .log = true };
constexpr Range kThreshold { .min = -60.0, .max = 0.0, .step = 0.5 };
constexpr Range kRatio { .min = 1.0, .max = 20.0, .log = true, .infValue = 1000.0, .infText = "∞:1" }; // RATIO_INF
constexpr Range kAttack { .min = 0.1, .max = 100.0, .log = true, .zero = kFastest };
constexpr Range kRelease { .min = 5.0, .max = 1000.0, .log = true, .zero = kFastest };

constexpr std::array<std::string_view, 4> kSlopeChoices { "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" };

constexpr ParamSpec boolean(std::string_view id, std::string_view name, std::string_view shortName, bool on) {
    return { .id = id, .name = name, .shortName = shortName, .kind = Kind::Bool, .defaultValue = on ? 1.0 : 0.0 };
}
constexpr ParamSpec number(std::string_view id, std::string_view name, std::string_view shortName, Range range, Unit unit,
                           double defaultValue) {
    return { .id = id, .name = name, .shortName = shortName, .kind = Kind::Float, .range = range, .unit = unit,
             .defaultValue = defaultValue };
}
constexpr ParamSpec slope(std::string_view id, std::string_view name, std::string_view shortName, int dbPerOctave) {
    const auto it = std::find(kSlopes.begin(), kSlopes.end(), dbPerOctave);
    return { .id = id, .name = name, .shortName = shortName, .kind = Kind::Choice,
             .defaultValue = static_cast<double>(it - kSlopes.begin()), .choices = kSlopeChoices };
}

std::string_view trim(std::string_view s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}
std::string lowerAscii(std::string_view s) {
    std::string out(s);
    for (auto& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}
bool removeSuffix(std::string& s, std::string_view suffix) {
    if (s.size() < suffix.size() || std::string_view(s).substr(s.size() - suffix.size()) != suffix) return false;
    s.erase(s.size() - suffix.size());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    return true;
}
// Math.max(0, Math.min(1, t)): NaN stays NaN (std::min and std::max would turn it into an end).
double clamp01(double t) noexcept {
    if (std::isnan(t)) return t;
    return t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
}
// A number, inf and -inf included; one too large for a double reads as an infinity, one too small as 0. Not NaN.
std::optional<double> readNumber(std::string_view s) {
    s = trim(s);
    if (!s.empty() && s.front() == '+') s.remove_prefix(1);
    if (s.empty()) return std::nullopt;
    const bool negative = s.front() == '-';
    if (s.substr(negative ? 1 : 0) == "∞") return negative ? -HUGE_VAL : HUGE_VAL;
    double v = 0.0;
    const auto result = std::from_chars(s.data(), s.data() + s.size(), v);
    if (result.ptr != s.data() + s.size()) return std::nullopt;
    if (result.ec == std::errc::result_out_of_range) {
        const bool tiny = s.find("e-") != std::string_view::npos;
        return tiny ? 0.0 : negative ? -HUGE_VAL : HUGE_VAL;
    }
    if (result.ec != std::errc {} || std::isnan(v)) return std::nullopt;
    return v;
}
}

const std::array<ParamSpec, kNumEqParams> kEqParams { {
    boolean("bypass", "Bypass", "Bypass", false),
    boolean("hp_on", "HP On", "HP On", false),
    number("hp_freq", "HP Freq", "HP Freq", kFreq, Unit::Hz, 80.0),
    slope("hp_slope", "HP Slope", "HP Slope", 24),
    boolean("ls_on", "LS On", "LS On", true),
    number("ls_freq", "LS Freq", "LS Freq", kFreq, Unit::Hz, 120.0),
    number("ls_gain", "LS Gain", "LS Gain", kGain, Unit::Db, 0.0),
    boolean("b1_on", "Bell 1 On", "B1 On", true),
    number("b1_freq", "Bell 1 Freq", "B1 Freq", kFreq, Unit::Hz, 300.0),
    number("b1_gain", "Bell 1 Gain", "B1 Gain", kGain, Unit::Db, 0.0),
    number("b1_q", "Bell 1 Q", "B1 Q", kQ, Unit::Q, 1.0),
    boolean("b2_on", "Bell 2 On", "B2 On", true),
    number("b2_freq", "Bell 2 Freq", "B2 Freq", kFreq, Unit::Hz, 1000.0),
    number("b2_gain", "Bell 2 Gain", "B2 Gain", kGain, Unit::Db, 0.0),
    number("b2_q", "Bell 2 Q", "B2 Q", kQ, Unit::Q, 1.0),
    boolean("b3_on", "Bell 3 On", "B3 On", true),
    number("b3_freq", "Bell 3 Freq", "B3 Freq", kFreq, Unit::Hz, 4000.0),
    number("b3_gain", "Bell 3 Gain", "B3 Gain", kGain, Unit::Db, 0.0),
    number("b3_q", "Bell 3 Q", "B3 Q", kQ, Unit::Q, 1.0),
    boolean("hs_on", "HS On", "HS On", true),
    number("hs_freq", "HS Freq", "HS Freq", kFreq, Unit::Hz, 8000.0),
    number("hs_gain", "HS Gain", "HS Gain", kGain, Unit::Db, 0.0),
    boolean("lp_on", "LP On", "LP On", false),
    number("lp_freq", "LP Freq", "LP Freq", kFreq, Unit::Hz, 18000.0),
    slope("lp_slope", "LP Slope", "LP Slope", 12),
    boolean("eq_auto_gain", "Auto Gain", "AutoGain", true),
} };

const std::array<ParamSpec, kNumCompParams> kCompParams { {
    boolean("bypass", "Bypass", "Bypass", false),
    number("comp_threshold", "Threshold", "Thresh", kThreshold, Unit::Db, 0.0),
    number("comp_ratio", "Ratio", "Ratio", kRatio, Unit::Ratio, 4.0),
    number("comp_attack", "Attack", "Attack", kAttack, Unit::Ms, 10.0),
    number("comp_release", "Release", "Release", kRelease, Unit::Ms, 100.0),
    boolean("comp_auto_gain", "Auto Gain", "AutoGain", true),
} };

double lowest(const Range& r) noexcept {
    return r.zero.empty() ? r.min : 0.0;
}

double highest(const Range& r) noexcept {
    return r.infText.empty() ? r.max : r.infValue;
}

double toNorm(const Range& r, double v) noexcept {
    const bool zero = !r.zero.empty();
    const bool inf = !r.infText.empty();
    const double lo = zero ? 0.04 : 0.0;
    const double hi = inf ? 0.96 : 1.0;
    if (zero && v <= 0.0) return 0.0;
    if (inf && v >= r.infValue) return 1.0;
    const double t = r.log ? std::log(v / r.min) / std::log(r.max / r.min) : (v - r.min) / (r.max - r.min);
    return lo + clamp01(t) * (hi - lo);
}

double fromNorm(const Range& r, double n) noexcept {
    const bool zero = !r.zero.empty();
    const bool inf = !r.infText.empty();
    const double lo = zero ? 0.04 : 0.0;
    const double hi = inf ? 0.96 : 1.0;
    if (zero && n < lo / 2.0) return 0.0;
    if (inf && n > (hi + 1.0) / 2.0) return r.infValue;
    const double t = clamp01((n - lo) / (hi - lo));
    double v = r.log ? r.min * std::pow(r.max / r.min, t) : r.min + t * (r.max - r.min);
    if (r.step != 0.0) v = jsRound(v / r.step) * r.step;
    return v;
}

double legalValue(const Range& r, double v) noexcept {
    if (!r.zero.empty() && v <= 0.0) return 0.0;
    if (!r.infText.empty() && v >= r.infValue) return r.infValue;
    v = std::clamp(v, r.min, r.max);
    if (r.step != 0.0) v = jsRound(v / r.step) * r.step;
    return v;
}

std::string valueText(const ParamSpec& spec, double value) {
    switch (spec.kind) {
    case Kind::Bool: return value >= 0.5 ? "On" : "Off";
    case Kind::Choice: {
        if (spec.choices.empty()) return {};
        const auto last = static_cast<double>(spec.choices.size() - 1);
        return std::string(spec.choices[static_cast<std::size_t>(std::clamp(jsRound(value), 0.0, last))]);
    }
    case Kind::Float: break;
    }
    return showValue(spec.range, spec.unit, value);
}

std::optional<double> parseValue(const ParamSpec& spec, std::string_view text) {
    const auto trimmed = trim(text);
    if (trimmed.empty()) return std::nullopt;
    auto s = lowerAscii(trimmed);
    if (spec.kind == Kind::Bool) {
        if (s == "on" || s == "1" || s == "true" || s == "yes") return 1.0;
        if (s == "off" || s == "0" || s == "false" || s == "no") return 0.0;
        return std::nullopt;
    }
    if (spec.kind == Kind::Choice) {
        // The choice's name, or its number without the unit ("24", "24 db").
        if (!removeSuffix(s, "db/oct")) removeSuffix(s, "db");
        const auto typed = readNumber(s);
        for (std::size_t i = 0; i < spec.choices.size(); ++i) {
            const auto name = lowerAscii(spec.choices[i]);
            if (s == name) return static_cast<double>(i);
            auto number = name;
            removeSuffix(number, "db/oct");
            if (typed && readNumber(number) == typed) return static_cast<double>(i);
        }
        return std::nullopt;
    }
    // inf and ∞, with or without the unit (-inf dB, ∞:1), go to the ends through legalValue.
    const auto& r = spec.range;
    if (!r.zero.empty() && s == r.zero) return 0.0;
    double scale = 1.0;
    switch (spec.unit) {
    case Unit::Db: removeSuffix(s, "db"); break;
    case Unit::Hz:
        if (removeSuffix(s, "khz")) scale = 1000.0;
        else removeSuffix(s, "hz");
        break;
    case Unit::Ms:
        if (!removeSuffix(s, "ms") && removeSuffix(s, "s")) scale = 1000.0;
        break;
    case Unit::Ratio: removeSuffix(s, ":1"); break;
    case Unit::Q:
    case Unit::None: break;
    }
    if (scale == 1.0 && removeSuffix(s, "k")) scale = 1000.0;
    const auto v = readNumber(s);
    if (!v) return std::nullopt;
    return legalValue(r, *v * scale);
}

} // namespace lesson
