// Crystal Voice – DSP-Kern
// Gehörgerechte Bandaufteilung (ERB-Skala) und Bandleistungs-Analyse.
#pragma once
#include "Fft.h"
#include <algorithm>

namespace gh {

struct BandLayout {
    int fftSize = 0, numBins = 0;
    std::vector<int> edges;     // Bin-Grenzen, Band b = [edges[b], edges[b+1])
    std::vector<float> centers; // Bandmitten in Bins

    int num() const { return (int) edges.size() - 1; }

    void build(double sampleRate, int fft, int desired = 32) {
        fftSize = fft;
        numBins = fft / 2 + 1;
        auto erb = [](double f) { return 21.4 * std::log10(1.0 + 0.00437 * f); };
        auto inv = [](double e) { return (std::pow(10.0, e / 21.4) - 1.0) / 0.00437; };
        const double e0 = erb(50.0), e1 = erb(0.5 * sampleRate);
        edges.assign(1, 0);
        for (int i = 1; i < desired; ++i) {
            int bin = (int) std::lround(inv(e0 + (e1 - e0) * i / desired) / sampleRate * fft);
            bin = std::max(bin, edges.back() + 1);
            if (bin >= numBins - 1) break;
            edges.push_back(bin);
        }
        edges.push_back(numBins);
        centers.resize(num());
        for (int b = 0; b < num(); ++b)
            centers[b] = 0.5f * (float) (edges[b] + edges[b + 1] - 1);
    }

    // Bandwerte linear auf alle Bins interpolieren.
    void toBins(const float* g, float* out) const {
        const int nb = num();
        int b = 0;
        for (int k = 0; k < numBins; ++k) {
            const float x = (float) k;
            if (x <= centers[0]) { out[k] = g[0]; continue; }
            if (x >= centers[nb - 1]) { out[k] = g[nb - 1]; continue; }
            while (b < nb - 2 && x > centers[b + 1]) ++b;
            const float t = (x - centers[b]) / (centers[b + 1] - centers[b]);
            out[k] = g[b] + t * (g[b + 1] - g[b]);
        }
    }
};

class BandAnalyzer {
public:
    void init(const BandLayout& l) {
        L = &l;
        const int M = l.fftSize;
        fft.init(M);
        win.resize(M);
        buf.resize(M);
        for (int i = 0; i < M; ++i)
            win[i] = (float) (0.5 - 0.5 * std::cos(2.0 * kPi * i / M));
    }

    // x: fftSize zusammenhängende Samples. out: Bandleistungen (num() Werte).
    void analyze(const float* x, float* out) {
        const int M = L->fftSize;
        for (int i = 0; i < M; ++i) buf[i] = { x[i] * win[i], 0.0f };
        fft.forward(buf.data());
        for (int b = 0; b < L->num(); ++b) {
            float s = 0.0f;
            for (int k = L->edges[b]; k < L->edges[b + 1]; ++k) s += std::norm(buf[k]);
            out[b] = s;
        }
    }

private:
    const BandLayout* L = nullptr;
    Fft<float> fft;
    std::vector<float> win;
    std::vector<std::complex<float>> buf;
};

} // namespace gh
