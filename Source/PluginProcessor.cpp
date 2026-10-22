#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
constexpr int kMaxChunk = 4096;
constexpr float kIrCutMs = 5.0f; // später als Direktschall + 5 ms gilt als Raum
}

CrystalVoiceProcessor::CrystalVoiceProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Mikrofon", juce::AudioChannelSet::mono(), true)
                         .withOutput("Ausgang", juce::AudioChannelSet::mono(), true)
                         .withInput("Instrumental", juce::AudioChannelSet::stereo(), true)),
      params(*this, nullptr, "CrystalVoice", createLayout()) {
    pVocal = params.getRawParameterValue("vocal");
    pInstr = params.getRawParameterValue("instrumental");
    pSync = params.getRawParameterValue("sync");
    pMic = params.getRawParameterValue("mic");
    pPresence = params.getRawParameterValue("presence");
    startTimer(100);
}

CrystalVoiceProcessor::~CrystalVoiceProcessor() {
    stopTimer();
    ++generation;
    pool.removeAllJobs(true, 10000);
    model.store(nullptr);
}

juce::AudioProcessorValueTreeState::ParameterLayout CrystalVoiceProcessor::createLayout() {
    using P = juce::AudioParameterFloat;
    using R = juce::NormalisableRange<float>;
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    const auto pct = juce::AudioParameterFloatAttributes().withLabel("%").withStringFromValueFunction(
        [](float v, int) { return juce::String(juce::roundToInt(v)); });
    l.add(std::make_unique<P>(juce::ParameterID { "vocal", 1 }, "Vocal", R(0.0f, 100.0f, 0.1f), 50.0f, pct));
    l.add(std::make_unique<P>(juce::ParameterID { "instrumental", 1 }, "Instrumental", R(0.0f, 100.0f, 0.1f), 50.0f, pct));
    l.add(std::make_unique<P>(juce::ParameterID { "sync", 1 }, "Sync", R(-100.0f, 100.0f, 0.1f), 0.0f,
                              juce::AudioParameterFloatAttributes().withLabel("ms")));
    l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "mic", 1 }, "Mikrofon",
                                                       juce::StringArray { "Heil PR 37", "Shure KSM9", "Neumann KMS 105" }, 0));
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "presence", 1 }, "Presence", false));
    return l;
}

