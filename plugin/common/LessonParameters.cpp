#include "LessonParameters.h"
#include <memory>

namespace lesson::plugin {

juce::String asJuce(std::string_view text) {
    return juce::String::fromUTF8(text.data(), static_cast<int>(text.size()));
}

juce::NormalisableRange<float> rangeFor(const Range& range) {
    // The snap only clamps (the step is fromNorm's): a value already legal stays as it is.
    return { static_cast<float>(lowest(range)), static_cast<float>(highest(range)),
             [range](float, float, float n) { return static_cast<float>(fromNorm(range, n)); },
             [range](float, float, float v) { return static_cast<float>(toNorm(range, v)); },
             [range](float, float, float v) { return static_cast<float>(legalValue(range, v)); } };
}

namespace {
// The host asks for the title with 128 characters and for the short title with 8 (VST3 shortTitle). The short title is
// always the table's (B1 Q next to B1 Freq), even where the name would fit in 8 (Bell 1 Q).
juce::String nameFor(const juce::String& name, const juce::String& shortName, int maximumStringLength) {
    if (maximumStringLength > 8 && name.length() <= maximumStringLength) return name;
    return shortName.substring(0, maximumStringLength);
}
juce::String cut(const juce::String& text, int maximumStringLength) {
    return maximumStringLength > 0 ? text.substring(0, maximumStringLength) : text;
}

// The texts are the core's (lesson::valueText, lesson::parseValue), the same as the screen's; a text that cannot be read
// leaves the parameter where it is. The label stays empty: the unit is in the text (Hz becomes kHz).
class FloatParameter final : public juce::AudioParameterFloat {
public:
    explicit FloatParameter(const ParamSpec& spec)
        : AudioParameterFloat(juce::ParameterID { asJuce(spec.id), 1 }, asJuce(spec.name), rangeFor(spec.range),
                              static_cast<float>(spec.defaultValue), juce::AudioParameterFloatAttributes().withLabel({})),
          spec_(spec), shortName_(asJuce(spec.shortName)) {}
    juce::String getName(int maximumStringLength) const override {
        return nameFor(name, shortName_, maximumStringLength);
    }
    juce::String getText(float normalised, int maximumStringLength) const override {
        return cut(asJuce(valueText(spec_, convertFrom0to1(normalised))), maximumStringLength);
    }
    float getValueForText(const juce::String& text) const override {
        const auto value = parseValue(spec_, text.toStdString());
        return convertTo0to1(value ? static_cast<float>(*value) : get());
    }

private:
    const ParamSpec& spec_;
    juce::String shortName_;
};

class BoolParameter final : public juce::AudioParameterBool {
public:
    explicit BoolParameter(const ParamSpec& spec)
        : AudioParameterBool(juce::ParameterID { asJuce(spec.id), 1 }, asJuce(spec.name), spec.defaultValue >= 0.5),
          spec_(spec), shortName_(asJuce(spec.shortName)) {}
    juce::String getName(int maximumStringLength) const override {
        return nameFor(name, shortName_, maximumStringLength);
    }
    juce::String getText(float normalised, int maximumStringLength) const override {
        return cut(asJuce(valueText(spec_, normalised >= 0.5f ? 1.0 : 0.0)), maximumStringLength);
    }
    float getValueForText(const juce::String& text) const override {
        const auto value = parseValue(spec_, text.toStdString());
        return value ? static_cast<float>(*value) : (get() ? 1.0f : 0.0f);
    }

private:
    const ParamSpec& spec_;
    juce::String shortName_;
};

juce::StringArray choicesOf(const ParamSpec& spec) {
    juce::StringArray choices;
    for (const auto choice : spec.choices) choices.add(asJuce(choice));
    return choices;
}

class ChoiceParameter final : public juce::AudioParameterChoice {
public:
    explicit ChoiceParameter(const ParamSpec& spec)
        : AudioParameterChoice(juce::ParameterID { asJuce(spec.id), 1 }, asJuce(spec.name), choicesOf(spec),
                               static_cast<int>(spec.defaultValue)),
          spec_(spec), shortName_(asJuce(spec.shortName)) {}
    juce::String getName(int maximumStringLength) const override {
        return nameFor(name, shortName_, maximumStringLength);
    }
    juce::String getText(float normalised, int maximumStringLength) const override {
        return cut(asJuce(valueText(spec_, convertFrom0to1(normalised))), maximumStringLength);
    }
    float getValueForText(const juce::String& text) const override {
        const auto value = parseValue(spec_, text.toStdString());
        return convertTo0to1(value ? static_cast<float>(*value) : static_cast<float>(getIndex()));
    }

private:
    const ParamSpec& spec_;
    juce::String shortName_;
};
}

juce::AudioProcessorValueTreeState::ParameterLayout makeLayout(std::span<const ParamSpec> specs) {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& spec : specs) {
        switch (spec.kind) {
        case Kind::Float: layout.add(std::make_unique<FloatParameter>(spec)); break;
        case Kind::Bool: layout.add(std::make_unique<BoolParameter>(spec)); break;
        case Kind::Choice: layout.add(std::make_unique<ChoiceParameter>(spec)); break;
        }
    }
    return layout;
}

} // namespace lesson::plugin
