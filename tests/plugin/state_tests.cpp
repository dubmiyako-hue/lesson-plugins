// AC-14: the state holds every parameter and a version, and reading it back restores them.
#include <doctest.h>
#include <comp/CompProcessor.h>
#include <eq/EqProcessor.h>
#include <cmath>
#include <memory>
#include <random>

using namespace lesson::plugin;

namespace {
template <typename Processor>
void roundTrip(unsigned seed) {
    Processor source;
    std::mt19937 random(seed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    for (auto* p : source.getParameters()) p->setValueNotifyingHost(unit(random));

    juce::MemoryBlock block;
    source.getStateInformation(block);
    const auto xml = juce::AudioProcessor::getXmlFromBinary(block.getData(), static_cast<int>(block.getSize()));
    REQUIRE(xml != nullptr);
    CHECK(xml->getIntAttribute("version") == LessonProcessor::kStateVersion);
    for (auto* p : source.getParameters()) {
        const auto id = dynamic_cast<juce::RangedAudioParameter*>(p)->getParameterID();
        CHECK_MESSAGE(xml->getChildByAttribute("id", id) != nullptr, id);
    }

    Processor target;
    target.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    const auto& from = source.getParameters();
    const auto& to = target.getParameters();
    REQUIRE(from.size() == to.size());
    int moved = 0;
    float worst = 0.0f;
    for (int i = 0; i < from.size(); ++i) {
        auto* a = dynamic_cast<juce::RangedAudioParameter*>(from[i]);
        auto* b = dynamic_cast<juce::RangedAudioParameter*>(to[i]);
        const float va = a->convertFrom0to1(a->getValue());
        const float vb = b->convertFrom0to1(b->getValue());
        INFO(a->getParameterID());
        CHECK(a->getText(a->getValue(), 128) == b->getText(b->getValue(), 128));
        if (va != vb) ++moved;
        worst = std::max(worst, std::fabs(va - vb) / std::max(1.0f, std::fabs(va)));
        CHECK(vb == va);
    }
    MESSAGE("seed " << seed << ": " << moved << " of " << from.size() << " values moved, largest relative change " << worst);
}
}

TEST_CASE("[state] Lesson EQ's state restores every parameter") {
    for (unsigned seed = 1; seed <= 20; ++seed) roundTrip<EqProcessor>(seed);
}

TEST_CASE("[state] Lesson Comp's state restores every parameter") {
    for (unsigned seed = 1; seed <= 20; ++seed) roundTrip<CompProcessor>(seed);
}

TEST_CASE("[state] a parameter the state lacks takes its default, a foreign state changes nothing") {
    EqProcessor source;
    source.parameters().getParameter("b1_gain")->setValueNotifyingHost(0.75f);
    source.parameters().getParameter("hs_gain")->setValueNotifyingHost(0.75f);
    juce::MemoryBlock block;
    source.getStateInformation(block);
    auto xml = juce::AudioProcessor::getXmlFromBinary(block.getData(), static_cast<int>(block.getSize()));
    REQUIRE(xml != nullptr);
    xml->removeChildElement(xml->getChildByAttribute("id", "hs_gain"), true);
    juce::MemoryBlock trimmed;
    juce::AudioProcessor::copyXmlToBinary(*xml, trimmed);

    EqProcessor target;
    target.parameters().getParameter("hs_gain")->setValueNotifyingHost(0.25f);
    target.setStateInformation(trimmed.getData(), static_cast<int>(trimmed.getSize()));
    CHECK(target.parameters().getParameter("b1_gain")->getValue() == doctest::Approx(0.75f));
    CHECK(target.parameters().getParameter("hs_gain")->getValue() == doctest::Approx(0.5f)); // 0 dB

    CompProcessor comp;
    comp.parameters().getParameter("comp_ratio")->setValueNotifyingHost(0.3f);
    comp.setStateInformation(block.getData(), static_cast<int>(block.getSize())); // Lesson EQ's state
    CHECK(comp.parameters().getParameter("comp_ratio")->getValue() == doctest::Approx(0.3f));
    const char garbage[] = "not a state";
    comp.setStateInformation(garbage, sizeof garbage);
    CHECK(comp.parameters().getParameter("comp_ratio")->getValue() == doctest::Approx(0.3f));
}
