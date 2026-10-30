#pragma once
// The JUCE parameters built from the core's tables (lesson::kEqParams, lesson::kCompParams): the web app's 0..1 mapping,
// its value texts and the 8-character short names.
#include <juce_audio_processors/juce_audio_processors.h>
#include <lesson/Params.h>
#include <span>

namespace lesson::plugin {

juce::String asJuce(std::string_view text);

// The range of a Float parameter: params.ts toNorm and fromNorm, with 0 below the zero notch and infValue above the inf
// notch, so that the host's 0..1 is the web app's knob position.
juce::NormalisableRange<float> rangeFor(const Range& range);

juce::AudioProcessorValueTreeState::ParameterLayout makeLayout(std::span<const ParamSpec> specs);

} // namespace lesson::plugin
