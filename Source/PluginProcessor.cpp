#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
constexpr int kMaxChunk = 4096;
constexpr float kIrCutMs = 5.0f; // später als Direktschall + 5 ms gilt als Raum
const juce::String kNoRoom = juce::String::fromUTF8("Keine Raum-IR – LOAD oder MEASURE");
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
    pMode = params.getRawParameterValue("mode");
    pSendMute = params.getRawParameterValue("sendMute");
    status = kNoRoom;
    resolveSources();
    startTimer(100);
}

CrystalVoiceProcessor::~CrystalVoiceProcessor() {
    stopTimer();
    gh::StreamHub::get().release(mySlot.exchange(-1));
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
    l.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "mic", 2 }, "Mikrofon",
        juce::StringArray { "Neutral", "Heil PR 37", "Shure KSM9", "Neumann KMS 105" }, 0));
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "presence", 1 }, "Presence", false));
    l.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "mode", 1 }, "Function",
        juce::StringArray { "Process", "Send Instrumental", "Send Reference Vocal" }, 0));
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "sendMute", 1 }, "Send Output Mute", false));
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
    scratchVoc.assign(kMaxChunk, 0.0f);
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
    auto& hub = gh::StreamHub::get();

    auto mainIn = getBusBuffer(buffer, true, 0);
    auto mainOut = getBusBuffer(buffer, false, 0);
    const int inCh = mainIn.getNumChannels();
    const int outCh = mainOut.getNumChannels();

    bool playing = false;
    long long pos = 0;
    if (auto* ph = getPlayHead())
        if (auto info = ph->getPosition()) {
            playing = info->getIsPlaying();
            if (auto t = info->getTimeInSamples()) pos = (long long) *t;
        }

    auto vu = [this](double e, int c, float& ms) {
        const float a = 1.0f - std::exp(-(float) c / (float) (getSampleRate() * 0.3));
        ms += a * ((float) (e / c) - ms);
    };

    // ------------------------------------------------------------------ Sender-Modus
    const int mode = (int) pMode->load();
    if (mode != Process) {
        if (measState.load() == Running || measState.load() == Requested) measState.store(Idle);
        const int slot = mySlot.load();
        hub.setRole(slot, mode == SendInstrumental ? gh::StreamHub::Instrumental : gh::StreamHub::Reference);
        const bool mute = pSendMute->load() > 0.5f;
        for (int off = 0; off < n; off += kMaxChunk) {
            const int c = juce::jmin(kMaxChunk, n - off);
            if (inCh == 0) std::fill(scratchMic.begin(), scratchMic.begin() + c, 0.0f);
            else {
                juce::FloatVectorOperations::copy(scratchMic.data(), mainIn.getReadPointer(0, off), c);
                for (int ch = 1; ch < inCh; ++ch)
                    juce::FloatVectorOperations::add(scratchMic.data(), mainIn.getReadPointer(ch, off), c);
                if (inCh > 1) juce::FloatVectorOperations::multiply(scratchMic.data(), 1.0f / (float) inCh, c);
            }
            if (playing) hub.write(slot, pos + off, scratchMic.data(), c);
            double e = 0.0;
            for (int i = 0; i < c; ++i) e += (double) scratchMic[(size_t) i] * scratchMic[(size_t) i];
            vu(e, c, msIn);
            vu(mute ? 0.0 : e, c, msOut);
        }
        // Durchreichen (Ein- und Ausgang teilen sich die Kanäle) oder stummschalten
        for (int ch = 0; ch < outCh; ++ch) {
            if (mute) mainOut.clear(ch, 0, n);
            else if (ch >= inCh && inCh > 0) mainOut.copyFrom(ch, 0, mainIn, 0, 0, n);
        }
        inLevel.store(msIn);
        outLevel.store(msOut);
        instrOk.store(false); refOk.store(false); vocalActive.store(false);
        playbackDb.store(0.0f);
        return;
    }

    // ------------------------------------------------------------------ Hauptinstanz
    const float* scCh[2] = { nullptr, nullptr };
    int scNum = 0;
    if (getBusCount(true) > 1 && getBus(true, 1)->isEnabled()) {
        auto sc = getBusBuffer(buffer, true, 1);
        scNum = juce::jmin(2, sc.getNumChannels());
        for (int c = 0; c < scNum; ++c) scCh[c] = sc.getReadPointer(c);
    }
    sidechainOk.store(scNum > 0);

    engine.setAmounts(pVocal->load() * 0.01f, pInstr->load() * 0.01f);
    tone.set((int) pMic->load(), pPresence->load() > 0.5f);

    // Sender-Spuren werden um einen Block versetzt gelesen: so ist ihr Signal sicher schon
    // geschrieben, egal in welcher Reihenfolge der Host die Spuren rechnet. Das Mikro hört
    // die Anlage ohnehin erst nach Ein- plus Ausgangslatenz (≥ 2 Blöcke).
    const int iSlot = instrSlot.load(), rSlot = refSlot.load();
    const long long syncSamples = (long long) std::llround(pSync->load() * 0.001 * getSampleRate());
    bool gotInstr = false, gotRef = false;

    // Messung starten: nur bei gestopptem Transport (Schutz vor einem Sweep mitten im Song)
    if (measState.load() == Requested) {
        if (playing) { measState.store(Idle); measRefused.store(true); }
        else { measPos.store(0); measState.store(Running); }
    }
    const bool measuring = measState.load() == Running;
    const gh::RoomModel* m = model.load();

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

        // Instrumental: Sender-Spur oder Host-Sidechain
        const float* instr = nullptr;
        if (iSlot >= 0) {
            if (playing) gotInstr |= hub.read(iSlot, pos + off - n, scratchRef.data(), c);
            else std::fill(scratchRef.begin(), scratchRef.begin() + c, 0.0f);
            instr = scratchRef.data();
        } else if (iSlot == -2 && scNum > 0) {
            juce::FloatVectorOperations::copy(scratchRef.data(), scCh[0] + off, c);
            if (scNum > 1) {
                juce::FloatVectorOperations::add(scratchRef.data(), scCh[1] + off, c);
                juce::FloatVectorOperations::multiply(scratchRef.data(), 0.5f, c);
            }
            instr = scratchRef.data();
            gotInstr = true;
        }

        // Referenz-Vocal (SYNC verschiebt sie gegen den Live-Gesang)
        const float* voc = nullptr;
        if (rSlot >= 0 && playing) {
            gotRef |= hub.read(rSlot, pos + off - n + syncSamples, scratchVoc.data(), c);
            voc = scratchVoc.data();
        }

        double eIn = 0.0, eOut = 0.0;
        for (int i = 0; i < c; ++i) eIn += (double) scratchMic[(size_t) i] * scratchMic[(size_t) i];
        if (measuring) {
            // Mikro aufnehmen, Sweep ausgeben; Entstörung und Klangstufe ruhen
            size_t p = measPos.load();
            for (int i = 0; i < c; ++i, ++p) {
                if (p < recBuf.size()) recBuf[p] = scratchMic[(size_t) i];
                scratchOut[(size_t) i] = (p >= measPre && p - measPre < sweep.size()) ? sweep[p - measPre] : 0.0f;
            }
            measPos.store(p);
            if (p >= recBuf.size()) measState.store(Recorded);
        } else {
            engine.process(scratchMic.data(), instr, voc, scratchOut.data(), c, playing, m);
            tone.process(scratchOut.data(), c);
        }
        for (int i = 0; i < c; ++i) eOut += (double) scratchOut[(size_t) i] * scratchOut[(size_t) i];
        vu(eIn, c, msIn);
        vu(eOut, c, msOut);

        for (int ch = 0; ch < outCh; ++ch)
            juce::FloatVectorOperations::copy(mainOut.getWritePointer(ch, off), scratchOut.data(), c);
    }

    instrOk.store(gotInstr || (iSlot >= 0 && hub.isLive(iSlot)));
    refOk.store(gotRef);
    playbackDb.store(engine.playbackReductionDb());
    inLevel.store(msIn);
    outLevel.store(msOut);
    vocalActive.store(engine.vocalStageActive());
    modelOk.store(engine.modelAccepted());
}

