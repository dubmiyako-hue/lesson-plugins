#pragma once
// What Lesson EQ and Lesson Comp share: the parameters from a core table, the host's bypass, mono or stereo buses and the
// state with a version. The DSP is the subclass's.
#include <juce_audio_processors/juce_audio_processors.h>
#include <lesson/Params.h>
#include <atomic>
#include <span>
#include <vector>

namespace lesson::plugin {

class LessonProcessor : public juce::AudioProcessor {
public:
    static constexpr int kStateVersion = 1;

    LessonProcessor(juce::String name, std::span<const ParamSpec> specs, const juce::Identifier& stateType);

    const juce::String getName() const override { return name_; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    // The VST3 wrapper lists the one program, and the Steinberg validator fails a program without a name.
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}
    void releaseResources() override {}

    bool isBusesLayoutSupported(const BusesLayout& layout) const override;
    juce::AudioProcessorParameter* getBypassParameter() const override;
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    std::span<const ParamSpec> specs() const noexcept { return specs_; }
    juce::AudioProcessorValueTreeState& parameters() noexcept { return parameters_; }

protected:
    // The plain value of the parameter at `index` in the table (atomic; any thread).
    float raw(std::size_t index) const noexcept { return raw_[index]->load(std::memory_order_relaxed); }

private:
    juce::String name_;
    std::span<const ParamSpec> specs_;
    juce::AudioProcessorValueTreeState parameters_;
    std::vector<std::atomic<float>*> raw_;
    juce::AudioProcessorParameter* bypass_ = nullptr;
};

} // namespace lesson::plugin
