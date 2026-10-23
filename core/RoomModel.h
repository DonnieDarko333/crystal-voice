// Crystal Voice – DSP-Kern
// Raummodell aus der gemessenen Impulsantwort: Für jede Verzögerung (in Hops à 64 Samples)
// und jedes Band, welcher Anteil der Direktenergie als später Hall zurückkommt.
// Die Engine faltet damit laufend die Bandenergie der Referenzstimme → erwarteter Hall.
#pragma once
#include <memory>
#include <string>
#include <vector>

namespace gh {

struct RoomModel {
    double sampleRate = 0.0;
    int hop = 64, fftSize = 512, numBands = 0;
    int length = 0;          // Kernlänge in Hops
    std::vector<float> K;    // [tau][band]

    const float* at(int tau) const { return K.data() + (size_t) tau * numBands; }

    // irCutMs: Alles später als Direktschall + irCutMs gilt als zu entfernender Raumanteil.
    static std::unique_ptr<RoomModel> build(const std::vector<float>& ir, double irSr, double sampleRate,
                                            float irCutMs, std::string& error, int bands = 32);
};

std::vector<float> resampleLinear(const std::vector<float>& x, double fromSr, double toSr);

} // namespace gh
