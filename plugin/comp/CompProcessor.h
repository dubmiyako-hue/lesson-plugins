#pragma once
// Lesson Comp. Cycle 1 passes the sound through; the web app's CompressorCore comes in cycle 2.
#include "../common/LessonProcessor.h"

namespace lesson::plugin {

class CompProcessor final : public LessonProcessor {
public:
    CompProcessor();
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using AudioProcessor::processBlock;
};

} // namespace lesson::plugin
