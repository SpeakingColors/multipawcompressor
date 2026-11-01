#include "PluginProcessor.h"
#include "ParamMap.h"
#include "../editor/PluginEditor.h"

using namespace juce;

AudioProcessorValueTreeState::ParameterLayout MultiPawProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    const int v = 1; // parameter version
    for (const auto& d : mp::kParams)
    {
        const String id (d.id);
        if (d.isInt && d.mx - d.mn <= 1.0f)
            p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, v }, d.name, d.df > 0.5f));
        else if (d.isInt)
        {
            StringArray names; for (int i = (int) d.mn; i <= (int) d.mx; ++i) names.add (String (i));
            p.push_back (std::make_unique<AudioParameterInt> (ParameterID { id, v }, d.name, (int) d.mn, (int) d.mx, (int) d.df));
        }
        else
        {
            NormalisableRange<float> r (d.mn, d.mx, 0.001f);
            if (d.log)
            {
                const float lo = d.mn, ratio = d.mx / d.mn;
                r = NormalisableRange<float> (lo, d.mx,
                    [lo, ratio] (float, float, float n) { return lo * std::pow (ratio, n); },
                    [lo, ratio] (float, float, float x) { return std::log (x / lo) / std::log (ratio); },
                    [lo, mx = d.mx] (float, float, float x) { return jlimit (lo, mx, x); });
            }
            p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, v }, d.name, r, d.df));
        }
    }
    return { p.begin(), p.end() };
}

MultiPawProcessor::MultiPawProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "MULTIPAW", createLayout())
{
    for (int i = 0; i < mp::kNumParams; ++i) raw[(size_t) i] = apvts.getRawParameterValue (mp::kParams[i].id);
    for (auto& l : lutShadow) l.fill (1.0f);
    for (auto& n : patterns) n = { { 0.0f, 1.0f }, { 1.0f, 1.0f } };
}

bool MultiPawProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != AudioChannelSet::stereo() || out != l.getMainInputChannelSet()) return false;
    const auto sidech = l.getChannelSet (true, 1);
    return sidech.isDisabled() || sidech == AudioChannelSet::stereo() || sidech == AudioChannelSet::mono();
}

var MultiPawProcessor::getSetting (const char* key, const var& fallback) const { return apvts.state.getProperty (key, fallback); }
void MultiPawProcessor::setSetting (const char* key, const var& v) { apvts.state.setProperty (key, v, nullptr); }

MultiPawProcessor::Nodes MultiPawProcessor::getPattern (int band) const { return patterns[(size_t) band]; }

void MultiPawProcessor::rebuildLut (int band)
{
    // Same curve as the mockup (buildLut): cosine ease between sorted nodes.
    auto n = patterns[(size_t) band];
    std::sort (n.begin(), n.end(), [] (auto& a, auto& b) { return a.first < b.first; });
    if (n.size() < 2) n = { { 0.0f, 1.0f }, { 1.0f, 1.0f } };
    auto& lut = lutShadow[(size_t) band];
    size_t j = 0;
    for (int k = 0; k <= 512; ++k)
    {
        const float x = (float) k / 512.0f;
        while (j + 2 < n.size() && n[j + 1].first < x) ++j;
        const auto a = n[j], b = n[std::min (j + 1, n.size() - 1)];
        const float dd = b.first - a.first, t = dd > 1e-6f ? jlimit (0.0f, 1.0f, (x - a.first) / dd) : 1.0f;
        const float u = 0.5f - 0.5f * std::cos (MathConstants<float>::pi * t);
        lut[(size_t) k] = a.second + (b.second - a.second) * u;
    }
}

void MultiPawProcessor::setPattern (int band, const Nodes& nodes)
{
    {
        const SpinLock::ScopedLockType l (patternLock);
        patterns[(size_t) band] = nodes;
        rebuildLut (band);
        lutVersion.fetch_add (1);
    }
    String s;
    for (auto& n : nodes) s << String (n.first, 4) << "," << String (n.second, 4) << ";";
    apvts.state.setProperty ("pat" + String (band), s, nullptr);
}

void MultiPawProcessor::loadPatternsFromState()
{
    for (int b = 0; b < 4; ++b)
    {
        Nodes n;
        for (auto& tok : StringArray::fromTokens (apvts.state.getProperty ("pat" + String (b), "").toString(), ";", ""))
        {
            auto xy = StringArray::fromTokens (tok, ",", "");
            if (xy.size() == 2) n.push_back ({ jlimit (0.0f, 1.0f, xy[0].getFloatValue()), jlimit (0.0f, 1.0f, xy[1].getFloatValue()) });
        }
        if (n.size() < 2) n = { { 0.0f, 1.0f }, { 1.0f, 1.0f } };
        const SpinLock::ScopedLockType l (patternLock);
        patterns[(size_t) b] = n;
        rebuildLut (b);
    }
    lutVersion.fetch_add (1);
}

