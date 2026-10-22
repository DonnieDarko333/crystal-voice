// Crystal Voice – DSP-Kern
// Vorab berechnetes Modell aus Referenz-Take und Raum-Impulsantwort:
// Für jeden 64-Sample-Hop der Song-Timeline die erwartete Hallenergie pro Band
// und eine Gesangs-Aktivitätskarte. Wird offline (Hintergrund-Thread) gebaut.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gh {

struct VocalModel {
    double sampleRate = 0.0;
    int hop = 64, fftSize = 512, numBands = 0, numFrames = 0;
    bool hasRoom = false;

    std::vector<float> pref;     // Bandleistung des Referenz-Takes   [frame][band]
    std::vector<float> rexp;     // erwartete Hall-Leistung im Raum    [frame][band]
    std::vector<uint8_t> voiced; // deutlich Gesang in diesem Frame
    std::vector<uint8_t> busy;   // 2 = Gesang (+/-50 ms) → AEC einfrieren, 1 = kräftiger Nachhall → langsam, 0 = frei

    const float* prefAt(int t) const { return pref.data() + (size_t) t * numBands; }
    const float* rexpAt(int t) const { return rexp.data() + (size_t) t * numBands; }

    // ref/ir in beliebiger Samplerate; ir darf leer sein (dann nur Aktivitätskarte).
    // irCutMs: Alles später als Direktschall + irCutMs gilt als zu entfernender Raumanteil.
    static std::unique_ptr<VocalModel> build(const std::vector<float>& ref, double refSr,
                                             const std::vector<float>& ir, double irSr,
                                             double sampleRate, float irCutMs,
                                             std::string& error, int bands = 32);
};

std::vector<float> resampleLinear(const std::vector<float>& x, double fromSr, double toSr);

} // namespace gh
