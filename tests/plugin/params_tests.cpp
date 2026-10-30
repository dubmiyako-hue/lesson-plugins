// AC-04: the parameters the host sees, against the tables of docs/dev-loop/requirements.md (written out here by hand, not
// read from the core's tables). AC-05 for the JUCE side: the plain value of a parameter is Math.fround(fromNorm(n)).
#include <doctest.h>
#include <comp/CompProcessor.h>
#include <eq/EqProcessor.h>
#include <lesson/Params.h>
#include <charconv>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace lesson;
using namespace lesson::plugin;

namespace {
enum class K { Bool, Float, Choice };
struct Expected {
    const char* id;
    const char* name;
    const char* shortName;
    K kind;
    float lowest;
    float highest;
    float defaultValue; // Choice: the index
    const char* defaultText;
};

const std::vector<Expected> kEq {
    { "bypass", "Bypass", "Bypass", K::Bool, 0, 1, 0, "Off" },
    { "hp_on", "HP On", "HP On", K::Bool, 0, 1, 0, "Off" },
    { "hp_freq", "HP Freq", "HP Freq", K::Float, 20, 20000, 80, "80.0 Hz" },
    { "hp_slope", "HP Slope", "HP Slope", K::Choice, 0, 3, 1, "24 dB/oct" },
    { "ls_on", "LS On", "LS On", K::Bool, 0, 1, 1, "On" },
    { "ls_freq", "LS Freq", "LS Freq", K::Float, 20, 20000, 120, "120 Hz" },
    { "ls_gain", "LS Gain", "LS Gain", K::Float, -18, 18, 0, "0.0 dB" },
    { "b1_on", "Bell 1 On", "B1 On", K::Bool, 0, 1, 1, "On" },
    { "b1_freq", "Bell 1 Freq", "B1 Freq", K::Float, 20, 20000, 300, "300 Hz" },
    { "b1_gain", "Bell 1 Gain", "B1 Gain", K::Float, -18, 18, 0, "0.0 dB" },
    { "b1_q", "Bell 1 Q", "B1 Q", K::Float, 0.3f, 12, 1, "1.00" },
    { "b2_on", "Bell 2 On", "B2 On", K::Bool, 0, 1, 1, "On" },
    { "b2_freq", "Bell 2 Freq", "B2 Freq", K::Float, 20, 20000, 1000, "1.00 kHz" },
    { "b2_gain", "Bell 2 Gain", "B2 Gain", K::Float, -18, 18, 0, "0.0 dB" },
    { "b2_q", "Bell 2 Q", "B2 Q", K::Float, 0.3f, 12, 1, "1.00" },
    { "b3_on", "Bell 3 On", "B3 On", K::Bool, 0, 1, 1, "On" },
    { "b3_freq", "Bell 3 Freq", "B3 Freq", K::Float, 20, 20000, 4000, "4.00 kHz" },
    { "b3_gain", "Bell 3 Gain", "B3 Gain", K::Float, -18, 18, 0, "0.0 dB" },
    { "b3_q", "Bell 3 Q", "B3 Q", K::Float, 0.3f, 12, 1, "1.00" },
    { "hs_on", "HS On", "HS On", K::Bool, 0, 1, 1, "On" },
    { "hs_freq", "HS Freq", "HS Freq", K::Float, 20, 20000, 8000, "8.00 kHz" },
    { "hs_gain", "HS Gain", "HS Gain", K::Float, -18, 18, 0, "0.0 dB" },
    { "lp_on", "LP On", "LP On", K::Bool, 0, 1, 0, "Off" },
    { "lp_freq", "LP Freq", "LP Freq", K::Float, 20, 20000, 18000, "18.0 kHz" },
    { "lp_slope", "LP Slope", "LP Slope", K::Choice, 0, 3, 0, "12 dB/oct" },
    { "eq_auto_gain", "Auto Gain", "AutoGain", K::Bool, 0, 1, 1, "On" },
};

// Attack and Release reach 0 at the 最速 notch, Ratio 1000 (RATIO_INF) at the ∞:1 notch.
const std::vector<Expected> kComp {
    { "bypass", "Bypass", "Bypass", K::Bool, 0, 1, 0, "Off" },
    { "comp_threshold", "Threshold", "Thresh", K::Float, -60, 0, 0, "0.0 dB" },
    { "comp_ratio", "Ratio", "Ratio", K::Float, 1, 1000, 4, "4.0:1" },
    { "comp_attack", "Attack", "Attack", K::Float, 0, 100, 10, "10.0 ms" },
    { "comp_release", "Release", "Release", K::Float, 0, 1000, 100, "100 ms" },
    { "comp_auto_gain", "Auto Gain", "AutoGain", K::Bool, 0, 1, 1, "On" },
};

void checkTable(LessonProcessor& processor, const std::vector<Expected>& expected) {
    const auto& parameters = processor.getParameters();
    REQUIRE(parameters.size() == static_cast<int>(expected.size()));
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const auto& e = expected[i];
        auto* p = dynamic_cast<juce::RangedAudioParameter*>(parameters[static_cast<int>(i)]);
        REQUIRE(p != nullptr);
        INFO(e.id);
        CHECK(p->getParameterID() == e.id);
        CHECK(p->getName(128) == e.name);
        CHECK(p->getName(8) == e.shortName);
        CHECK(juce::String(e.shortName).length() <= 8);
        CHECK(p->getLabel().isEmpty());
        CHECK(p->isAutomatable());
        CHECK(p->getText(p->getDefaultValue(), 128) == juce::String::fromUTF8(e.defaultText));
        const auto& range = p->getNormalisableRange();
        CHECK(range.start == e.lowest);
        CHECK(range.end == e.highest);
        CHECK(p->convertFrom0to1(p->getDefaultValue()) == doctest::Approx(e.defaultValue).epsilon(1e-6));
        switch (e.kind) {
        case K::Bool: CHECK(dynamic_cast<juce::AudioParameterBool*>(p) != nullptr); break;
        case K::Float: CHECK(dynamic_cast<juce::AudioParameterFloat*>(p) != nullptr); break;
        case K::Choice: {
            auto* choice = dynamic_cast<juce::AudioParameterChoice*>(p);
            REQUIRE(choice != nullptr);
            CHECK(choice->choices == juce::StringArray { "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" });
            break;
        }
        }
    }
    auto* bypass = processor.getBypassParameter();
    REQUIRE(bypass != nullptr);
    CHECK(bypass == parameters[0]);
    CHECK(dynamic_cast<juce::RangedAudioParameter*>(bypass)->getParameterID() == "bypass");
}