void MultiPawProcessor::prepareToPlay (double sampleRate, int)
{
    sampleRateNow = sampleRate;
    engine.prepare (sampleRate);
    for (int i = 0; i < mp::kNumParams; ++i) vals[i] = raw[(size_t) i]->load();
    mp::applyParams (vals, mbc, sc, bypassNow);
    lutApplied = -1;
    engine.setMBC (mbc); engine.setSC (sc);
    lastLatency = engine.latencySamples();
    reportedLatency.store (lastLatency);
    setLatencySamples (lastLatency);
}

void MultiPawProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (getTotalNumOutputChannels() < 2 || buffer.getNumChannels() < 2) return;

    for (int i = 0; i < mp::kNumParams; ++i) vals[i] = raw[(size_t) i]->load();
    mp::applyParams (vals, mbc, sc, bypassNow);
    if (lutApplied != lutVersion.load())
    {
        const SpinLock::ScopedTryLockType l (patternLock);
        if (l.isLocked())
        {
            for (int b = 0; b < 4; ++b) std::copy (lutShadow[(size_t) b].begin(), lutShadow[(size_t) b].end(), mbc.b[b].lut);
            lutApplied = lutVersion.load();
            lutCopy = true;
        }
    }
    if (lutCopy) for (int b = 0; b < 4; ++b) std::copy (lutShadow[(size_t) b].begin(), lutShadow[(size_t) b].end(), mbc.b[b].lut);
    engine.setMBC (mbc); engine.setSC (sc);
    engine.bypass = bypassNow;
    const int lat = engine.latencySamples();
    if (lat != lastLatency) { lastLatency = lat; reportedLatency.store (lat); triggerAsyncUpdate(); }

    multipaw::HostInfo host;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            host.playing = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) host.ppq = *q;
            if (auto b = pos->getBpm()) { host.bpm = *b; bpmNow.store (*b); }
        }
    midiOffsets.clear();
    for (const auto m : midi) if (m.getMessage().isNoteOn()) midiOffsets.push_back (m.samplePosition);
    host.midiOn = midiOffsets.data(); host.nMidiOn = (int) midiOffsets.size();

    const float* scL = nullptr; const float* scR = nullptr;
    auto sideBus = getBusBuffer (buffer, true, 1);
    if (sideBus.getNumChannels() > 0) { scL = sideBus.getReadPointer (0); scR = sideBus.getReadPointer (sideBus.getNumChannels() > 1 ? 1 : 0); }

    {
        const float* il = buffer.getReadPointer (0); const float* ir = buffer.getReadPointer (1);
        int w = ringPos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) { ringPre[(size_t) w] = 0.5f * (il[i] + ir[i]); w = (w + 1) & (kRing - 1); }
    }
    engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n, scL, scR, host);
    {
        const float* ol = buffer.getReadPointer (0); const float* orr = buffer.getReadPointer (1);
        int w = ringPos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) { ringPost[(size_t) w] = 0.5f * (ol[i] + orr[i]); w = (w + 1) & (kRing - 1); }
        ringPos.store (w, std::memory_order_release);
    }

    for (int c = 0; c < 2; ++c) { meters.prePk[c].store (engine.prePk[c]); meters.postPk[c].store (engine.postPk[c]); meters.preMs[c].store (engine.preMs[c]); meters.postMs[c].store (engine.postMs[c]); }
    meters.limGr.store (engine.mGr);
    for (int b = 0; b < 4; ++b) { meters.bandGr[b].store (engine.bandGr[b]); meters.bandLvl[b].store (engine.bandLvl[b]); meters.bandLim[b].store (engine.bandLimGr[b]); }
    meters.scLow.store (engine.scLow); meters.scHigh.store (engine.scHigh);
}

AudioProcessorEditor* MultiPawProcessor::createEditor() { return new MultiPawEditor (*this); }

void MultiPawProcessor::getStateInformation (MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", 1, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void MultiPawProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (ValueTree::fromXml (*xml)); // unknown keys are ignored by APVTS
            loadPatternsFromState();
        }
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MultiPawProcessor(); }
