// Mono to mono and stereo to stereo only; no latency, and no tail at the defaults. At the defaults both plug-ins leave a
// sine as it is: the EQ's stages are 0 dB and its Auto Gain 0 dB, and nothing goes over the comp's 0 dB threshold.
// The host's reset starts the sound afresh; the EQ's tail follows its settings.
#include <doctest.h>
#include <comp/CompProcessor.h>
#include <eq/EqProcessor.h>
#include <utility>
#include <vector>

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

namespace {
template <class Processor>
void setPlain(Processor& processor, const std::vector<std::pair<std::size_t, float>>& plain) {
    for (const auto& [at, value] : plain) {
        auto& parameter = processor.parameter(at);
        parameter.setValueNotifyingHost(parameter.convertTo0to1(value));
    }
}

// 1 s of loud noise, then the host's reset: the next block is a fresh processor's at the same settings, bit for bit.
template <class Processor>
void checkReset(const std::vector<std::pair<std::size_t, float>>& plain) {
    const auto stereo = juce::AudioChannelSet::stereo();
    Processor played;
    Processor fresh;
    for (Processor* p : { &played, &fresh }) {
        REQUIRE(p->setBusesLayout(layout(stereo, stereo)));
        setPlain(*p, plain);
        p->prepareToPlay(48000.0, 512);
    }
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> loud(2, 512);
    juce::Random random(7);
    for (int block = 0; block < 94; ++block) {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i) loud.setSample(ch, i, 0.99f * (2.0f * random.nextFloat() - 1.0f));
        played.processBlock(loud, midi);
    }
    played.reset();
    juce::AudioBuffer<float> a(2, 512);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i) a.setSample(ch, i, 0.5f * std::sin(0.01f * static_cast<float>(i * (ch + 1))));
    juce::AudioBuffer<float> b(a);
    played.processBlock(a, midi);
    fresh.processBlock(b, midi);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i) REQUIRE(a.getSample(ch, i) == b.getSample(ch, i));
}
}

TEST_CASE("[bus] after the host's reset the next play is a fresh processor's") {
    using lesson::index;
    using lesson::CompParam;
    using lesson::EqParam;
    checkReset<EqProcessor>({ { index(EqParam::B1Freq), 20.0f }, { index(EqParam::B1Gain), 18.0f }, { index(EqParam::B1Q), 12.0f } });
    checkReset<CompProcessor>(
        { { index(CompParam::Threshold), -40.0f }, { index(CompParam::Ratio), 20.0f }, { index(CompParam::Release), 1000.0f } });
}

TEST_CASE("[bus] the EQ's tail is its settings' ring; the comp has none") {
    using lesson::index;
    using lesson::EqParam;
    EqProcessor eq;
    eq.prepareToPlay(48000.0, 512);
    CHECK(eq.getTailLengthSeconds() == 0.0);
    setPlain(eq, { { index(EqParam::B1Freq), 20.0f }, { index(EqParam::B1Gain), 18.0f }, { index(EqParam::B1Q), 12.0f } });
    // the bell's poles fall 16 dB a second: 120 dB and its 18 dB in about 8.6 s, and the margin
    CHECK(eq.getTailLengthSeconds() > 10.0);
    CHECK(eq.getTailLengthSeconds() < 11.5);
    CompProcessor comp;
    setPlain(comp, { { lesson::index(lesson::CompParam::Release), 1000.0f } });
    CHECK(comp.getTailLengthSeconds() == 0.0);
}