// The core's table entry for a JUCE parameter, by id.
const ParamSpec& specOf(const LessonProcessor& processor, const juce::RangedAudioParameter& p) {
    for (const auto& spec : processor.specs())
        if (p.getParameterID() == juce::String::fromUTF8(spec.id.data(), static_cast<int>(spec.id.size()))) return spec;
    FAIL("no spec for " << p.getParameterID());
    return processor.specs()[0];
}
}

TEST_CASE("[params] Lesson EQ's parameters are the requirements' table") {
    EqProcessor processor;
    CHECK(processor.getName() == "Lesson EQ");
    checkTable(processor, kEq);
}

TEST_CASE("[params] Lesson Comp's parameters are the requirements' table") {
    CompProcessor processor;
    CHECK(processor.getName() == "Lesson Comp");
    checkTable(processor, kComp);
}

TEST_CASE("[norm] a JUCE parameter's value is Math.fround of fromNorm, and its text the core's") {
    EqProcessor eq;
    CompProcessor comp;
    for (LessonProcessor* processor : { static_cast<LessonProcessor*>(&eq), static_cast<LessonProcessor*>(&comp) }) {
        for (auto* parameter : processor->getParameters()) {
            auto* p = dynamic_cast<juce::RangedAudioParameter*>(parameter);
            const auto& spec = specOf(*processor, *p);
            INFO(spec.id);
            for (int k = 0; k <= 4000; ++k) {
                const float n = static_cast<float>(k) / 4000.0f;
                const float value = p->convertFrom0to1(n);
                if (spec.kind == Kind::Float) {
                    REQUIRE(value == static_cast<float>(fromNorm(spec.range, n)));
                    CHECK(std::fabs(p->convertTo0to1(value) - static_cast<float>(toNorm(spec.range, value))) <= 1e-7f);
                }
                REQUIRE(p->getText(n, 128) == juce::String::fromUTF8(valueText(spec, value).c_str()));
            }
        }
    }
}

