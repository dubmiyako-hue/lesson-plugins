// The plug-ins' parameters reach the core's settings as they should: the processor's output is the engine's output for the
// settings the mapping spells out (the band order, the On threshold of 0.5, the Slope choice through kSlopes, the fixed
// gain, Q and slope of the bands that lack them). Every value is set to something other than its default and the
// parameters are read back as the processor holds them (floats). The comparison is bit for bit; a settings value
// swapped with another's must give a different output, or the comparison would pass whatever the mapping did.
#include <doctest.h>
#include <comp/CompProcessor.h>
#include <eq/EqProcessor.h>
#include <lesson/Engine.h>
#include <lesson/NoDenormals.h>
#include <lesson/Params.h>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

using namespace lesson;
using namespace lesson::plugin;

namespace {
using Channels = std::vector<std::vector<float>>;

constexpr double kFs = 48000.0;
constexpr int kBlock = 512;

// Stereo noise, a different sequence in each channel; the level steps between `loud` and `quiet` every `segment` frames.
Channels noise(int frames, std::uint32_t seed, float loud, float quiet, int segment) {
    Channels x(2, std::vector<float>(static_cast<std::size_t>(frames)));
    for (std::size_t c = 0; c < x.size(); ++c) {
        std::uint32_t state = seed + 7919u * static_cast<std::uint32_t>(c);
        for (std::size_t i = 0; i < x[c].size(); ++i) {
            state = state * 1664525u + 1013904223u;
            const float u = static_cast<float>((static_cast<double>(state >> 8) - 8388608.0) / 8388608.0);
            x[c][i] = u * (((static_cast<int>(i) / segment) % 2 == 0) ? loud : quiet);
        }
    }
    return x;
}

std::size_t mismatches(const Channels& a, const Channels& b) {
    REQUIRE(a.size() == b.size());
    std::size_t count = 0;
    for (std::size_t c = 0; c < a.size(); ++c) {
        REQUIRE(a[c].size() == b[c].size());
        for (std::size_t i = 0; i < a[c].size(); ++i)
            if (!(a[c][i] == b[c][i])) ++count;
    }
    return count;
}

juce::RangedAudioParameter& parameterAt(LessonProcessor& processor, std::size_t i) {
    auto* p = dynamic_cast<juce::RangedAudioParameter*>(processor.getParameters()[static_cast<int>(i)]);
    REQUIRE(p != nullptr);
    return *p;
}

void setValue(LessonProcessor& processor, std::size_t i, float value) {
    auto& p = parameterAt(processor, i);
    p.setValueNotifyingHost(p.convertTo0to1(value));
}

// The value the processor holds for the parameter at `i` (a Choice: its index).
float heldValue(LessonProcessor& processor, std::size_t i) {
    auto& p = parameterAt(processor, i);
    auto* raw = processor.parameters().getRawParameterValue(p.getParameterID());
    REQUIRE(raw != nullptr);
    return raw->load();
}

template <std::size_t N>
std::array<float, N> heldValues(LessonProcessor& processor) {
    std::array<float, N> values {};
    for (std::size_t i = 0; i < N; ++i) values[i] = heldValue(processor, i);
    return values;
}

// Stereo in and out, 48 kHz, 512-frame blocks; `input` is a whole number of blocks.
Channels runProcessor(juce::AudioProcessor& processor, const Channels& input) {
    const NoDenormals noDenormals;
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(juce::AudioChannelSet::stereo());
    layout.outputBuses.add(juce::AudioChannelSet::stereo());
    REQUIRE(processor.setBusesLayout(layout));
    processor.prepareToPlay(kFs, kBlock);
    Channels out = input;
    const int frames = static_cast<int>(input[0].size());
    REQUIRE(frames % kBlock == 0);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < frames; pos += kBlock) {
        juce::AudioBuffer<float> buffer(2, kBlock);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlock; ++i)
                buffer.setSample(ch, i, input[static_cast<std::size_t>(ch)][static_cast<std::size_t>(pos + i)]);
        processor.processBlock(buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlock; ++i)
                out[static_cast<std::size_t>(ch)][static_cast<std::size_t>(pos + i)] = buffer.getSample(ch, i);
    }
    processor.releaseResources();
    return out;
}

template <class Engine, class Settings>
Channels runEngine(const Settings& settings, const Channels& input) {
    const NoDenormals noDenormals;
    Engine engine;
    engine.prepare(kFs, settings);
    Channels out = input;
    const int frames = static_cast<int>(input[0].size());
    for (int pos = 0; pos < frames; pos += kBlock) {
        float* channels[2] = { out[0].data() + pos, out[1].data() + pos };
        engine.process(channels, 2, kBlock, settings);
    }
    return out;
}

