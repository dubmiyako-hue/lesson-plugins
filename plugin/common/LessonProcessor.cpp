#include "LessonProcessor.h"
#include "LessonParameters.h"
#include "WebEditor.h"

namespace lesson::plugin {

static_assert(std::atomic<float>::is_always_lock_free);

LessonProcessor::LessonProcessor(juce::String name, std::span<const ParamSpec> specs, const juce::Identifier& stateType)
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      name_(std::move(name)), specs_(specs), parameters_(*this, nullptr, stateType, makeLayout(specs)) {
    raw_.reserve(specs.size());
    params_.reserve(specs.size());
    for (const auto& spec : specs) {
        const auto id = asJuce(spec.id);
        raw_.push_back(parameters_.getRawParameterValue(id));
        params_.push_back(parameters_.getParameter(id));
        if (spec.id == "bypass") bypass_ = parameters_.getParameter(id);
    }
}

bool LessonProcessor::isBusesLayoutSupported(const BusesLayout& layout) const {
    const auto output = layout.getMainOutputChannelSet();
    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo()) return false;
    return layout.getMainInputChannelSet() == output;
}

juce::AudioProcessorParameter* LessonProcessor::getBypassParameter() const {
    return bypass_;
}

juce::AudioProcessorEditor* LessonProcessor::createEditor() {
    return new WebEditor(*this);
}

void LessonProcessor::getStateInformation(juce::MemoryBlock& destData) {
    const auto state = parameters_.copyState();
    const auto xml = state.createXml();
    if (xml == nullptr) return;
    xml->setAttribute("version", kStateVersion);
    copyXmlToBinary(*xml, destData);
}

void LessonProcessor::setStateInformation(const void* data, int sizeInBytes) {
    // Read by id, whatever the version: a parameter the state lacks takes its default.
    const auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName(parameters_.state.getType())) return;
    xml->removeAttribute("version");
    parameters_.replaceState(juce::ValueTree::fromXml(*xml));
}

} // namespace lesson::plugin