// ---------------------------------------------------------------------------------------
// Quellen und Sender

std::vector<CrystalVoiceProcessor::SourceOption> CrystalVoiceProcessor::getSourceOptions(bool instrumental) const {
    std::vector<SourceOption> v;
    v.push_back({ "AUTO", "AUTO" });
    if (instrumental) v.push_back({ "SIDECHAIN", "HOST-SIDECHAIN" });
    v.push_back({ "OFF", "AUS" });
    const int want = instrumental ? gh::StreamHub::Instrumental : gh::StreamHub::Reference;
    const int own = mySlot.load();
    for (auto& i : gh::StreamHub::get().list())
        if (i.slot != own && i.role == want && !i.name.empty())
            v.push_back({ "SEND:" + juce::String::fromUTF8(i.name.c_str()), juce::String::fromUTF8(i.name.c_str()) });
    return v;
}

void CrystalVoiceProcessor::setSource(bool instrumental, const juce::String& id) {
    {
        const juce::ScopedLock sl(srcLock);
        (instrumental ? instrSource : refSource) = id;
    }
    resolveSources();
}

juce::String CrystalVoiceProcessor::getSource(bool instrumental) const {
    const juce::ScopedLock sl(srcLock);
    return instrumental ? instrSource : refSource;
}

juce::String CrystalVoiceProcessor::describeSource(bool instrumental) const {
    const juce::ScopedLock sl(srcLock);
    return instrumental ? instrLabel : refLabel;
}

