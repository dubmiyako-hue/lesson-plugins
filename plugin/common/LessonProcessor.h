#pragma once
// What Lesson EQ and Lesson Comp share: the parameters from a core table, the host's bypass, mono or stereo buses and the
// state with a version. The DSP is the subclass's.
#include <juce_audio_processors/juce_audio_processors.h>
#include <lesson/Params.h>
#include <lesson/Taps.h>
#include <atomic>
#include <cstdint>
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
    // The parameter of the table's entry `index` (its plain value is the screen's: Hz, dB, ms, the choice index, 0 or 1).
    juce::RangedAudioParameter& parameter(std::size_t index) const noexcept { return *params_[index]; }
    float plainValue(std::size_t index) const noexcept { return raw(index); }

    // ---- what the screen (Bridge) reads; any thread unless said ----
    // "eq" or "comp": the page's screen.
    virtual const char* screenKey() const noexcept = 0;
    // The Auto Gain computed for the settings, also while it is off (dB).
    virtual double makeupDb() const noexcept = 0;
    // The sample rate the sound was last prepared at (Hz): the rate of what the taps hold. JUCE's getSampleRate() is set
    // by the host's wrapper, not by prepareToPlay, so a caller that only prepares (a test) would not see it there.
    double preparedRate() const noexcept { return preparedRate_.load(std::memory_order_relaxed); }
    // processBlock calls so far: the screen takes the audio as playing while this moves.
    std::uint64_t blocksProcessed() const noexcept { return blocks_.load(std::memory_order_relaxed); }
    // The window is open: the taps below are written while it is (the Bridge sets it, message thread).
    void setScreenOpen(bool open) noexcept { screenOpen_.store(open, std::memory_order_relaxed); }
    // The EQ's spectrum inputs, and the compressor's level points; null where the plug-in has none.
    virtual lesson::SignalRing* spectrumPre() noexcept { return nullptr; }
    virtual lesson::SignalRing* spectrumPost() noexcept { return nullptr; }
    virtual lesson::CompPoints* compPoints() noexcept { return nullptr; }

protected:
    // The plain value of the parameter at `index` in the table (atomic; any thread).
    float raw(std::size_t index) const noexcept { return raw_[index]->load(std::memory_order_relaxed); }
    // prepareToPlay: the rate it prepared the sound at.
    void setPreparedRate(double rate) noexcept { preparedRate_.store(rate, std::memory_order_relaxed); }
    // processBlock: counts the call, and says whether the taps are to be written.
    bool beginBlock() noexcept {
        blocks_.fetch_add(1, std::memory_order_relaxed);
        return screenOpen_.load(std::memory_order_relaxed);
    }

private:
    juce::String name_;
    std::span<const ParamSpec> specs_;
    juce::AudioProcessorValueTreeState parameters_;
    std::vector<std::atomic<float>*> raw_;
    juce::AudioProcessorParameter* bypass_ = nullptr;
    std::vector<juce::RangedAudioParameter*> params_; // in the table's order
    std::atomic<std::uint64_t> blocks_ { 0 };
    std::atomic<bool> screenOpen_ { false };
    std::atomic<double> preparedRate_ { 48000.0 };
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free && std::atomic<bool>::is_always_lock_free
                  && std::atomic<double>::is_always_lock_free);
};

} // namespace lesson::plugin