bool on(float value) { return value >= 0.5f; }

// The mapping as the spec gives it, from the values the processor holds (EqParam order).
EqEngineSettings eqSettingsOf(const std::array<float, kNumEqParams>& v) {
    const auto at = [&](EqParam p) { return v[index(p)]; };
    const auto slope = [&](EqParam p) { return kSlopes[static_cast<std::size_t>(std::lround(at(p)))]; };
    EqEngineSettings s;
    s.bands[0] = { on(at(EqParam::HpOn)), at(EqParam::HpFreq), 0.0f, 1.0f, slope(EqParam::HpSlope) };
    s.bands[1] = { on(at(EqParam::LsOn)), at(EqParam::LsFreq), at(EqParam::LsGain), 1.0f, 12 };
    s.bands[2] = { on(at(EqParam::B1On)), at(EqParam::B1Freq), at(EqParam::B1Gain), at(EqParam::B1Q), 12 };
    s.bands[3] = { on(at(EqParam::B2On)), at(EqParam::B2Freq), at(EqParam::B2Gain), at(EqParam::B2Q), 12 };
    s.bands[4] = { on(at(EqParam::B3On)), at(EqParam::B3Freq), at(EqParam::B3Gain), at(EqParam::B3Q), 12 };
    s.bands[5] = { on(at(EqParam::HsOn)), at(EqParam::HsFreq), at(EqParam::HsGain), 1.0f, 12 };
    s.bands[6] = { on(at(EqParam::LpOn)), at(EqParam::LpFreq), 0.0f, 1.0f, slope(EqParam::LpSlope) };
    s.autoGain = on(at(EqParam::AutoGain));
    s.bypass = on(at(EqParam::Bypass));
    return s;
}

CompEngineSettings compSettingsOf(const std::array<float, kNumCompParams>& v) {
    const auto at = [&](CompParam p) { return v[index(p)]; };
    CompEngineSettings s;
    s.comp = { at(CompParam::Threshold), at(CompParam::Ratio), at(CompParam::Attack), at(CompParam::Release) };
    s.autoGain = on(at(CompParam::AutoGain));
    s.bypass = on(at(CompParam::Bypass));
    return s;
}

struct EqScenario {
    const char* name;
    bool autoGain;
    bool bypass;
    std::array<bool, kEqBands> bandOn; // HP, LS, Bell 1 to 3, HS, LP
};

struct EqMutation {
    const char* name;
    std::function<void(EqEngineSettings&)> apply;
};

bool sameSettings(const EqEngineSettings& a, const EqEngineSettings& b) {
    return a.bands == b.bands && a.autoGain == b.autoGain && a.bypass == b.bypass;
}

template <class T>
void swapIn(EqEngineSettings& s, std::size_t a, std::size_t b, T EqBandSettings::*field) {
    std::swap(s.bands[a].*field, s.bands[b].*field);
}
}

