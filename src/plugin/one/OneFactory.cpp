// Plugin entry point for Moduledrum (kept out of OneProcessor.cpp so the dev tools can link it).
#include "OneProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new md::plugin::one::MdOneProcessor(); }
