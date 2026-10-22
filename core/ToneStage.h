// Crystal Voice – Klangstufe nach der Entstörung: Mikrofon-Anpassung + Presence.
// Biquads (RBJ), minimalphasig, 0 Samples Latenz. Umschalten mit 30 ms Überblendung.
#pragma once
#include "Fft.h"
#include <array>

namespace gh {

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    float process(float x) {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
    void clear() { z1 = z2 = 0; }

    static Biquad highpass(double sr, double f, double q) {
        const double w = 2 * kPi * f / sr, c = std::cos(w), al = std::sin(w) / (2 * q);
        return norm((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    static Biquad peak(double sr, double f, double q, double db) {
        const double A = std::pow(10.0, db / 40), w = 2 * kPi * f / sr, c = std::cos(w), al = std::sin(w) / (2 * q);
        return norm(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    static Biquad highShelf(double sr, double f, double db, double slope = 0.8) {
        const double A = std::pow(10.0, db / 40), w = 2 * kPi * f / sr, c = std::cos(w), s = std::sin(w);
        const double al = s / 2 * std::sqrt((A + 1 / A) * (1 / slope - 1) + 2), sq = 2 * std::sqrt(A) * al;
        return norm(A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
                    (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
    }
    static Biquad lowShelf(double sr, double f, double db, double slope = 0.8) {
        const double A = std::pow(10.0, db / 40), w = 2 * kPi * f / sr, c = std::cos(w), s = std::sin(w);
        const double al = s / 2 * std::sqrt((A + 1 / A) * (1 / slope - 1) + 2), sq = 2 * std::sqrt(A) * al;
        return norm(A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
                    (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
    }

    // Betragsgang in dB (für Tests)
    double magDb(double sr, double f) const {
        const double w = 2 * kPi * f / sr;
        const std::complex<double> z = std::polar(1.0, -w), z2c = z * z;
        const auto h = (b0 + b1 * z + b2 * z2c) / (1.0 + a1 * z + a2 * z2c);
        return 20 * std::log10(std::abs(h));
    }

private:
    static Biquad norm(double b0, double b1, double b2, double a0, double a1, double a2) {
        Biquad q;
        q.b0 = b0 / a0; q.b1 = b1 / a0; q.b2 = b2 / a0; q.a1 = a1 / a0; q.a2 = a2 / a0;
        return q;
    }
};

class ToneStage {
public:
    enum Mic { HeilPR37 = 0, ShureKSM9 = 1, NeumannKMS105 = 2 };
    static constexpr int kStages = 4;
    using Chain = std::array<Biquad, kStages>;

    void prepare(double sampleRate) {
        sr = sampleRate;
        fadeLen = (int) (0.030 * sr);
        cur = design(mic, presence);
        fading = false;
    }

    // Aus dem Audio-Thread vor process() aufrufen; Änderungen werden überblendet.
    void set(int newMic, bool newPresence) {
        if (newMic == mic && newPresence == presence) return;
        mic = newMic;
        presence = newPresence;
        if (fading) cur = nxt;          // laufende Überblendung abschließen
        nxt = design(mic, presence);
        fading = true;
        fadePos = 0;
    }

    void process(float* x, int n) {
        for (int i = 0; i < n; ++i) {
            const float a = run(cur, x[i]);
            if (!fading) { x[i] = a; continue; }
            const float b = run(nxt, x[i]);
            const float t = (float) fadePos / (float) fadeLen;
            x[i] = a + t * (b - a);
            if (++fadePos >= fadeLen) { cur = nxt; fading = false; }
        }
    }

    // Kennlinien. Begründung siehe README („Mikrofon-Anpassung“).
    Chain design(int m, bool pres) const {
        Chain c;
        switch (m) {
        case HeilPR37:      // dynamisch, oberes Ende ab ~10 kHz fallend (spez. bis 18 kHz)
            c[0] = Biquad::highpass(sr, 50.0, 0.707);       // unter dem Arbeitsbereich: Trittschall
            c[1] = Biquad::highShelf(sr, 9000.0, 2.5);      // Luft ergänzen, die die Membranmasse kostet
            c[2] = Biquad::lowShelf(sr, 150.0, -1.0);       // Nahbesprechung der Superniere leicht zurück
            break;
        case ShureKSM9:     // glatt, ab 50 Hz spezifiziert, Bass bereits sanft abfallend
            c[0] = Biquad::highpass(sr, 60.0, 0.707);       // Hand- und Bühnengeräusche unterhalb 50 Hz
            c[1] = Biquad::peak(sr, 5000.0, 0.8, 1.0);      // sehr dezent mehr Kontur für den Livemix
            break;
        case NeumannKMS105: // linear bei 5 cm, ca. +5 dB um 12 kHz, reicht bis 20 Hz
            c[0] = Biquad::highpass(sr, 45.0, 0.707);       // Kondensator bis 20 Hz: Rumpeln/Handling
            c[1] = Biquad::peak(sr, 12000.0, 1.2, -2.5);    // Höhenanhebung halbieren (Zischlaute, Hallanregung)
            c[2] = Biquad::lowShelf(sr, 180.0, 1.0);        // Bassabfall bei etwas Abstand ausgleichen
            break;
        default: break;
        }
        if (pres) c[kStages - 1] = Biquad::peak(sr, 3000.0, 1.0, 2.0);
        return c;
    }

private:
    static float run(Chain& c, float x) {
        for (auto& q : c) x = q.process(x);
        return x;
    }

    double sr = 48000.0;
    int mic = HeilPR37;
    bool presence = false;
    Chain cur, nxt;
    bool fading = false;
    int fadePos = 0, fadeLen = 1440;
};

} // namespace gh