TEST_CASE("[mapping] Lesson EQ parameters reach the bands of the engine") {
    const std::vector<EqScenario> scenarios {
        { "every band on and Auto Gain off", false, false, { true, true, true, true, true, true, true } },
        { "every band on and Auto Gain on", true, false, { true, true, true, true, true, true, true } },
        { "LS and Bell 2 and LP off", true, false, { true, false, true, false, true, true, false } },
        { "bypassed", false, true, { true, true, true, true, true, true, true } },
    };
    const std::array<EqMutation, 13> mutations { {
        { "HP and LP Freq", [](EqEngineSettings& s) { swapIn(s, 0, 6, &EqBandSettings::freq); } },
        { "HP and LP Slope", [](EqEngineSettings& s) { swapIn(s, 0, 6, &EqBandSettings::slope); } },
        { "LS and HS Freq", [](EqEngineSettings& s) { swapIn(s, 1, 5, &EqBandSettings::freq); } },
        { "LS and HS Gain", [](EqEngineSettings& s) { swapIn(s, 1, 5, &EqBandSettings::gain); } },
        { "Bell 1 and Bell 3 Freq", [](EqEngineSettings& s) { swapIn(s, 2, 4, &EqBandSettings::freq); } },
        { "Bell 1 and Bell 3 Gain", [](EqEngineSettings& s) { swapIn(s, 2, 4, &EqBandSettings::gain); } },
        { "Bell 1 and Bell 2 Q", [](EqEngineSettings& s) { swapIn(s, 2, 3, &EqBandSettings::q); } },
        { "Bell 2 and Bell 3 Q", [](EqEngineSettings& s) { swapIn(s, 3, 4, &EqBandSettings::q); } },
        { "Bell 2 and Bell 3 Gain", [](EqEngineSettings& s) { swapIn(s, 3, 4, &EqBandSettings::gain); } },
        { "HP On and LP On", [](EqEngineSettings& s) { std::swap(s.bands[0].on, s.bands[6].on); } },
        { "LS On and HS On", [](EqEngineSettings& s) { std::swap(s.bands[1].on, s.bands[5].on); } },
        { "Bell 1 On and Bell 2 On", [](EqEngineSettings& s) { std::swap(s.bands[2].on, s.bands[3].on); } },
        { "Auto Gain", [](EqEngineSettings& s) { s.autoGain = !s.autoGain; } },
    } };
    const Channels input = noise(6 * kBlock, 21, 0.5f, 0.5f, 1 << 20);

    for (const auto& scenario : scenarios) {
        CAPTURE(scenario.name);
        EqProcessor processor;
        // Every value is away from its default: HP 36 dB/oct (the default is 24) and LP 48 dB/oct (12), the bells all different.
        setValue(processor, index(EqParam::Bypass), scenario.bypass ? 1.0f : 0.0f);
        setValue(processor, index(EqParam::HpOn), scenario.bandOn[0] ? 1.0f : 0.0f);
        setValue(processor, index(EqParam::HpFreq), 150.0f);
        setValue(processor, index(EqParam::HpSlope), 2.0f);
        setValue(processor, index(EqParam::LsOn), scenario.bandOn[1] ? 1.0f : 0.0f);
        setValue(processor, index(EqParam::LsFreq), 250.0f);
        setValue(processor, index(EqParam::LsGain), 7.5f);
        setValue(processor, index(EqParam::B1On), scenario.bandOn[2] ? 1.0f : 0.0f);
        setValue(processor, index(EqParam::B1Freq), 450.0f);
        setValue(processor, index(EqParam::B1Gain), -6.5f);
        setValue(processor, index(EqParam::B1Q), 2.5f);
        setValue(processor, index(EqParam::B2On), scenario.bandOn[3] ? 1.0f : 0.0f);
        setValue(processor, index(EqParam::B2Freq), 1800.0f);
        setValue(processor, index(EqParam::B2Gain), 5.0f);
        setValue(processor, index(EqParam::B2Q), 0.7f);
        setValue(processor, index(EqParam::B3On), scenario.bandOn[4] ? 1.0f : 0.0f);
        setValue(processor, index(EqParam::B3Freq), 6500.0f);
        setValue(processor, index(EqParam::B3Gain), -9.0f);
        setValue(processor, index(EqParam::B3Q), 4.0f);
        setValue(processor, index(EqParam::HsOn), scenario.bandOn[5] ? 1.0f : 0.0f);
        setValue(processor, index(EqParam::HsFreq), 10000.0f);
        setValue(processor, index(EqParam::HsGain), 4.5f);
        setValue(processor, index(EqParam::LpOn), scenario.bandOn[6] ? 1.0f : 0.0f);
        setValue(processor, index(EqParam::LpFreq), 14000.0f);
        setValue(processor, index(EqParam::LpSlope), 3.0f);
        setValue(processor, index(EqParam::AutoGain), scenario.autoGain ? 1.0f : 0.0f);

        const auto held = heldValues<kNumEqParams>(processor);
        // The setting took: no value the test chose is still at its default, and the switches are what the scenario says.
        for (std::size_t i = 0; i < kNumEqParams; ++i) {
            INFO(kEqParams[i].id);
            const bool isSwitch = kEqParams[i].kind == Kind::Bool;
            if (!isSwitch) CHECK(held[i] != static_cast<float>(kEqParams[i].defaultValue));
        }
        CHECK(held[index(EqParam::HpSlope)] == 2.0f);
        CHECK(held[index(EqParam::LpSlope)] == 3.0f);
        const EqEngineSettings expected = eqSettingsOf(held);
        CHECK(expected.bypass == scenario.bypass);
        CHECK(expected.autoGain == scenario.autoGain);
        for (std::size_t b = 0; b < kEqBands; ++b) CHECK(expected.bands[b].on == scenario.bandOn[b]);
        CHECK(expected.bands[0].slope == 36);
        CHECK(expected.bands[6].slope == 48);

        const Channels fromProcessor = runProcessor(processor, input);
        const Channels fromEngine = runEngine<EqEngine>(expected, input);
        CHECK(mismatches(fromProcessor, fromEngine) == 0);

        // The comparison tells the mappings apart. Bypassed, only the bypass itself shows.
        if (scenario.bypass) {
            EqEngineSettings flipped = expected;
            flipped.bypass = false;
            CHECK(mismatches(fromProcessor, runEngine<EqEngine>(flipped, input)) > 0);
        } else {
            for (const auto& mutation : mutations) {
                CAPTURE(mutation.name);
                EqEngineSettings other = expected;
                mutation.apply(other);
                if (sameSettings(other, expected)) continue; // nothing to tell apart (a pair that is equal in this scenario)
                CHECK(mismatches(fromProcessor, runEngine<EqEngine>(other, input)) > 0);
            }
            EqEngineSettings bypassed = expected;
            bypassed.bypass = true;
            CHECK(mismatches(fromProcessor, runEngine<EqEngine>(bypassed, input)) > 0);
        }
    }
}

