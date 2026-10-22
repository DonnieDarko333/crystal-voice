// Crystal Voice – DSP-Kern
// Adaptiver Playback-Canceller (partitioniertes Frequenzbereichs-NLMS, Overlap-Save).
// Latenzfrei: Partition 0 (Verzögerung < 64 Samples) wird ausgelassen. Der Weg
// Rechner → Pult → PA → Luft → Mikro ist immer länger als 64 Samples (1,3 ms),
// daher steht die Vorhersage für den nächsten Hop schon zu dessen Beginn fest.
#pragma once
#include "Fft.h"

namespace gh {

class Aec {
public:
    static constexpr int N = 64; // Hop-/Partitionsgröße

    void prepare(int numPartitions);
    void reset();
    bool healthy() const;

    // Vorhersage des Playback-Anteils im Mikro für den kommenden Hop (N Samples).
    void predict(float* y);
    // Nachführen mit dem Fehler des abgelaufenen Hops (muScale 0 = eingefroren).
    void adapt(const float* e, float muScale);
    // Referenz (Instrumental) des abgelaufenen Hops übernehmen.
    void pushRef(const float* x);

    int partitions() const { return P; }
    void setStep(float m) { muBase = m; }

private:
    using C = std::complex<float>;
    static constexpr int F = 2 * N, K = N + 1;
    void toFull(const C* half);

    int P = 0, head = 0, rr = 0;
    Fft<float> fft;
    std::vector<C> W, Xh, buf, acc, E;
    std::vector<float> xPrev;
    std::vector<double> S; // Summe |X|^2 über alle Partitionen je Bin (NLMS-Normierung)
    int pushCount = 0;
    float refEnergy = 0.0f;
    float muBase = 1.0f;
};

} // namespace gh
