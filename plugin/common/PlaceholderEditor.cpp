#include "PlaceholderEditor.h"

namespace lesson::plugin {

PlaceholderEditor::PlaceholderEditor(juce::AudioProcessor& processor) : AudioProcessorEditor(processor) {
    label_.setText(processor.getName(), juce::dontSendNotification);
    label_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label_);
    setSize(720, 420);
}

void PlaceholderEditor::paint(juce::Graphics& g) {
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void PlaceholderEditor::resized() {
    label_.setBounds(getLocalBounds());
}

} // namespace lesson::plugin
