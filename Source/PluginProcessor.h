#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine.h"
#include "ToneStage.h"
#include "IrMeasure.h"
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
    void loadReference(const juce::File& f);
    void loadRoomIr(const juce::File& f);
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
    void publish(std::unique_ptr<gh::VocalModel> m);
    void setStatus(const juce::String& s);
    static bool readMono(const juce::File& f, std::vector<float>& out, double& sr, juce::String& err);

    gh::Engine engine;
    gh::ToneStage tone;
    std::vector<float> scratchMic, scratchRef, scratchOut;

    // Messung: Puffer werden in prepareToPlay angelegt, der Audio-Thread schreibt nur hinein
    std::vector<float> sweep, recBuf;
    size_t measPre = 0;
    std::atomic<int> measState { Idle };
    std::atomic<size_t> measPos { 0 };
    std::atomic<bool> measRefused { false };
    void runAnalysis();
    juce::String roomInfo;  // Kurzinfo zur gemessenen IR (geschützt durch dataLock)

    // Modell: Audio-Thread liest nur den Zeiger. Ersetzte Modelle werden verzögert freigegeben.
    std::atomic<gh::VocalModel*> model { nullptr };
    std::unique_ptr<gh::VocalModel> owned;
    std::vector<std::pair<std::unique_ptr<gh::VocalModel>, juce::uint32>> retired;
    juce::CriticalSection modelLock;

    juce::ThreadPool pool { 1 };
    juce::CriticalSection dataLock;
    std::vector<float> refData, irData;
    double refSr = 0.0, irSr = 0.0;
    juce::File refFile, irFile;
    std::atomic<double> hostSr { 0.0 };
    std::atomic<int> generation { 0 };

    mutable juce::CriticalSection statusLock;
    juce::String status { "Kein Referenz-Take geladen" };

    std::atomic<float> playbackDb { 0.0f }, inLevel { 0.0f }, outLevel { 0.0f };
    float msIn = 0.0f, msOut = 0.0f;
    std::atomic<bool> vocalActive { false }, sidechainOk { false }, modelOk { false };

    std::atomic<float>* pVocal = nullptr;
    std::atomic<float>* pInstr = nullptr;
    std::atomic<float>* pSync = nullptr;
    std::atomic<float>* pMic = nullptr;
    std::atomic<float>* pPresence = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CrystalVoiceProcessor)
};
