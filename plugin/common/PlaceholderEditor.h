#pragma once
// The editor until the WebView page arrives (cycle 3): the plug-in's name on a plain window.
#include <juce_audio_processors/juce_audio_processors.h>

namespace lesson::plugin {

class PlaceholderEditor final : public juce::AudioProcessorEditor {
public:
    explicit PlaceholderEditor(juce::AudioProcessor& processor);
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    juce::Label label_;
};

} // namespace lesson::plugin
