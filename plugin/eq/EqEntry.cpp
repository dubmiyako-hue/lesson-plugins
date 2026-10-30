#include "EqProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new lesson::plugin::EqProcessor();
}
