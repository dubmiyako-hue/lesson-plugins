#include "CompProcessor.h"
#include <lesson/NoDenormals.h>

namespace lesson::plugin {

CompProcessor::CompProcessor() : LessonProcessor("Lesson Comp", kCompParams, "LessonComp") {
    // Ready before the host prepares it, so that a stray processBlock finds a working engine.
    prepareToPlay(48000.0, 512);
}

CompEngineSettings CompProcessor::settings() const noexcept {
    const auto value = [this](CompParam p) { return raw(index(p)); };
    CompEngineSettings s;
    s.comp = { value(CompParam::Threshold), value(CompParam::Ratio), value(CompParam::Attack), value(CompParam::Release) };
    s.autoGain = value(CompParam::AutoGain) >= 0.5f;
    s.bypass = value(CompParam::Bypass) >= 0.5f;
    return s;
}

void CompProcessor::prepareToPlay(double sampleRate, int) {
    if (!(sampleRate > 0)) sampleRate = 48000.0; // a host that has no rate yet
    engine_.prepare(sampleRate, settings());
    makeupDb_.store(engine_.makeupDb(), std::memory_order_relaxed);
    restartTaps();
    setPreparedRate(sampleRate);
}

// The host's reset (VST3 setProcessing(false): on the UI or the processing thread, never while processBlock runs, and to
// be a light operation, no allocation): the sound starts afresh at the current settings, as after prepareToPlay, so the
// next play does not begin with the last one's GR.
void CompProcessor::reset() {
    engine_.reset(settings());
    makeupDb_.store(engine_.makeupDb(), std::memory_order_relaxed);
    restartTaps();
}

void CompProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    // The input and output buses have the same channels (isBusesLayoutSupported), so the buffer already holds the input.
    const NoDenormals noDenormals;
    const bool screen = beginBlock();
    engine_.process(buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples(), settings(),
                    screen ? &points_ : nullptr);
    makeupDb_.store(engine_.makeupDb(), std::memory_order_relaxed);
}

} // namespace lesson::plugin
