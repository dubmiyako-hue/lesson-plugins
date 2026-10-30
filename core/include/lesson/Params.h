#pragma once
// The parameters of Lesson EQ and Lesson Comp: ids, host names, ranges, defaults and the 0..1 mapping. The ranges and the
// mapping are the web app's (mix-course src/params.ts); the order is the signal flow (PRAYROOM behavior.md).
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace lesson {

enum class Kind { Float, Bool, Choice };
enum class Unit { None, Db, Hz, Ms, Ratio, Q };

// One control's range as params.ts describes it. `zero`: the lowest notch means 0 and shows this text. `infText`: the
// highest notch means infValue and shows this text.
struct Range {
    double min = 0.0;
    double max = 1.0;
    bool log = false;
    double step = 0.0;
    std::string_view zero {};
    double infValue = 0.0;
    std::string_view infText {};
};

// The lowest and highest values the control can hold (0 below the zero notch, infValue above the inf notch).
double lowest(const Range& r) noexcept;
double highest(const Range& r) noexcept;
// params.ts toNorm and fromNorm, operation for operation.
double toNorm(const Range& r, double v) noexcept;
double fromNorm(const Range& r, double n) noexcept;
// The value a host or a typed number lands on: the zero and inf notches, the range's ends and the step, as fromNorm(toNorm(v)).
double legalValue(const Range& r, double v) noexcept;

struct ParamSpec {
    std::string_view id;
    std::string_view name;      // the host's title, in English
    std::string_view shortName; // the host's short title, 8 characters or fewer
    Kind kind = Kind::Float;
    Range range {};             // Float only
    Unit unit = Unit::None;     // Float only
    double defaultValue = 0.0;  // Float: the value; Bool: 0 or 1; Choice: the index
    std::span<const std::string_view> choices {};
};

enum class EqParam : std::size_t {
    Bypass,
    HpOn, HpFreq, HpSlope,
    LsOn, LsFreq, LsGain,
    B1On, B1Freq, B1Gain, B1Q,
    B2On, B2Freq, B2Gain, B2Q,
    B3On, B3Freq, B3Gain, B3Q,
    HsOn, HsFreq, HsGain,
    LpOn, LpFreq, LpSlope,
    AutoGain,
    Count
};
enum class CompParam : std::size_t { Bypass, Threshold, Ratio, Attack, Release, AutoGain, Count };

constexpr std::size_t kNumEqParams = static_cast<std::size_t>(EqParam::Count);
constexpr std::size_t kNumCompParams = static_cast<std::size_t>(CompParam::Count);
constexpr std::size_t index(EqParam p) noexcept { return static_cast<std::size_t>(p); }
constexpr std::size_t index(CompParam p) noexcept { return static_cast<std::size_t>(p); }

extern const std::array<ParamSpec, kNumEqParams> kEqParams;
extern const std::array<ParamSpec, kNumCompParams> kCompParams;

// The Slope choices in dB/oct, in the order of the choice index.
inline constexpr std::array<int, 4> kSlopes { 12, 24, 36, 48 };

// The text for a value, the same on the screen and in the host: showValue of params.ts for a Float, On/Off for a Bool and
// the choice's name for a Choice (index rounded and clamped).
std::string valueText(const ParamSpec& spec, double value);
// Reads a typed value (PRAYROOM behavior.md): the unit may be left out, `k` multiplies by 1000, `∞`/`inf` is the top and
// `-inf` the bottom, 最速 is 0, and a value outside the range lands on its end. nullopt when the text cannot be read.
std::optional<double> parseValue(const ParamSpec& spec, std::string_view text);

} // namespace lesson
