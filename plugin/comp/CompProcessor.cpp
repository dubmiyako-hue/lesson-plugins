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
}

void CompProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    // The input and output buses have the same channels (isBusesLayoutSupported), so the buffer already holds the input.
    const NoDenormals noDenormals;
    engine_.process(buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples(), settings());
    makeupDb_.store(engine_.makeupDb(), std::memory_order_relaxed);
}

} // namespace lesson::plugin
