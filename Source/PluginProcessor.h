#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine.h"
#include "ToneStage.h"
#include "IrMeasure.h"
#include "StreamHub.h"
#include <atomic>
#include <memory>
#include <vector>

class CrystalVoiceProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    CrystalVoiceProcessor();
    ~CrystalVoiceProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Für den Editor
    juce::AudioProcessorValueTreeState params;
    void loadRoomIr(const juce::File& f);

    // Betriebsart: Hauptinstanz (Mikrokanal) oder Sender auf einer Quellspur
    enum Mode { Process = 0, SendInstrumental = 1, SendReference = 2 };
    int getMode() const { return (int) pMode->load(); }

    // Quellenwahl der Hauptinstanz. IDs: "AUTO", "SIDECHAIN" (nur Instrumental), "OFF", "SEND:<Name>"
    struct SourceOption { juce::String id, label; };
    std::vector<SourceOption> getSourceOptions(bool instrumental) const;
    void setSource(bool instrumental, const juce::String& id);
    juce::String getSource(bool instrumental) const;
    juce::String describeSource(bool instrumental) const; // z. B. "AUTO · Playback"
    bool isInstrumentalFlowing() const { return instrOk.load(); }
    bool isReferenceFlowing() const { return refOk.load(); }

    // Sender: angezeigter Name (leer = Spurname bzw. Standard)
    void setSendName(const juce::String& n);
    juce::String getSendName() const;          // effektiver Name
    juce::String getSendNameRaw() const;       // nur vom Nutzer gesetzt
    void updateTrackProperties(const TrackProperties& p) override;
    juce::String getStatus() const;
    float getPlaybackReductionDb() const { return playbackDb.load(); }
    bool isVocalStageActive() const { return vocalActive.load(); }
    bool isSidechainConnected() const { return sidechainOk.load(); }
    bool isModelAccepted() const { return modelOk.load(); }
    // Effektivpegel (Mittelwert der Quadrate, 300 ms integriert) für die Instrumente
    float getInputMs() const { return inLevel.load(); }
    float getOutputMs() const { return outLevel.load(); }

    // Raummessung per Sweep (gibt den Sweep auf dem Ausgang dieses Kanals aus)
    enum MeasureState { Idle = 0, Requested, Running, Recorded, Analyzing };
    void startMeasurement();
    void stopMeasurement();
    int getMeasureState() const { return measState.load(); }
    double getMeasureSeconds() const;   // verstrichene Zeit der laufenden Messung
    bool wasMeasureRefused() { return measRefused.exchange(false); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void startRebuild();
    void publish(std::unique_ptr<gh::RoomModel> m);
    void resolveSources();
    int resolveOne(const juce::String& id, int role, bool allowSidechain, juce::String& label) const;
    void setStatus(const juce::String& s);
    static bool readMono(const juce::File& f, std::vector<float>& out, double& sr, juce::String& err);

    gh::Engine engine;
    gh::ToneStage tone;
    std::vector<float> scratchMic, scratchRef, scratchVoc, scratchOut;

    // Quellen und Sender
    std::atomic<int> instrSlot { -2 }, refSlot { -1 }, mySlot { -1 }; // -2 = Host-Sidechain, -1 = keine
    std::atomic<bool> instrOk { false }, refOk { false };
    mutable juce::CriticalSection srcLock;
    juce::String instrSource { "AUTO" }, refSource { "AUTO" }, instrLabel, refLabel;
    juce::String sendName, trackName;
    const juce::uint32 instanceId = juce::Random::getSystemRandom().nextInt() | 1u;

    // Messung: Puffer werden in prepareToPlay angelegt, der Audio-Thread schreibt nur hinein
    std::vector<float> sweep, recBuf;
    size_t measPre = 0;
    std::atomic<int> measState { Idle };
    std::atomic<size_t> measPos { 0 };
    std::atomic<bool> measRefused { false };
    void runAnalysis();
    juce::String roomInfo;  // Kurzinfo zur gemessenen IR (geschützt durch dataLock)

    // Raummodell: Audio-Thread liest nur den Zeiger. Ersetzte Modelle werden verzögert freigegeben.
    std::atomic<gh::RoomModel*> model { nullptr };
    std::unique_ptr<gh::RoomModel> owned;
    std::vector<std::pair<std::unique_ptr<gh::RoomModel>, juce::uint32>> retired;
    juce::CriticalSection modelLock;

    juce::ThreadPool pool { 1 };
    juce::CriticalSection dataLock;
    std::vector<float> irData;
    double irSr = 0.0;
    juce::File irFile;
    std::atomic<double> hostSr { 0.0 };
    std::atomic<int> generation { 0 };

    mutable juce::CriticalSection statusLock;
    juce::String status;

    std::atomic<float> playbackDb { 0.0f }, inLevel { 0.0f }, outLevel { 0.0f };
    float msIn = 0.0f, msOut = 0.0f;
    std::atomic<bool> vocalActive { false }, sidechainOk { false }, modelOk { false };

    std::atomic<float>* pVocal = nullptr;
    std::atomic<float>* pInstr = nullptr;
    std::atomic<float>* pSync = nullptr;
    std::atomic<float>* pMic = nullptr;
    std::atomic<float>* pPresence = nullptr;
    std::atomic<float>* pMode = nullptr;
    std::atomic<float>* pSendMute = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CrystalVoiceProcessor)
};
