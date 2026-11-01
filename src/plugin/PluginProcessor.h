#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../core/MultiPawCore.h"
#include "ParamTable.h"
#include <array>
#include <atomic>

// Parameter ids are the ones of the approved mockup (see tools/params.json). Never rename them.
namespace setting
{
    inline constexpr const char* look = "look";       // "A", "B", "C"
    inline constexpr const char* tips = "tips";       // info bubbles
    inline constexpr const char* power = "power";     // power saving redraw
    inline constexpr const char* scale = "scale";     // window scale, 1.0 = 100 %
    inline constexpr const char* page = "page";       // 0 compressor, 1 sidechain
    inline constexpr const char* preset1 = "preset1"; // name of the loaded preset, page 1
    inline constexpr const char* preset2 = "preset2"; // page 2
}

class MultiPawProcessor : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    MultiPawProcessor();
    ~MultiPawProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "MultiPaw Compressor"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // Meters for the editor (written on the audio thread, read by the editor timer).
    struct Meters
    {
        std::atomic<float> prePk[2] { { 0 }, { 0 } }, postPk[2] { { 0 }, { 0 } }, preMs[2] { { 0 }, { 0 } }, postMs[2] { { 0 }, { 0 } }, limGr { 0 };
        std::atomic<float> bandGr[4] { { 0 }, { 0 }, { 0 }, { 0 } }, bandLvl[4] { { -90 }, { -90 }, { -90 }, { -90 } }, bandLim[4] { { 0 }, { 0 }, { 0 }, { 0 } };
        std::atomic<float> scLow { 1 }, scHigh { 1 };
    } meters;

    // Mono sample rings (input and output) for the spectrum view; written on the audio thread.
    static constexpr int kRing = 4096;
    std::array<float, kRing> ringPre {}, ringPost {};
    std::atomic<int> ringPos { 0 };
    double sampleRateNow = 48000.0;
    std::atomic<double> bpmNow { 120.0 };

    // Dynamic pattern curves (DPC), nodes of the four bands: [x 0..1 of the cycle, y 0..1 (1 = untouched)].
    using Nodes = std::vector<std::pair<float, float>>;
    Nodes getPattern (int band) const;
    void setPattern (int band, const Nodes& n);

    juce::var getSetting (const char* key, const juce::var& fallback) const;
    void setSetting (const char* key, const juce::var& v);
    int latencyNow() const { return engine.latencySamples(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void handleAsyncUpdate() override { setLatencySamples (reportedLatency.load()); updateHostDisplay (ChangeDetails().withLatencyChanged (true)); }
    void loadPatternsFromState();
    void rebuildLut (int band);

    multipaw::Engine engine;
    multipaw::MbcP mbc;
    multipaw::ScP sc;
    std::array<std::atomic<float>*, mp::kNumParams> raw {};
    float vals[mp::kNumParams] {};
    bool bypassNow = false;
    std::array<Nodes, 4> patterns;
    std::array<std::array<float, multipaw::DPC_LUT>, 4> lutShadow;
    std::atomic<int> lutVersion { 0 };
    int lutApplied = -1;
    bool lutCopy = false;
    std::atomic<int> reportedLatency { 0 };
    int lastLatency = -1;
    std::vector<int> midiOffsets;
    juce::SpinLock patternLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultiPawProcessor)
};