bool CrystalVoiceProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo()) return false;
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (layouts.inputBuses.size() > 1) {
        const auto sc = layouts.getChannelSet(true, 1);
        if (!sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void CrystalVoiceProcessor::prepareToPlay(double sampleRate, int) {
    scratchMic.assign(kMaxChunk, 0.0f);
    scratchRef.assign(kMaxChunk, 0.0f);
    scratchOut.assign(kMaxChunk, 0.0f);
    engine.prepare(sampleRate);
    tone.prepare(sampleRate);
    setLatencySamples(0);

    measState.store(Idle); // laufende Messung abbrechen
    sweep = gh::IrMeasurement::makeSweep(sampleRate);
    recBuf.assign(gh::IrMeasurement::recordLength(sampleRate), 0.0f);
    measPre = gh::IrMeasurement::preLength(sampleRate);

    const double old = hostSr.exchange(sampleRate);
    auto* m = model.load();
    if (old != sampleRate || (m && m->sampleRate != sampleRate)) startRebuild();
}

void CrystalVoiceProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    auto mainIn = getBusBuffer(buffer, true, 0);
    auto mainOut = getBusBuffer(buffer, false, 0);

    const float* scCh[2] = { nullptr, nullptr };
    int scNum = 0;
    if (getBusCount(true) > 1 && getBus(true, 1)->isEnabled()) {
        auto sc = getBusBuffer(buffer, true, 1);
        scNum = juce::jmin(2, sc.getNumChannels());
        for (int c = 0; c < scNum; ++c) scCh[c] = sc.getReadPointer(c);
    }
    sidechainOk.store(scNum > 0);

    engine.setAmounts(pVocal->load() * 0.01f, pInstr->load() * 0.01f);
    engine.setSyncMs(pSync->load());
    tone.set((int) pMic->load(), pPresence->load() > 0.5f);

    bool playing = false;
    long long pos = 0;
    if (auto* ph = getPlayHead())
        if (auto info = ph->getPosition()) {
            playing = info->getIsPlaying();
            if (auto t = info->getTimeInSamples()) pos = (long long) *t;
        }

    // Messung starten: nur bei gestopptem Transport (Schutz vor einem Sweep mitten im Song)
    if (measState.load() == Requested) {
        if (playing) { measState.store(Idle); measRefused.store(true); }
        else { measPos.store(0); measState.store(Running); }
    }
    const bool measuring = measState.load() == Running;

    const gh::VocalModel* m = model.load();
    const int inCh = mainIn.getNumChannels();
    const int outCh = mainOut.getNumChannels();

    for (int off = 0; off < n; off += kMaxChunk) {
        const int c = juce::jmin(kMaxChunk, n - off);

        // Mikro (Eingang und Ausgang teilen sich ggf. denselben Speicher → erst kopieren)
        if (inCh == 0) std::fill(scratchMic.begin(), scratchMic.begin() + c, 0.0f);
        else {
            juce::FloatVectorOperations::copy(scratchMic.data(), mainIn.getReadPointer(0, off), c);
            for (int ch = 1; ch < inCh; ++ch)
                juce::FloatVectorOperations::add(scratchMic.data(), mainIn.getReadPointer(ch, off), c);
            if (inCh > 1) juce::FloatVectorOperations::multiply(scratchMic.data(), 1.0f / (float) inCh, c);
        }

        const float* ref = nullptr;
        if (scNum > 0) {
            juce::FloatVectorOperations::copy(scratchRef.data(), scCh[0] + off, c);
            if (scNum > 1) {
                juce::FloatVectorOperations::add(scratchRef.data(), scCh[1] + off, c);
                juce::FloatVectorOperations::multiply(scratchRef.data(), 0.5f, c);
            }
            ref = scratchRef.data();
        }

        double eIn = 0.0, eOut = 0.0;
        for (int i = 0; i < c; ++i) eIn += (double) scratchMic[(size_t) i] * scratchMic[(size_t) i];
        if (measuring) {
            // Mikro aufnehmen, Sweep ausgeben; Entstörung und Klangstufe ruhen
            size_t p = measPos.load();
            for (int i = 0; i < c; ++i, ++p) {
                const bool inRec = p < recBuf.size();
                if (inRec) recBuf[p] = scratchMic[(size_t) i];
                scratchOut[(size_t) i] = (p >= measPre && p - measPre < sweep.size()) ? sweep[p - measPre] : 0.0f;
            }
            measPos.store(p);
            if (p >= recBuf.size()) measState.store(Recorded);
        } else {
            engine.process(scratchMic.data(), ref, scratchOut.data(), c, playing, pos + off, m);
            tone.process(scratchOut.data(), c);
        }
        for (int i = 0; i < c; ++i) eOut += (double) scratchOut[(size_t) i] * scratchOut[(size_t) i];
        // VU-artige Integration (300 ms) für die Zeigerinstrumente
        const float a = 1.0f - std::exp(-(float) c / (float) (getSampleRate() * 0.3));
        msIn += a * ((float) (eIn / c) - msIn);
        msOut += a * ((float) (eOut / c) - msOut);

        for (int ch = 0; ch < outCh; ++ch)
            juce::FloatVectorOperations::copy(mainOut.getWritePointer(ch, off), scratchOut.data(), c);
    }

    playbackDb.store(engine.playbackReductionDb());
    inLevel.store(msIn);
    outLevel.store(msOut);
    vocalActive.store(engine.vocalStageActive());
    modelOk.store(engine.modelAccepted());
}

// ---------------------------------------------------------------------------------------
// Dateien laden und Modell im Hintergrund bauen

bool CrystalVoiceProcessor::readMono(const juce::File& f, std::vector<float>& out, double& sr, juce::String& err) {
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
    if (!r) { err = "Datei nicht lesbar: " + f.getFileName(); return false; }
    const auto len = r->lengthInSamples;
    if (len <= 0 || len > (juce::int64) (r->sampleRate * 60.0 * 20.0)) {
        err = juce::String::fromUTF8("Datei leer oder länger als 20 Minuten.");
        return false;
    }
    const int ch = (int) juce::jmax(1u, r->numChannels);
    juce::AudioBuffer<float> b(ch, (int) len);
    r->read(&b, 0, (int) len, 0, true, true);
    out.assign((size_t) len, 0.0f);
    for (int c = 0; c < ch; ++c)
        juce::FloatVectorOperations::add(out.data(), b.getReadPointer(c), (int) len);
    if (ch > 1) juce::FloatVectorOperations::multiply(out.data(), 1.0f / (float) ch, (int) len);
    sr = r->sampleRate;
    return true;
}

void CrystalVoiceProcessor::loadReference(const juce::File& f) {
    pool.addJob([this, f] {
        std::vector<float> d;
        double sr = 0.0;
        juce::String err;
        setStatus(juce::String::fromUTF8("Lade Referenz-Take …"));
        if (!readMono(f, d, sr, err)) { setStatus(err); return; }
        {
            const juce::ScopedLock sl(dataLock);
            refData = std::move(d);
            refSr = sr;
            refFile = f;
        }
        startRebuild();
    });
}

void CrystalVoiceProcessor::loadRoomIr(const juce::File& f) {
    pool.addJob([this, f] {
        std::vector<float> d;
        double sr = 0.0;
        juce::String err;
        setStatus(juce::String::fromUTF8("Lade Raum-IR …"));
        if (!readMono(f, d, sr, err)) { setStatus(err); return; }
        {
            const juce::ScopedLock sl(dataLock);
            irData = std::move(d);
            irSr = sr;
            irFile = f;
            if (!f.getFileName().startsWith("Raum-IR ")) roomInfo.clear();
        }
        startRebuild();
    });
}

void CrystalVoiceProcessor::startMeasurement() {
    int expected = Idle;
    measState.compare_exchange_strong(expected, Requested);
}

void CrystalVoiceProcessor::stopMeasurement() {
    int expected = Running;
    if (!measState.compare_exchange_strong(expected, Idle)) {
        expected = Requested;
        measState.compare_exchange_strong(expected, Idle);
    }
}

double CrystalVoiceProcessor::getMeasureSeconds() const {
    const double sr = hostSr.load();
    return sr > 0.0 ? (double) measPos.load() / sr : 0.0;
}

void CrystalVoiceProcessor::runAnalysis() {
    // Audio-Thread schreibt nicht mehr (Zustand Recorded/Analyzing)
    const double sr = hostSr.load();
    pool.addJob([this, sr] {
        setStatus(juce::String::fromUTF8("Berechne Impulsantwort …"));
        auto res = gh::IrMeasurement::analyze(recBuf, sweep, sr);
        if (res.ir.empty() || res.clipped) {
            setStatus(juce::String::fromUTF8("Messung verworfen: ") + juce::String::fromUTF8(res.warning.c_str()));
            measState.store(Idle);
            return;
        }
        // als WAV sichern, damit sie mit dem Projekt wieder geladen wird
        const auto dir = juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("Crystal Voice");
        dir.createDirectory();
        const auto file = dir.getChildFile("Raum-IR " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S") + ".wav");
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> os(file.createOutputStream().release());
            if (os != nullptr)
                if (auto* w = wav.createWriterFor(os.get(), sr, 1, 24, {}, 0)) {
                    os.release();
                    std::unique_ptr<juce::AudioFormatWriter> writer(w);
                    const float* ch[1] = { res.ir.data() };
                    writer->writeFromFloatArrays(ch, 1, (int) res.ir.size());
                }
        }
        juce::String info = "gemessen, T60 " + (res.t60 > 0.0f ? juce::String(res.t60, 1) + " s" : juce::String("?"))
                          + ", SNR " + juce::String(juce::roundToInt(res.snrDb)) + " dB";
        if (!res.warning.empty()) info << juce::String::fromUTF8(" – ") << juce::String::fromUTF8(res.warning.c_str());
        {
            const juce::ScopedLock sl(dataLock);
            irData = std::move(res.ir);
            irSr = sr;
            irFile = file;
            roomInfo = info;
        }
        measState.store(Idle);
        startRebuild();
    });
}

void CrystalVoiceProcessor::startRebuild() {
    const int gen = ++generation;
    pool.addJob([this, gen] {
        std::vector<float> ref, ir;
        double rSr, iSr;
        juce::String refName, irName;
        {
            const juce::ScopedLock sl(dataLock);
            ref = refData; ir = irData; rSr = refSr; iSr = irSr;
            refName = refFile.getFileName(); irName = irFile.getFileName();
        }
        const double sr = hostSr.load();
        juce::String info;
        {
            const juce::ScopedLock sl(dataLock);
            info = roomInfo;
        }
        const juce::String roomText = irName.isEmpty() ? juce::String("fehlt (nur Pausenerkennung)") : (info.isNotEmpty() ? info : irName);
        if (ref.empty()) {
            setStatus(ir.empty() ? juce::String("Kein Referenz-Take geladen")
                                 : juce::String::fromUTF8("Raum: ") + roomText + juce::String::fromUTF8(" – Referenz-Take fehlt"));
            return;
        }
        if (sr <= 0.0) { setStatus(juce::String::fromUTF8("Warte auf Audio-Engine …")); return; }

        setStatus(juce::String::fromUTF8("Analysiere …"));
        std::string err;
        auto m = gh::VocalModel::build(ref, rSr, ir, iSr, sr, kIrCutMs, err);
        if (gen != generation.load()) return; // inzwischen neuer Auftrag
        if (!m) { setStatus(juce::String(err)); return; }
        publish(std::move(m));
        setStatus(juce::String::fromUTF8("Bereit · Referenz: ") + refName + juce::String::fromUTF8(" · Raum: ") + roomText);
    });
}

void CrystalVoiceProcessor::publish(std::unique_ptr<gh::VocalModel> m) {
    const juce::ScopedLock sl(modelLock);
    if (owned) retired.emplace_back(std::move(owned), juce::Time::getMillisecondCounter());
    owned = std::move(m);
    model.store(owned.get());
}

void CrystalVoiceProcessor::timerCallback() {
    int expected = Recorded;
    if (measState.compare_exchange_strong(expected, Analyzing)) runAnalysis();

    const juce::ScopedLock sl(modelLock);
    const auto now = juce::Time::getMillisecondCounter();
    retired.erase(std::remove_if(retired.begin(), retired.end(),
                                 [now](const auto& r) { return now - r.second > 2000; }),
                  retired.end());
}

void CrystalVoiceProcessor::setStatus(const juce::String& s) {
    const juce::ScopedLock sl(statusLock);
    status = s;
}

juce::String CrystalVoiceProcessor::getStatus() const {
    const juce::ScopedLock sl(statusLock);
    return status;
}

// ---------------------------------------------------------------------------------------

void CrystalVoiceProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = params.copyState();
    {
        const juce::ScopedLock sl(dataLock);
        state.setProperty("refFile", refFile.getFullPathName(), nullptr);
        state.setProperty("irFile", irFile.getFullPathName(), nullptr);
    }
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, destData);
}

void CrystalVoiceProcessor::setStateInformation(const void* data, int sizeInBytes) {
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml || !xml->hasTagName(params.state.getType())) return;
    auto state = juce::ValueTree::fromXml(*xml);
    params.replaceState(state);
    const auto rs = state.getProperty("refFile").toString();
    const auto is = state.getProperty("irFile").toString();
    if (juce::File::isAbsolutePath(is) && juce::File(is).existsAsFile()) loadRoomIr(juce::File(is));
    if (juce::File::isAbsolutePath(rs) && juce::File(rs).existsAsFile()) loadReference(juce::File(rs));
}

juce::AudioProcessorEditor* CrystalVoiceProcessor::createEditor() {
    return new CrystalVoiceEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new CrystalVoiceProcessor();
}
