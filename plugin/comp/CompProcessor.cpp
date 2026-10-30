#include "CompProcessor.h"

namespace lesson::plugin {

CompProcessor::CompProcessor() : LessonProcessor("Lesson Comp", kCompParams, "LessonComp") {}

void CompProcessor::prepareToPlay(double, int) {}

void CompProcessor::processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) {
    // The input and output buses have the same channels (isBusesLayoutSupported), so the buffer already holds the input.
}

} // namespace lesson::plugin
