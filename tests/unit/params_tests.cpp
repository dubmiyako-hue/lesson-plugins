// AC-05 (the 0..1 mapping) and the value texts against tests/golden/params.tsv, which tests/golden/gen-params.mjs wrote
// from the web app's src/params.ts; then the typed values (PRAYROOM behavior.md), which the web app does not have.
#include <doctest.h>
#include <lesson/Format.h>
#include <lesson/Params.h>
#include <charconv>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace lesson;

namespace {
struct Line {
    std::vector<std::string> fields;
};
std::vector<Line> golden(std::string_view kind) {
    std::ifstream file(LESSON_GOLDEN_DIR "/params.tsv", std::ios::binary);
    REQUIRE(file.good());
    std::vector<Line> lines;
    std::string text;
    while (std::getline(file, text)) {
        if (!text.empty() && text.back() == '\r') text.pop_back();
        Line line;
        std::stringstream fields(text);
        std::string field;
        while (std::getline(fields, field, '\t')) line.fields.push_back(field);
        if (!line.fields.empty() && line.fields[0] == kind) lines.push_back(std::move(line));
    }
    REQUIRE(!lines.empty());
    return lines;
}
double number(const std::string& text) {
    double v = 0.0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), v);
    REQUIRE_MESSAGE(result.ptr == text.data() + text.size(), "not a number: " << text);
    return v;
}
// The golden's keys: the web app's PARAMS and BAND_PARAMS.
const ParamSpec& specFor(const std::string& key) {
    static const std::map<std::string, const ParamSpec*> specs {
        { "threshold", &kCompParams[index(CompParam::Threshold)] },
        { "ratio", &kCompParams[index(CompParam::Ratio)] },
        { "attack", &kCompParams[index(CompParam::Attack)] },
        { "release", &kCompParams[index(CompParam::Release)] },
        { "freq", &kEqParams[index(EqParam::B1Freq)] },
        { "gain", &kEqParams[index(EqParam::B1Gain)] },
        { "q", &kEqParams[index(EqParam::B1Q)] },
    };
    const auto it = specs.find(key);
    REQUIRE_MESSAGE(it != specs.end(), "unknown key " << key);
    return *it->second;
}
bool same(double a, double b) {
    return (std::isnan(a) && std::isnan(b)) || a == b;
}
}

TEST_CASE("[norm] fromNorm matches params.ts") {
    // 1e-9 (provisional, AC-05) for the log curves, whose pow may differ from V8's in the last bit; the steps and the
    // notches (0, the step's multiples, infValue) must be equal.
    int checked = 0, failures = 0;
    double worst = 0.0;
    for (const auto& line : golden("from")) {
        const auto& spec = specFor(line.fields[1]);
        const double n = number(line.fields[2]);
        const double expected = number(line.fields[3]);
        const double actual = fromNorm(spec.range, n);
        const bool exact = !spec.range.log || expected == 0.0 || expected == spec.range.infValue;
        const double diff = std::fabs(actual - expected);
        worst = std::max(worst, diff);
        if (exact ? !same(actual, expected) : !(diff <= 1e-9)) {
            if (++failures <= 10) MESSAGE(line.fields[1] << " n=" << line.fields[2] << " expected " << line.fields[3] << " got " << actual);
        }
        ++checked;
    }
    MESSAGE(checked << " values, largest difference " << worst);
    CHECK(failures == 0);
}

TEST_CASE("[norm] toNorm matches params.ts") {
    int checked = 0, failures = 0;
    double worst = 0.0;
    for (const auto& line : golden("to")) {
        const auto& spec = specFor(line.fields[1]);
        const double v = number(line.fields[2]);
        const double expected = number(line.fields[3]);
        const double actual = toNorm(spec.range, v);
        const double diff = std::isnan(expected) ? 0.0 : std::fabs(actual - expected);
        worst = std::max(worst, diff);
        if (std::isnan(expected) ? !std::isnan(actual) : !(diff <= 1e-9)) {
            if (++failures <= 10) MESSAGE(line.fields[1] << " v=" << line.fields[2] << " expected " << line.fields[3] << " got " << actual);
        }
        ++checked;
    }
    MESSAGE(checked << " values, largest difference " << worst);
    CHECK(failures == 0);
}

