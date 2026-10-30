// Mono to mono and stereo to stereo only; no latency, no tail. At the defaults both plug-ins leave a sine as it is: the
// EQ's stages are 0 dB and its Auto Gain 0 dB, and nothing goes over the comp's 0 dB threshold.
#include <doctest.h>
#include <comp/CompProcessor.h>
#include <eq/EqProcessor.h>

using namespace lesson::plugin;

namespace {
juce::AudioProcessor::BusesLayout layout(const juce::AudioChannelSet& in, const juce::AudioChannelSet& out) {
    juce::AudioProcessor::BusesLayout result;
    result.inputBuses.add(in);
    result.outputBuses.add(out);
    return result;
}

void checkBuses(juce::AudioProcessor& processor) {
    using Set = juce::AudioChannelSet;
    CHECK(processor.checkBusesLayoutSupported(layout(Set::mono(), Set::mono())));
    CHECK(processor.checkBusesLayoutSupported(layout(Set::stereo(), Set::stereo())));
    CHECK_FALSE(processor.checkBusesLayoutSupported(layout(Set::mono(), Set::stereo())));
    CHECK_FALSE(processor.checkBusesLayoutSupported(layout(Set::stereo(), Set::mono())));
    CHECK_FALSE(processor.checkBusesLayoutSupported(layout(Set::create5point1(), Set::create5point1())));
    CHECK_FALSE(processor.checkBusesLayoutSupported(layout(Set::disabled(), Set::stereo())));
    CHECK(processor.getLatencySamples() == 0);
    CHECK(processor.getTailLengthSeconds() == 0.0);
    CHECK_FALSE(processor.acceptsMidi());
    CHECK_FALSE(processor.producesMidi());
}

void checkPassThrough(juce::AudioProcessor& processor) {
    for (const int channels : { 1, 2 }) {
        const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        REQUIRE(processor.setBusesLayout(layout(set, set)));
        processor.prepareToPlay(48000.0, 256);
        juce::AudioBuffer<float> buffer(channels, 256);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < 256; ++i) buffer.setSample(ch, i, std::sin(0.01f * static_cast<float>(i * (ch + 1))));
        juce::AudioBuffer<float> input(buffer);
        juce::MidiBuffer midi;
        processor.processBlock(buffer, midi);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < 256; ++i) REQUIRE(buffer.getSample(ch, i) == input.getSample(ch, i));
        processor.releaseResources();
    }
}
}

TEST_CASE("[bus] Lesson EQ") {
    EqProcessor processor;
    checkBuses(processor);
    checkPassThrough(processor);
}

TEST_CASE("[bus] Lesson Comp") {
    CompProcessor processor;
    checkBuses(processor);
    checkPassThrough(processor);
}