TEST_CASE("[mapping] Lesson Comp parameters reach the settings of the engine") {
    struct Scenario {
        const char* name;
        bool autoGain;
        bool bypass;
    };
    const std::vector<Scenario> scenarios {
        { "Auto Gain off", false, false },
        { "Auto Gain on", true, false },
        { "bypassed", false, true },
    };
    // Loud and quiet in turn every 1024 frames: the Attack and the Release both have something to do. 8 blocks.
    const Channels input = noise(8 * kBlock, 33, 0.8f, 0.01f, 1024);

    for (const auto& scenario : scenarios) {
        CAPTURE(scenario.name);
        CompProcessor processor;
        // Away from the defaults (0 dB, 4:1, 10 ms, 100 ms) and from one another: Attack 3 ms and Release 400 ms, Threshold
        // -30 dB and Ratio 6:1.
        setValue(processor, index(CompParam::Bypass), scenario.bypass ? 1.0f : 0.0f);
        setValue(processor, index(CompParam::Threshold), -30.0f);
        setValue(processor, index(CompParam::Ratio), 6.0f);
        setValue(processor, index(CompParam::Attack), 3.0f);
        setValue(processor, index(CompParam::Release), 400.0f);
        setValue(processor, index(CompParam::AutoGain), scenario.autoGain ? 1.0f : 0.0f);

        const auto held = heldValues<kNumCompParams>(processor);
        for (std::size_t i = 0; i < kNumCompParams; ++i) {
            INFO(kCompParams[i].id);
            if (kCompParams[i].kind != Kind::Bool) CHECK(held[i] != static_cast<float>(kCompParams[i].defaultValue));
        }
        const CompEngineSettings expected = compSettingsOf(held);
        CHECK(expected.bypass == scenario.bypass);
        CHECK(expected.autoGain == scenario.autoGain);
        // The Threshold is far enough under the noise for the compressor to work on it.
        CHECK(expected.comp.threshold < -20.0f);

        const Channels fromProcessor = runProcessor(processor, input);
        const Channels fromEngine = runEngine<CompEngine>(expected, input);
        CHECK(mismatches(fromProcessor, fromEngine) == 0);

        if (scenario.bypass) {
            CompEngineSettings flipped = expected;
            flipped.bypass = false;
            CHECK(mismatches(fromProcessor, runEngine<CompEngine>(flipped, input)) > 0);
        } else {
            CompEngineSettings attackAndRelease = expected;
            std::swap(attackAndRelease.comp.attack, attackAndRelease.comp.release);
            CHECK(mismatches(fromProcessor, runEngine<CompEngine>(attackAndRelease, input)) > 0);
            CompEngineSettings thresholdAndRatio = expected;
            std::swap(thresholdAndRatio.comp.threshold, thresholdAndRatio.comp.ratio);
            CHECK(mismatches(fromProcessor, runEngine<CompEngine>(thresholdAndRatio, input)) > 0);
            CompEngineSettings autoGain = expected;
            autoGain.autoGain = !autoGain.autoGain;
            CHECK(mismatches(fromProcessor, runEngine<CompEngine>(autoGain, input)) > 0);
            CompEngineSettings bypassed = expected;
            bypassed.bypass = true;
            CHECK(mismatches(fromProcessor, runEngine<CompEngine>(bypassed, input)) > 0);
        }
    }
}
