#pragma once
// Lesson EQ: the web app's 13 biquads, Auto Gain and bypass (lesson::EqEngine).
#include "../common/LessonProcessor.h"
#include <lesson/Engine.h>
#include <atomic>

namespace lesson::plugin {

class EqProcessor final : public LessonProcessor {
public:
    EqProcessor();
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using AudioProcessor::processBlock;

    // The Auto Gain computed for the settings, also while the switch is off (any thread).
    double makeupDb() const noexcept { return makeupDb_.load(std::memory_order_relaxed); }

private:
    EqEngineSettings settings() const noexcept;

    EqEngine engine_;
    std::atomic<double> makeupDb_ { 0.0 };
    static_assert(std::atomic<double>::is_always_lock_free);
};

} // namespace lesson::plugin