TEST_CASE("[norm] the notches and the steps land where params.ts puts them") {
    const auto& attack = kCompParams[index(CompParam::Attack)].range;
    const auto& ratio = kCompParams[index(CompParam::Ratio)].range;
    const auto& threshold = kCompParams[index(CompParam::Threshold)].range;
    CHECK(fromNorm(attack, 0.0199999) == 0.0);
    CHECK(fromNorm(attack, 0.02) == doctest::Approx(0.1));
    CHECK(fromNorm(attack, 0.04) == doctest::Approx(0.1));
    CHECK(toNorm(attack, 0.0) == 0.0);
    CHECK(fromNorm(ratio, 0.98) == doctest::Approx(20.0));
    CHECK(fromNorm(ratio, 0.9800001) == 1000.0);
    CHECK(toNorm(ratio, 1000.0) == 1.0);
    CHECK(fromNorm(threshold, 0.5) == -30.0);
    CHECK(fromNorm(threshold, 0.504) == -30.0);
    CHECK(fromNorm(threshold, 0.505) == -29.5);
}

TEST_CASE("[text] value texts match params.ts showValue") {
    int checked = 0, failures = 0;
    for (const auto& line : golden("text")) {
        const auto& spec = specFor(line.fields[1]);
        const double v = number(line.fields[2]);
        const auto expected = line.fields.size() > 3 ? line.fields[3] : std::string();
        const auto actual = showValue(spec.range, spec.unit, v);
        if (actual != expected) {
            if (++failures <= 10) MESSAGE(line.fields[1] << " v=" << line.fields[2] << " expected '" << expected << "' got '" << actual << "'");
        }
        ++checked;
    }
    MESSAGE(checked << " texts");
    CHECK(failures == 0);
}

TEST_CASE("[text] toFixed and Math.round match JavaScript") {
    int failures = 0;
    for (const auto& line : golden("fixed")) {
        const int digits = static_cast<int>(number(line.fields[1]));
        const auto actual = toFixed(number(line.fields[2]), digits);
        if (actual != line.fields[3] && ++failures <= 10)
            MESSAGE("(" << line.fields[2] << ").toFixed(" << digits << ") expected " << line.fields[3] << " got " << actual);
    }
    for (const auto& line : golden("round")) {
        const auto actual = jsRound(number(line.fields[1]));
        if (actual != number(line.fields[2]) && ++failures <= 10)
            MESSAGE("Math.round(" << line.fields[1] << ") expected " << line.fields[2] << " got " << actual);
    }
    CHECK(failures == 0);
}

TEST_CASE("[text] toFixed where JavaScript prints String(v)") {
    // The golden cannot hold these: `${-0}` prints 0.
    CHECK(toFixed(-0.0, 1) == "0.0");
    CHECK(toFixed(-0.0, 0) == "0");
    CHECK(toFixed(-0.04, 1) == "-0.0");
    CHECK(toFixed(std::nan(""), 2) == "NaN");
    CHECK(toFixed(HUGE_VAL, 2) == "Infinity");
    CHECK(toFixed(-HUGE_VAL, 2) == "-Infinity");
    CHECK(toFixed(1e21, 2) == "1e+21");
    CHECK(fmtHz(std::nan("")) == "NaN Hz");
    CHECK(fmtDb(-0.0) == "0.0 dB");
}

TEST_CASE("[text] On/Off and the Slope choices") {
    const auto& on = kEqParams[index(EqParam::HpOn)];
    const auto& slope = kEqParams[index(EqParam::HpSlope)];
    CHECK(valueText(on, 1.0) == "On");
    CHECK(valueText(on, 0.0) == "Off");
    CHECK(valueText(slope, 0.0) == "12 dB/oct");
    CHECK(valueText(slope, 1.0) == "24 dB/oct");
    CHECK(valueText(slope, 3.0) == "48 dB/oct");
    CHECK(valueText(kCompParams[index(CompParam::Attack)], 0.0) == "最速");
    CHECK(valueText(kCompParams[index(CompParam::Ratio)], 1000.0) == "∞:1");
    CHECK(valueText(kCompParams[index(CompParam::Ratio)], 4.0) == "4.0:1");
    CHECK(valueText(kEqParams[index(EqParam::B1Freq)], 2500.0) == "2.50 kHz");
    CHECK(valueText(kEqParams[index(EqParam::B1Gain)], -18.0) == "-18.0 dB");
    CHECK(valueText(kEqParams[index(EqParam::B1Q)], 1.0) == "1.00");
}

