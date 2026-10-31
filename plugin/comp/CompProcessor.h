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
    void reset() override;

    const char* screenKey() const noexcept override { return "comp"; }
    // The Auto Gain for the settings, also while the switch is off (any thread).
    double makeupDb() const noexcept override { return makeupDb_.load(std::memory_order_relaxed); }
    lesson::CompPoints* compPoints() noexcept override { return &points_; }

private:
    CompEngineSettings settings() const noexcept;
    void restartTaps() noexcept override { points_.restart(); }

    CompEngine engine_;
    std::atomic<double> makeupDb_ { 0.0 };
    lesson::CompPoints points_; // the level history
    static_assert(std::atomic<double>::is_always_lock_free);
};

} // namespace lesson::plugin
