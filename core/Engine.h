// Crystal Voice – DSP-Kern
// Signalfluss pro Sample (keine Latenz im Audiopfad):
//
//   Mikro ──(−) Playback-Vorhersage (AEC, Instrumental-Sidechain)
//        └─► minimalphasiger Gain-Filter (256 Taps) ─► Ausgang
//                 ▲
//   Gains je Hop (64 Samples) aus:
//   • Vocal:  erwarteter Raumhall = laufende Bandenergie der Referenzstimme ⊛ Raummodell (aus IR)
//   • Instr.: gemessener Rest des Playbacks nach der Subtraktion
//
// Regler 0..1:  Vocal 0 = keine Korrektur, 1 = maximal (Übersubtraktion 2, Boden −24 dB)
//               Instr 0..0.5 = Subtraktion 0..100 %, 0.5..1 = zusätzlich Restunterdrückung bis −20 dB
#pragma once
#include "Aec.h"
#include "Bands.h"
#include "RoomModel.h"

namespace gh {

class Engine {
public:
    static constexpr int H = Aec::N; // 64
    static constexpr int M = 512;    // Analysefenster
    static constexpr int L = 256;    // Länge Gain-Filter

    void prepare(double sampleRate, float aecLengthMs = 500.0f, int bands = 32);
    void reset();

    void setAmounts(float vocal01, float instr01);
    void setAecStep(float m) { aec.setStep(m); }

    // mic und out dürfen identisch sein. instr / vocalRef dürfen nullptr sein (Quelle fehlt).
    // vocalRef muss zeitlich zum Live-Gesang passen (Ausrichtung erledigt der Aufrufer).
    void process(const float* mic, const float* instr, const float* vocalRef, float* out, int n,
                 bool playing, const RoomModel* room);

    // Nur für Tests: zusätzliche Signale durch exakt denselben zeitvarianten Gain-Filter
    // schicken (ohne Subtraktion). Erlaubt getrennte Messung von Direktschall und Hall.
    void setAux(const float* const* in, float* const* out, int num);

    // Diagnose (Audio-Thread schreibt, nur lesen)
    float playbackReductionDb() const { return erleDb; }
    bool vocalStageActive() const { return vocalOn; }
    bool modelAccepted() const { return modelOk; }   // Raummodell passt
    bool referenceActive() const { return refSeen; } // Referenzstimme liefert Signal
    float debugMeanGainDb() const { float s = 0; for (float g : tmpB) s += 20.0f * std::log10(std::max(g, 1e-6f)); return tmpB.empty() ? 0.f : s / (float) tmpB.size(); }
    float debugRatioDb() const { return dbgRatio; }
    float debugLevelDb() const { return 4.3429f * logLevel; }

private:
    void endHop();
    void startHop();
    void computeFir(const float* binGain);
    float firOut(float x, float a);

    double sr = 48000.0;

    Aec aec;
    BandLayout layout;
    BandAnalyzer an;
    int nb = 0;

    std::vector<float> predHop, xHop, fullHop, subHop, dHop;
    std::vector<float> histSub, histFull, histPred;
    std::vector<float> bOut, bFull, bPred, leak, gS, gT, tmpB, binG, bOutS;
    int hopIdx = 0;

    bool playing = false;
    const RoomModel* room = nullptr;
    bool modelOk = false;

    // laufende Analyse der Referenzstimme
    std::vector<float> rHop, histRef, bRef, prefRing, rexp;
    int ringCap = 0, ringPos = 0, ringFill = 0;
    bool refThisBlock = false, refSeen = false;
    float maxP = 0.0f, peakDecay = 1.0f;
    int singHang = 1 << 20;

    float wSub = 0.0f, gamma = 0.0f, beta = 0.0f, gminI = 1.0f, gminV = 1.0f;
    float logLevel = 0.0f;
    int levelCount = 0;
    float muNext = 1.0f;
    float attC = 0.5f, relC = 0.9f, powC = 0.9f;
    bool vocalOn = false;

    std::vector<float> hOld, hNew, dl;
    int dlPos = 0;
    bool oldDelta = true, newDelta = true;
    Fft<float> cep;
    std::vector<std::complex<float>> cbuf;

    struct Aux { const float* in = nullptr; float* out = nullptr; std::vector<float> dl; int pos = 0; };
    std::vector<Aux> aux;
    float auxOut(Aux& a, float x, float w);

    float dbgRatio = 0.0f;
    int divergeCnt = 0;
    float eD = 0.0f, eE = 0.0f, erleDb = 0.0f;
};

} // namespace gh
