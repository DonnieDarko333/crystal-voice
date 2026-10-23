#include "RoomModel.h"
#include "Bands.h"
#include <algorithm>
#include <cmath>

namespace gh {

std::vector<float> resampleLinear(const std::vector<float>& x, double fromSr, double toSr) {
    if (x.empty() || fromSr <= 0.0 || toSr <= 0.0 || fromSr == toSr) return x;
    const double ratio = fromSr / toSr;
    const size_t n = (size_t) std::floor((double) (x.size() - 1) / ratio) + 1;
    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i) {
        const double pos = (double) i * ratio;
        const size_t i0 = (size_t) pos;
        const float f = (float) (pos - (double) i0);
        out[i] = (i0 + 1 < x.size()) ? x[i0] + f * (x[i0 + 1] - x[i0]) : x[i0];
    }
    return out;
}

// Frame t = Analysefenster, das am Ende von Hop t endet: [(t+1)*hop - M, (t+1)*hop)
static void frameBands(const std::vector<float>& sig, int numFrames, int hop, BandAnalyzer& an, int M, int nb,
                       std::vector<float>& out) {
    out.assign((size_t) numFrames * nb, 0.0f);
    std::vector<float> tmp(M);
    for (int t = 0; t < numFrames; ++t) {
        const long long end = (long long) (t + 1) * hop, start = end - M;
        bool any = false;
        for (int i = 0; i < M; ++i) {
            const long long idx = start + i;
            const float v = (idx >= 0 && idx < (long long) sig.size()) ? sig[(size_t) idx] : 0.0f;
            tmp[i] = v;
            any |= (v != 0.0f);
        }
        if (any) an.analyze(tmp.data(), &out[(size_t) t * nb]);
    }
}

std::unique_ptr<RoomModel> RoomModel::build(const std::vector<float>& ir, double irSr, double sampleRate,
                                            float irCutMs, std::string& error, int bands) {
    if (ir.empty()) { error = "Raum-IR ist leer."; return nullptr; }
    if (sampleRate <= 0.0) { error = "Ungültige Samplerate."; return nullptr; }

    auto m = std::make_unique<RoomModel>();
    m->sampleRate = sampleRate;
    const int hop = m->hop, M = m->fftSize;
    BandLayout lay;
    lay.build(sampleRate, M, bands);
    BandAnalyzer an;
    an.init(lay);
    const int nb = lay.num();
    m->numBands = nb;

    auto h = resampleLinear(ir, irSr, sampleRate);
    const size_t maxLen = (size_t) (3.0 * sampleRate);
    if (h.size() > maxLen) h.resize(maxLen);

    size_t peak = 0;
    for (size_t i = 1; i < h.size(); ++i)
        if (std::fabs(h[i]) > std::fabs(h[peak])) peak = i;

    const size_t pre = (size_t) (0.001 * sampleRate);
    const size_t cut = (size_t) (std::max(0.0f, irCutMs) * 0.001 * sampleRate);
    const size_t d0 = peak > pre ? peak - pre : 0, d1 = std::min(h.size(), peak + cut);

    std::vector<float> direct(h.size(), 0.0f), late(h.size(), 0.0f);
    for (size_t i = d0; i < d1; ++i) direct[i] = h[i];
    for (size_t i = d1; i < h.size(); ++i) late[i] = h[i];

    const int irFrames = (int) ((h.size() + hop - 1) / hop) + M / hop;
    std::vector<float> Ed, El;
    frameBands(direct, irFrames, hop, an, M, nb, Ed);
    frameBands(late, irFrames, hop, an, M, nb, El);

    std::vector<double> dirTot(nb, 0.0);
    for (int t = 0; t < irFrames; ++t)
        for (int b = 0; b < nb; ++b) dirTot[b] += Ed[(size_t) t * nb + b];

    // Kern: späte Energie relativ zur Direktenergie, Ursprung = Frame des Direktschalls
    const int tPeak = (int) (peak / hop);
    const int Lk = std::max(1, irFrames - tPeak);
    std::vector<double> K((size_t) Lk * nb, 0.0);
    double kMax = 0.0;
    for (int t = 0; t < irFrames; ++t) {
        const int tau = std::max(0, t - tPeak);
        if (tau >= Lk) continue;
        for (int b = 0; b < nb; ++b) {
            double& k = K[(size_t) tau * nb + b];
            k += El[(size_t) t * nb + b] / (dirTot[b] + 1e-30);
            kMax = std::max(kMax, k);
        }
    }
    int last = 0;
    for (int tau = 0; tau < Lk; ++tau)
        for (int b = 0; b < nb; ++b)
            if (K[(size_t) tau * nb + b] > kMax * 1e-7) last = tau;

    m->length = last + 1;
    m->K.resize((size_t) m->length * nb);
    for (size_t i = 0; i < m->K.size(); ++i) m->K[i] = (float) K[i];
    return m;
}

} // namespace gh
