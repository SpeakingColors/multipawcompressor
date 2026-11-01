// Renders the real editor offscreen to a PNG so it can be compared with the mockup.
// usage: MultiPawSnapshot out.png [page 0|1] [scale] [look A|B|C] [frames]
#include "../src/plugin/PluginProcessor.h"
#include "../src/editor/PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 2) return 1;
    MultiPawProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    proc.setSetting (setting::page, argc > 2 ? juce::String (argv[2]).getIntValue() : 0);
    if (argc > 4) proc.setSetting (setting::look, juce::String (argv[4]));
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    const double sc = argc > 3 ? juce::String (argv[3]).getDoubleValue() : 1.0;
    ed->setSize (juce::roundToInt (1280 * sc), juce::roundToInt (680 * sc));
    const int frames = argc > 5 ? juce::String (argv[5]).getIntValue() : 60;
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    juce::Random rnd (7);
    long pos = 0;
    for (int f = 0; f < frames; ++f)
    {
        for (int i = 0; i < 512; ++i, ++pos)
        {
            const double t = (double) pos / 48000.0, tk = std::fmod (t, 0.5), th = std::fmod (t + 0.25, 0.5);
            float v = (float) (0.85 * std::exp (-tk * 14.0) * std::sin (6.2831853 * (52.0 + 90.0 * std::exp (-tk * 40.0)) * tk));
            v += (float) (0.22 * std::exp (-th * 45.0) * (rnd.nextFloat() * 2.0f - 1.0f));
            v += 0.12f * (float) std::sin (6.2831853 * 220.0 * t);
            buf.setSample (0, i, v); buf.setSample (1, i, v * 0.97f);
        }
        proc.processBlock (buf, midi);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (16);
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
    juce::Image img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 1.0f);
    juce::File out (argv[1]);
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat().writeImageToStream (img, os);
    ed.reset();
    return 0;
}
