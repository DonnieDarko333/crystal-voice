// Rendert den Editor in eine PNG-Datei (Layout-Kontrolle ohne DAW).
// Vorher läuft etwas Audio durch, damit Instrumente und Lampen einen Zustand zeigen.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <random>

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0;
    const int block = 256;
    CrystalVoiceProcessor p;
    p.prepareToPlay(sr, block);
    p.params.getParameter("vocal")->setValueNotifyingHost(0.7f);
    p.params.getParameter("instrumental")->setValueNotifyingHost(0.85f);
    p.params.getParameter("presence")->setValueNotifyingHost(1.0f);
    p.params.getParameter("sync")->setValueNotifyingHost(p.params.getParameter("sync")->convertTo0to1(-6.5f));
    if (argc > 2) p.params.getParameter("mic")->setValueNotifyingHost(1.0f);

    // Mikro = Stimme + verzögertes Playback, Sidechain = Playback
    std::mt19937 rng(3);
    std::normal_distribution<float> nd(0.f, 1.f);
    std::vector<float> hist(4096, 0.f);
    juce::AudioBuffer<float> buf(3, block);
    juce::MidiBuffer midi;
    size_t hp = 0;
    for (int b = 0; b < (int) (6.0 * sr / block); ++b) {
        for (int i = 0; i < block; ++i) {
            const float pb = 0.25f * nd(rng);
            hist[hp % hist.size()] = pb;
            const float delayed = hist[(hp + hist.size() - 700) % hist.size()];
            ++hp;
            const float t = (float) (b * block + i) / (float) sr;
            const float voice = 0.30f * std::sin(6.2832f * 220.f * t) * (std::fmod(t, 2.f) < 1.4f ? 1.f : 0.f);
            buf.setSample(0, i, voice + 0.5f * delayed);
            buf.setSample(1, i, pb);
            buf.setSample(2, i, pb);
        }
        p.processBlock(buf, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    ed->setVisible(true);
    auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f);
    juce::File out(argc > 1 ? juce::String(argv[1]) : juce::String("editor.png"));
    out.deleteFile();
    juce::FileOutputStream os(out);
    juce::PNGImageFormat().writeImageToStream(img, os);
    return 0;
}
