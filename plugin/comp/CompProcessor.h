#pragma once
// Lesson Comp: the web app's CompressorCore, Auto Gain and bypass (lesson::CompEngine).
#include "../common/LessonProcessor.h"
#include <lesson/Engine.h>
#include <atomic>

namespace lesson::plugin {

class CompProcessor final : public LessonProcessor {
public:
    CompProcessor();
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using AudioProcessor::processBlock;

    // The Auto Gain for the settings, also while the switch is off (any thread).
    double makeupDb() const noexcept { return makeupDb_.load(std::memory_order_relaxed); }

private:
    CompEngineSettings settings() const noexcept;

    CompEngine engine_;
    std::atomic<double> makeupDb_ { 0.0 };
    static_assert(std::atomic<double>::is_always_lock_free);
};

} // namespace lesson::plugin
