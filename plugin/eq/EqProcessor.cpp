#include "EqProcessor.h"
#include <lesson/NoDenormals.h>
#include <algorithm>
#include <cmath>

namespace lesson::plugin {

EqProcessor::EqProcessor() : LessonProcessor("Lesson EQ", kEqParams, "LessonEQ") {
    // Ready before the host prepares it, so that a stray processBlock finds a working engine.
    prepareToPlay(48000.0, 512);
}

EqEngineSettings EqProcessor::settings() const noexcept {
    const auto on = [this](EqParam p) { return raw(index(p)) >= 0.5f; };
    const auto value = [this](EqParam p) { return raw(index(p)); };
    const auto slope = [this](EqParam p) {
        const int choice = static_cast<int>(std::lround(raw(index(p))));
        return kSlopes[static_cast<std::size_t>(std::clamp(choice, 0, static_cast<int>(kSlopes.size()) - 1))];
    };
    const auto bell = [&](EqParam on_, EqParam freq, EqParam gain, EqParam q) {
        return EqBandSettings { on(on_), value(freq), value(gain), value(q), 12 };
    };
    EqEngineSettings s;
    s.bands = {
        EqBandSettings { on(EqParam::HpOn), value(EqParam::HpFreq), 0.0f, 1.0f, slope(EqParam::HpSlope) },
        EqBandSettings { on(EqParam::LsOn), value(EqParam::LsFreq), value(EqParam::LsGain), 1.0f, 12 },
        bell(EqParam::B1On, EqParam::B1Freq, EqParam::B1Gain, EqParam::B1Q),
        bell(EqParam::B2On, EqParam::B2Freq, EqParam::B2Gain, EqParam::B2Q),
        bell(EqParam::B3On, EqParam::B3Freq, EqParam::B3Gain, EqParam::B3Q),
        EqBandSettings { on(EqParam::HsOn), value(EqParam::HsFreq), value(EqParam::HsGain), 1.0f, 12 },
        EqBandSettings { on(EqParam::LpOn), value(EqParam::LpFreq), 0.0f, 1.0f, slope(EqParam::LpSlope) },
    };
    s.autoGain = on(EqParam::AutoGain);
    s.bypass = on(EqParam::Bypass);
    return s;
}

void EqProcessor::prepareToPlay(double sampleRate, int) {
    if (!(sampleRate > 0)) sampleRate = 48000.0; // a host that has no rate yet
    engine_.prepare(sampleRate, settings());
    makeupDb_.store(engine_.makeupDb(), std::memory_order_relaxed);
    restartTaps(); // the rings' samples were at the old rate
    setPreparedRate(sampleRate);
}

// The host's reset (VST3 setProcessing(false): on the UI or the processing thread, never while processBlock runs, and to
// be a light operation, no allocation): the sound starts afresh at the current settings, as after prepareToPlay, so the
// next play does not begin with the last one's ring.
void EqProcessor::reset() {
    engine_.reset(settings());
    makeupDb_.store(engine_.makeupDb(), std::memory_order_relaxed);
    restartTaps();
}

// The current settings' ring at the prepared rate (lesson::eqTailSeconds). VST3 has no flag that tells the host the tail
// changed, so a host that reads it once (at activation) keeps that value.
double EqProcessor::getTailLengthSeconds() const {
    return eqTailSeconds(settings().bands, preparedRate());
}

void EqProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    // The input and output buses have the same channels (isBusesLayoutSupported), so the buffer already holds the input.
    const NoDenormals noDenormals;
    const EqTaps taps { &pre_, &post_ };
    const bool screen = beginBlock();
    engine_.process(buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples(), settings(),
                    screen ? &taps : nullptr);
    makeupDb_.store(engine_.makeupDb(), std::memory_order_relaxed);
}

} // namespace lesson::plugin
