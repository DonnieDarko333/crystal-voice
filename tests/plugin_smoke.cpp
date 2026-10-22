// Rauchtest: Processor wie ein Host betreiben (ohne Editor).
#include "../Source/PluginProcessor.h"
#include <cstdio>
#include <random>

struct FakePlayHead : juce::AudioPlayHead {
    long long pos = 0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo i;
        i.setIsPlaying(playing);
        i.setTimeInSamples(pos);
        return i;
    }
};

static void writeWav(const juce::File& f, const std::vector<float>& x, double sr) {
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os(f.createOutputStream().release());
    std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(os.get(), sr, 1, 24, {}, 0));
    if (!w) { std::printf("WAV-Writer fehlgeschlagen\n"); return; }
    os.release();
    const float* ch[1] = { x.data() };
    w->writeFromFloatArrays(ch, 1, (int) x.size());
}

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[%s] %s\n", ok ? " OK " : "FEHL", what);
    if (!ok) ++fails;
}

int main() {
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0;
    const int block = 256;
    const int secs = 6;

    // Testdateien: Referenz (Rauschimpulse als "Gesang"), Raum-IR (Direkt + Abklingen)
    std::mt19937 rng(5);
    std::normal_distribution<float> nd(0.f, 1.f);
    std::vector<float> ref((size_t) (sr * secs), 0.f), ir((size_t) (sr * 1.5), 0.f);
    for (size_t i = 0; i < ref.size(); ++i) {
        const double t = (double) i / sr;
        if (std::fmod(t, 1.5) < 0.8) ref[i] = 0.2f * nd(rng);
    }
    ir[100] = 1.f;
    for (size_t i = 700; i < ir.size(); ++i) ir[i] = 0.05f * nd(rng) * (float) std::exp(-(double) i / (0.3 * sr));
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    const auto refF = dir.getChildFile("gh_ref.wav"), irF = dir.getChildFile("gh_ir.wav");
    writeWav(refF, ref, sr);
    writeWav(irF, ir, 44100.0); // absichtlich andere Samplerate

    CrystalVoiceProcessor p;
    check(p.getTotalNumInputChannels() == 3, "Busse: Mikro mono + Sidechain stereo");
    check(p.getTotalNumOutputChannels() == 1, "Ausgang mono");
    check(p.getLatencySamples() == 0, "Latenz 0 Samples");

    FakePlayHead ph;
    p.setPlayHead(&ph);
    p.prepareToPlay(sr, block);

    p.loadRoomIr(irF);
    p.loadReference(refF);
    for (int i = 0; i < 100 && !p.getStatus().startsWith("Bereit"); ++i) juce::Thread::sleep(50);
    check(p.getStatus().startsWith("Bereit"), ("Status: " + p.getStatus()).toRawUTF8());

    p.params.getParameter("vocal")->setValueNotifyingHost(1.0f);
    p.params.getParameter("instrumental")->setValueNotifyingHost(1.0f);

    // Mikro = Referenz (live gesungen) + Playback-Übersprechen; Sidechain = Playback
    juce::AudioBuffer<float> buf(3, block);
    juce::MidiBuffer midi;
    bool finite = true, everActive = false;
    double eIn = 0, eOut = 0;
    for (long long pos = 0; pos + block <= (long long) ref.size(); pos += block) {
        ph.pos = pos;
        for (int i = 0; i < block; ++i) {
            const float pb = 0.3f * std::sin(0.05f * (float) (pos + i)) + 0.1f * nd(rng);
            const float delayed = pos + i >= 900 ? 0.3f * std::sin(0.05f * (float) (pos + i - 900)) : 0.f;
            buf.setSample(0, i, ref[(size_t) (pos + i)] + 0.5f * delayed);
            buf.setSample(1, i, pb);
            buf.setSample(2, i, pb);
        }
        const bool late = pos > (long long) (sr * 3);
        if (late) for (int i = 0; i < block; ++i) eIn += buf.getSample(0, i) * buf.getSample(0, i);
        p.processBlock(buf, midi);
        everActive |= p.isVocalStageActive();
        for (int i = 0; i < block; ++i) {
            const float v = buf.getSample(0, i);
            finite &= std::isfinite(v);
            if (late) eOut += v * v;
        }
    }
    check(finite, "Ausgang durchgehend endlich");
    check(p.isSidechainConnected(), "Sidechain erkannt");
    check(p.isModelAccepted(), "Modell passt zur Host-Samplerate");
    check(everActive, "Raumkorrektur lief mit Transport");
    std::printf("       Pegeländerung gesamt (nach 3 s): %.1f dB, Playback-Anzeige %.1f dB\n",
                10.0 * std::log10(eOut / eIn), p.getPlaybackReductionDb());

    // Zustand speichern/laden
    juce::MemoryBlock mb;
    p.getStateInformation(mb);
    CrystalVoiceProcessor q;
    q.prepareToPlay(sr, block);
    q.setStateInformation(mb.getData(), (int) mb.getSize());
    for (int i = 0; i < 100 && !q.getStatus().startsWith("Bereit"); ++i) juce::Thread::sleep(50);
    check(q.params.getRawParameterValue("vocal")->load() == 100.f, "Parameter wiederhergestellt");
    check(q.getStatus().startsWith("Bereit"), "Dateien nach Neustart wieder geladen");

    // Bypass-Stellung muss Signal unverändert durchlassen
    q.params.getParameter("vocal")->setValueNotifyingHost(0.0f);
    q.params.getParameter("instrumental")->setValueNotifyingHost(0.0f);
    // Erwartung: Entstörung neutral, nur die Klangstufe (Mikrofon + Presence) wirkt
    q.params.getParameter("mic")->setValueNotifyingHost(q.params.getParameter("mic")->convertTo0to1(2.0f));
    q.params.getParameter("presence")->setValueNotifyingHost(1.0f);
    gh::ToneStage ref2; ref2.prepare(sr); ref2.set(2, true);
    float maxDiff = 0.f;
    for (int k = 0; k < 50; ++k) {
        for (int i = 0; i < block; ++i) { buf.setSample(0, i, nd(rng)); buf.setSample(1, i, nd(rng)); buf.setSample(2, i, 0.f); }
        juce::AudioBuffer<float> copy(buf);
        q.processBlock(buf, midi);
        ref2.process(copy.getWritePointer(0), block);
        for (int i = 0; i < block; ++i) maxDiff = std::max(maxDiff, std::fabs(buf.getSample(0, i) - copy.getSample(0, i)));
    }
    check(maxDiff == 0.f, "0/0: Entstörung bitgenau neutral, nur Klangstufe wirkt");
    check((int) q.params.getRawParameterValue("mic")->load() == 2, "Mikrofon-Schalter auf KMS 105");

    // --- Raummessung: Ausgang → Lautsprecher → Halle (T60 1,0 s) → Mikro, 5 ms Latenz ---
    {
        CrystalVoiceProcessor mp;
        FakePlayHead mph;
        mph.playing = true;
        mp.setPlayHead(&mph);
        mp.prepareToPlay(sr, block);
        juce::AudioBuffer<float> mb(3, block);

        // Bei laufendem Transport muss die Messung abgelehnt werden
        mp.startMeasurement();
        mb.clear();
        mp.processBlock(mb, midi);
        check(mp.getMeasureState() == CrystalVoiceProcessor::Idle && mp.wasMeasureRefused(), "Messung bei laufendem Transport abgelehnt");

        mph.playing = false;
        // dünn besetzte Halle: Direktschall + 3000 abklingende Reflexionen
        std::vector<std::pair<int, float>> taps { { 0, 1.0f } };
        std::mt19937 r2(11);
        std::uniform_real_distribution<float> ud(0.012f, 2.2f);
        for (int k = 0; k < 3000; ++k) {
            const float t = ud(r2);
            taps.push_back({ (int) (t * sr), 0.05f * nd(r2) * std::exp(-6.91f * t / 1.0f) });
        }
        const int lat = (int) (0.005 * sr);
        std::vector<float> outHist(1 << 18, 0.f);
        const size_t mask = outHist.size() - 1;
        size_t n = 0;
        mp.startMeasurement();
        int guard = 0;
        while (mp.getMeasureState() != CrystalVoiceProcessor::Idle && guard++ < 6000) {
            if (mp.getMeasureState() == CrystalVoiceProcessor::Running || mp.getMeasureState() == CrystalVoiceProcessor::Requested) {
                for (int i = 0; i < block; ++i) {
                    float v = 0.f;
                    for (auto& t : taps) {
                        const long long idx = (long long) (n + i) - lat - t.first;
                        if (idx >= 0 && (size_t) ((long long) (n + i) - idx) < outHist.size()) v += t.second * outHist[(size_t) idx & mask];
                    }
                    mb.setSample(0, i, 0.5f * v + 1e-5f * nd(rng));
                    mb.setSample(1, i, 0.f);
                    mb.setSample(2, i, 0.f);
                }
                mp.processBlock(mb, midi);
                for (int i = 0; i < block; ++i) outHist[(n + i) & mask] = mb.getSample(0, i);
                n += block;
            } else {
                { juce::Thread::sleep(50); juce::Timer::callPendingTimersSynchronously(); } // Timer → Analyse
            }
        }
        for (int i = 0; i < 100 && !mp.getStatus().startsWith("Raum"); ++i) { juce::Thread::sleep(50); juce::Timer::callPendingTimersSynchronously(); }
        std::printf("       Status nach Messung: %s\n", mp.getStatus().toRawUTF8());
        const auto st = mp.getStatus();
        const float t60 = st.fromFirstOccurrenceOf("T60 ", false, false).upToFirstOccurrenceOf(" s", false, false).getFloatValue();
        check(st.contains("gemessen") && std::abs(t60 - 1.0f) <= 0.15f, "Gemessene Halle: T60 = 1,0 s (±0,15)");

        const auto dir = juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("Crystal Voice");
        const auto files = dir.findChildFiles(juce::File::findFiles, false, "Raum-IR *.wav");
        check(!files.isEmpty(), "IR als WAV gespeichert");
        for (auto& f : files) f.deleteFile();
    }

    refF.deleteFile();
    irF.deleteFile();
    std::printf("\n%s\n", fails == 0 ? "Alle Prüfungen bestanden." : "FEHLER vorhanden.");
    return fails;
}