TEST_CASE("[parse] typed values") {
    const auto& freq = kEqParams[index(EqParam::B1Freq)];
    const auto& gain = kEqParams[index(EqParam::B1Gain)];
    const auto& q = kEqParams[index(EqParam::B1Q)];
    const auto& threshold = kCompParams[index(CompParam::Threshold)];
    const auto& ratio = kCompParams[index(CompParam::Ratio)];
    const auto& attack = kCompParams[index(CompParam::Attack)];
    const auto& release = kCompParams[index(CompParam::Release)];
    const auto& on = kEqParams[index(EqParam::HpOn)];
    const auto& slope = kEqParams[index(EqParam::HpSlope)];

    CHECK(parseValue(freq, "2.5k") == 2500.0);
    CHECK(parseValue(freq, "2.5 kHz") == 2500.0);
    CHECK(parseValue(freq, "440 Hz") == 440.0);
    CHECK(parseValue(freq, "440") == 440.0);
    CHECK(parseValue(freq, " 1000hz ") == 1000.0);
    CHECK(parseValue(freq, "5") == 20.0);       // below the range: its end
    CHECK(parseValue(freq, "50k") == 20000.0);
    CHECK(parseValue(freq, "inf") == 20000.0);
    CHECK(parseValue(freq, "inf Hz") == 20000.0);
    CHECK(parseValue(freq, "1e999") == 20000.0);   // too large for a double: the top
    CHECK(parseValue(freq, "-1e999") == 20.0);
    CHECK(parseValue(freq, "1e-999") == 20.0);     // too small: 0, then the bottom
    CHECK(parseValue(gain, "-inf dB") == -18.0);
    CHECK(parseValue(attack, "-inf ms") == 0.0);
    CHECK(parseValue(ratio, "inf:1") == 1000.0);
    CHECK(parseValue(gain, "-18.0 dB") == -18.0);
    CHECK(parseValue(gain, "+6") == 6.0);
    CHECK(parseValue(gain, "-12.3") == -12.5);  // the 0.5 dB step, rounded as fromNorm rounds
    CHECK(parseValue(gain, "-12.2") == -12.0);
    CHECK(parseValue(gain, "-inf") == -18.0);
    CHECK(parseValue(gain, "40 dB") == 18.0);
    CHECK(parseValue(q, "0.71") == 0.71);
    CHECK(parseValue(q, "100") == 12.0);
    CHECK(parseValue(threshold, "-inf") == -60.0);
    CHECK(parseValue(threshold, "5") == 0.0);
    CHECK(parseValue(ratio, "4:1") == 4.0);
    CHECK(parseValue(ratio, "4") == 4.0);
    CHECK(parseValue(ratio, "∞") == 1000.0);
    CHECK(parseValue(ratio, "∞:1") == 1000.0);
    CHECK(parseValue(ratio, "INF") == 1000.0);
    CHECK(parseValue(ratio, "50") == 20.0);     // between 20:1 and the ∞ notch: 20:1, as toNorm then fromNorm
    CHECK(parseValue(ratio, "2000") == 1000.0);
    CHECK(parseValue(ratio, "0.5") == 1.0);
    CHECK(parseValue(attack, "最速") == 0.0);
    CHECK(parseValue(attack, "0") == 0.0);
    CHECK(parseValue(attack, "0.05") == 0.1);   // between the 最速 notch and 0.1 ms: 0.1 ms
    CHECK(parseValue(attack, "10 ms") == 10.0);
    CHECK(parseValue(release, "1 s") == 1000.0);
    CHECK(parseValue(release, "0.25s") == 250.0);
    CHECK(parseValue(release, "250ms") == 250.0);
    CHECK(parseValue(on, "On") == 1.0);
    CHECK(parseValue(on, "off") == 0.0);
    CHECK(parseValue(slope, "24 dB/oct") == 1.0);
    CHECK(parseValue(slope, "48") == 3.0);
    CHECK(parseValue(slope, "36 dB") == 2.0);

    CHECK_FALSE(parseValue(freq, "").has_value());
    CHECK_FALSE(parseValue(freq, "abc").has_value());
    CHECK_FALSE(parseValue(freq, "12 ms").has_value());
    CHECK_FALSE(parseValue(gain, "nan").has_value());
    CHECK_FALSE(parseValue(on, "maybe").has_value());
    CHECK_FALSE(parseValue(slope, "13").has_value());
    CHECK_FALSE(parseValue(threshold, "最速").has_value());
}

TEST_CASE("[parse] every value text reads back as its value") {
    // The text the host shows must give the same value when typed back (up to the text's digits).
    for (const auto* table : { &kEqParams[0], &kCompParams[0] }) {
        const auto count = table == &kEqParams[0] ? kNumEqParams : kNumCompParams;
        for (std::size_t i = 0; i < count; ++i) {
            const auto& spec = table[i];
            if (spec.kind != Kind::Float) continue;
            for (int k = 0; k <= 100; ++k) {
                const double v = fromNorm(spec.range, k / 100.0);
                const auto text = valueText(spec, v);
                const auto back = parseValue(spec, text);
                REQUIRE_MESSAGE(back.has_value(), spec.id << ": " << text);
                CHECK_MESSAGE(valueText(spec, *back) == text, spec.id << ": " << text << " read as " << *back);
            }
        }
    }
}
