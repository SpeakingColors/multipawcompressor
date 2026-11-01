#pragma once
#include "../plugin/PluginProcessor.h"

// TEMPORARY development stand-in: a plain parameter list. The native port of the approved
// mockup window replaces this before any release.
class MultiPawEditor : public juce::GenericAudioProcessorEditor
{
public:
    explicit MultiPawEditor (MultiPawProcessor& p) : juce::GenericAudioProcessorEditor (p) { setSize (720, 640); }
};
