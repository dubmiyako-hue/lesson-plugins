#include "EqProcessor.h"

namespace lesson::plugin {

EqProcessor::EqProcessor() : LessonProcessor("Lesson EQ", kEqParams, "LessonEQ") {}

void EqProcessor::prepareToPlay(double, int) {}

void EqProcessor::processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) {
    // The input and output buses have the same channels (isBusesLayoutSupported), so the buffer already holds the input.
}

} // namespace lesson::plugin