int CrystalVoiceProcessor::resolveOne(const juce::String& id, int role, bool allowSidechain, juce::String& label) const {
    const auto list = gh::StreamHub::get().list();
    const int own = mySlot.load();
    if (id == "OFF") { label = "AUS"; return -1; }
    if (id == "SIDECHAIN") { label = "HOST-SIDECHAIN"; return -2; }
    if (id.startsWith("SEND:")) {
        const auto name = id.fromFirstOccurrenceOf("SEND:", false, false);
        for (auto& i : list)
            if (i.slot != own && juce::String::fromUTF8(i.name.c_str()) == name) { label = name; return i.slot; }
        label = name + juce::String::fromUTF8(" – nicht gefunden");
        return -1;
    }
    // AUTO: erste Sender-Instanz mit passender Rolle, sonst Sidechain bzw. nichts
    for (auto& i : list)
        if (i.slot != own && i.role == role) {
            label = juce::String::fromUTF8("AUTO · ") + juce::String::fromUTF8(i.name.c_str());
            return i.slot;
        }
    label = allowSidechain ? juce::String::fromUTF8("AUTO · HOST-SIDECHAIN") : juce::String::fromUTF8("AUTO · kein Sender");
    return allowSidechain ? -2 : -1;
}

void CrystalVoiceProcessor::resolveSources() {
    juce::String iId, rId, iL, rL;
    {
        const juce::ScopedLock sl(srcLock);
        iId = instrSource; rId = refSource;
    }
    instrSlot.store(resolveOne(iId, gh::StreamHub::Instrumental, true, iL));
    refSlot.store(resolveOne(rId, gh::StreamHub::Reference, false, rL));
    const juce::ScopedLock sl(srcLock);
    instrLabel = iL;
    refLabel = rL;
}

void CrystalVoiceProcessor::setSendName(const juce::String& n) {
    {
        const juce::ScopedLock sl(srcLock);
        sendName = n.trim();
    }
    const int s = mySlot.load();
    if (s >= 0) gh::StreamHub::get().setName(s, getSendName().toStdString());
}

juce::String CrystalVoiceProcessor::getSendNameRaw() const {
    const juce::ScopedLock sl(srcLock);
    return sendName;
}

juce::String CrystalVoiceProcessor::getSendName() const {
    const juce::ScopedLock sl(srcLock);
    if (sendName.isNotEmpty()) return sendName;
    if (trackName.isNotEmpty()) return trackName;
    return getMode() == SendReference ? juce::String("Referenz-Vocal") : juce::String("Instrumental");
}

void CrystalVoiceProcessor::updateTrackProperties(const TrackProperties& p) {
    {
        const juce::ScopedLock sl(srcLock);
        trackName = p.name;
    }
    const int s = mySlot.load();
    if (s >= 0) gh::StreamHub::get().setName(s, getSendName().toStdString());
}

