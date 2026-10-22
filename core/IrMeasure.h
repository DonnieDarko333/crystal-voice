// Crystal Voice – Raummessung per exponentiellem Sinus-Sweep (Farina).
// Ablauf: Ruhe (Grundrauschen) → Sweep → Nachhall aufnehmen → Dekonvolution.
// Klirrprodukte des Lautsprechers landen bei negativer Zeit und werden abgeschnitten.
#pragma once
#include <string>
#include <vector>

namespace gh {

struct IrMeasurement {
    static constexpr double kPreSeconds = 1.0;   // Ruhe vor dem Sweep
    static constexpr double kSweepSeconds = 6.0;
    static constexpr double kTailSeconds = 4.0;  // Nachhall nach dem Sweep
    static constexpr double kF1 = 30.0, kF2 = 20000.0;
    static constexpr float kAmplitude = 0.25f;   // −12 dBFS

    static std::vector<float> makeSweep(double sr);
    static size_t recordLength(double sr);
    static size_t preLength(double sr);

    struct Result {
        std::vector<float> ir;
        float t60 = 0.0f;     // Breitband-Nachhallzeit (aus T20), 0 = nicht bestimmbar
        float snrDb = 0.0f;   // Spitze der IR zu Rauschboden
        bool clipped = false; // Mikrofonsignal übersteuert
        std::string warning;  // leer = alles gut
    };

    // rec: Mikrofonaufnahme ab Beginn der Ruhephase, sweep: ausgegebenes Signal
    static Result analyze(const std::vector<float>& rec, const std::vector<float>& sweep, double sr);
};

} // namespace gh