TEST_CASE("[norm] JUCE's 0..1 is params.ts's at the golden's float-exact points") {
    // The lines of tests/golden/params.tsv whose input is exactly a float, so that the host's float 0..1 and plain value
    // are the web app's own numbers: fromNorm to Math.fround of the golden, toNorm (at legal values) to its float.
    EqProcessor eq;
    CompProcessor comp;
    const std::map<std::string, juce::RangedAudioParameter*> byKey {
        { "threshold", comp.parameters().getParameter("comp_threshold") },
        { "ratio", comp.parameters().getParameter("comp_ratio") },
        { "attack", comp.parameters().getParameter("comp_attack") },
        { "release", comp.parameters().getParameter("comp_release") },
        { "freq", eq.parameters().getParameter("b1_freq") },
        { "gain", eq.parameters().getParameter("b1_gain") },
        { "q", eq.parameters().getParameter("b1_q") },
    };
    std::ifstream file(LESSON_GOLDEN_DIR "/params.tsv", std::ios::binary);
    REQUIRE(file.good());
    std::string text;
    int from = 0, to = 0;
    const auto number = [](const std::string& s) {
        double v = 0.0;
        std::from_chars(s.data(), s.data() + s.size(), v);
        return v;
    };
    while (std::getline(file, text)) {
        std::vector<std::string> f;
        std::stringstream fields(text);
        std::string field;
        while (std::getline(fields, field, '\t')) f.push_back(field);
        if (f.size() < 4 || (f[0] != "from" && f[0] != "to")) continue;
        auto* p = byKey.at(f[1]);
        const double input = number(f[2]);
        const double expected = number(f[3]);
        if (static_cast<double>(static_cast<float>(input)) != input) continue;
        INFO(f[0] << " " << f[1] << " " << f[2]);
        if (f[0] == "from") {
            if (input < 0.0 || input > 1.0) continue; // JUCE clamps the host's 0..1 first
            CHECK(p->convertFrom0to1(static_cast<float>(input)) == static_cast<float>(expected));
            ++from;
        } else {
            const auto& range = p->getNormalisableRange();
            if (std::isnan(expected) || input < range.start || input > range.end) continue;
            if (range.snapToLegalValue(static_cast<float>(input)) != static_cast<float>(input)) continue;
            CHECK(p->convertTo0to1(static_cast<float>(input)) == static_cast<float>(expected));
            ++to;
        }
    }
    MESSAGE(from << " fromNorm and " << to << " toNorm points");
    CHECK(from >= 100);
    CHECK(to >= 50);
}

TEST_CASE("[norm] typed texts set the parameter, unreadable ones leave it where it is") {
    EqProcessor eq;
    auto* freq = eq.parameters().getParameter("b1_freq");
    CHECK(freq->convertFrom0to1(freq->getValueForText("2.5k")) == doctest::Approx(2500.0f));
    freq->setValueNotifyingHost(freq->getValueForText("2.5k"));
    CHECK(freq->convertFrom0to1(freq->getValueForText("abc")) == doctest::Approx(2500.0f));
    auto* slope = eq.parameters().getParameter("hp_slope");
    CHECK(slope->convertFrom0to1(slope->getValueForText("48")) == 3.0f);
    slope->setValueNotifyingHost(slope->getValueForText("36"));
    CHECK(slope->convertFrom0to1(slope->getValueForText("13")) == 2.0f);
    CompProcessor comp;
    auto* ratio = comp.parameters().getParameter("comp_ratio");
    CHECK(ratio->getValueForText(juce::String::fromUTF8("∞")) == 1.0f);
    CHECK(ratio->getValueForText("inf") == 1.0f);
    auto* attack = comp.parameters().getParameter("comp_attack");
    CHECK(attack->getValueForText(juce::String::fromUTF8("最速")) == 0.0f);
    auto* on = comp.parameters().getParameter("comp_auto_gain");
    CHECK(on->getValueForText("Off") == 0.0f);
    on->setValueNotifyingHost(0.0f);
    CHECK(on->getValueForText("what") == 0.0f);
}

TEST_CASE("[params] names between the short and the full length") {
    EqProcessor eq;
    auto* freq = eq.parameters().getParameter("b1_freq");
    CHECK(freq->getName(11) == "Bell 1 Freq");
    CHECK(freq->getName(10) == "B1 Freq");
    CHECK(freq->getName(4) == "B1 F");
    auto* q = eq.parameters().getParameter("b1_q");
    CHECK(q->getName(8) == "B1 Q");
    CHECK(q->getName(9) == "Bell 1 Q");
}
