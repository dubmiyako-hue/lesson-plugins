#pragma once
// Lesson EQ. Cycle 1 passes the sound through; the web app's 13 biquads come in cycle 2.
#include "../common/LessonProcessor.h"

namespace lesson::plugin {

class EqProcessor final : public LessonProcessor {
public:
    EqProcessor();
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using AudioProcessor::processBlock;
};

} // namespace lesson::plugin