// ---------------------------------------------------------------------------------------
// Raum-IR laden und Raummodell im Hintergrund bauen

bool CrystalVoiceProcessor::readMono(const juce::File& f, std::vector<float>& out, double& sr, juce::String& err) {
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
    if (!r) { err = "Datei nicht lesbar: " + f.getFileName(); return false; }
    const auto len = r->lengthInSamples;
    if (len <= 0 || len > (juce::int64) (r->sampleRate * 30.0)) {
        err = juce::String::fromUTF8("Datei leer oder länger als 30 Sekunden.");
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
        std::vector<float> ir;
        double iSr;
        juce::String irName, info;
        {
            const juce::ScopedLock sl(dataLock);
            ir = irData; iSr = irSr;
            irName = irFile.getFileName();
            info = roomInfo;
        }
        if (ir.empty()) { setStatus(kNoRoom); return; }
        const double sr = hostSr.load();
        if (sr <= 0.0) { setStatus(juce::String::fromUTF8("Warte auf Audio-Engine …")); return; }

        std::string err;
        auto m = gh::RoomModel::build(ir, iSr, sr, kIrCutMs, err);
        if (gen != generation.load()) return; // inzwischen neuer Auftrag
        if (!m) { setStatus(juce::String(err)); return; }
        publish(std::move(m));
        setStatus("Raum: " + (info.isNotEmpty() ? info : irName));
    });
}

void CrystalVoiceProcessor::publish(std::unique_ptr<gh::RoomModel> m) {
    const juce::ScopedLock sl(modelLock);
    if (owned) retired.emplace_back(std::move(owned), juce::Time::getMillisecondCounter());
    owned = std::move(m);
    model.store(owned.get());
}

void CrystalVoiceProcessor::timerCallback() {
    int expected = Recorded;
    if (measState.compare_exchange_strong(expected, Analyzing)) runAnalysis();

    // Sender anmelden/abmelden, Namen pflegen, Quellen auflösen
    auto& hub = gh::StreamHub::get();
    const bool sender = getMode() != Process;
    if (sender && mySlot.load() < 0) {
        const int s = hub.claim(instanceId);
        hub.setRole(s, getMode() == SendInstrumental ? gh::StreamHub::Instrumental : gh::StreamHub::Reference);
        hub.setName(s, getSendName().toStdString());
        mySlot.store(s);
    } else if (!sender && mySlot.load() >= 0) {
        hub.release(mySlot.exchange(-1));
    } else if (sender) {
        hub.setName(mySlot.load(), getSendName().toStdString()); // Rolle kann den Standardnamen ändern
    }
    if (!sender) resolveSources();

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
        state.setProperty("irFile", irFile.getFullPathName(), nullptr);
        state.setProperty("roomInfo", roomInfo, nullptr);
    }
    {
        const juce::ScopedLock sl(srcLock);
        state.setProperty("instrSource", instrSource, nullptr);
        state.setProperty("refSource", refSource, nullptr);
        state.setProperty("sendName", sendName, nullptr);
    }
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, destData);
}

void CrystalVoiceProcessor::setStateInformation(const void* data, int sizeInBytes) {
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml || !xml->hasTagName(params.state.getType())) return;
    auto state = juce::ValueTree::fromXml(*xml);
    params.replaceState(state);
    {
        const juce::ScopedLock sl(srcLock);
        instrSource = state.getProperty("instrSource", "AUTO").toString();
        refSource = state.getProperty("refSource", "AUTO").toString();
        sendName = state.getProperty("sendName", "").toString();
    }
    const auto is = state.getProperty("irFile").toString();
    if (juce::File::isAbsolutePath(is) && juce::File(is).existsAsFile()) {
        {
            const juce::ScopedLock sl(dataLock);
            roomInfo = state.getProperty("roomInfo", "").toString();
        }
        loadRoomIr(juce::File(is));
    }
}

juce::AudioProcessorEditor* CrystalVoiceProcessor::createEditor() {
    return new CrystalVoiceEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new CrystalVoiceProcessor();
}
