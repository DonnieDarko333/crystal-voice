#include "VocalModel.h"
#include "Bands.h"
#include <algorithm>
#include <cmath>

namespace gh {

std::vector<float> resampleLinear(const std::vector<float>& x, double fromSr, double toSr) {
    // Für Hüllkurven-Analyse ausreichend; das Audiosignal selbst läuft hier nicht durch.
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

// Frame t umfasst das Analysefenster, das am Ende von Hop t endet: [(t+1)*hop - M, (t+1)*hop)
static void frameBands(const std::vector<float>& sig, int numFrames, int hop,
                       BandAnalyzer& an, int M, int nb, std::vector<float>& out) {
    out.assign((size_t) numFrames * nb, 0.0f);
    std::vector<float> tmp(M);
    for (int t = 0; t < numFrames; ++t) {
        const long long end = (long long) (t + 1) * hop;
        const long long start = end - M;
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

std::unique_ptr<VocalModel> VocalModel::build(const std::vector<float>& ref, double refSr,
                                              const std::vector<float>& ir, double irSr,
                                              double sampleRate, float irCutMs,
                                              std::string& error, int bands) {
    if (ref.empty()) { error = "Referenz-Take ist leer."; return nullptr; }
    if (sampleRate <= 0.0) { error = "Ungültige Samplerate."; return nullptr; }

    auto m = std::make_unique<VocalModel>();
    m->sampleRate = sampleRate;
    const int hop = m->hop, M = m->fftSize;

    BandLayout lay;
    lay.build(sampleRate, M, bands);
    BandAnalyzer an;
    an.init(lay);
    const int nb = lay.num();
    m->numBands = nb;

    const auto r = resampleLinear(ref, refSr, sampleRate);
    const int numFrames = (int) ((r.size() + hop - 1) / hop);
    m->numFrames = numFrames;
    frameBands(r, numFrames, hop, an, M, nb, m->pref);
    m->rexp.assign((size_t) numFrames * nb, 0.0f);

    if (!ir.empty()) {
        auto h = resampleLinear(ir, irSr, sampleRate);
        const size_t maxLen = (size_t) (3.0 * sampleRate);
        if (h.size() > maxLen) h.resize(maxLen);

        size_t peak = 0;
        for (size_t i = 1; i < h.size(); ++i)
            if (std::fabs(h[i]) > std::fabs(h[peak])) peak = i;

        const size_t pre = (size_t) (0.001 * sampleRate);
        const size_t cut = (size_t) (std::max(0.0f, irCutMs) * 0.001 * sampleRate);
        const size_t d0 = peak > pre ? peak - pre : 0;
        const size_t d1 = std::min(h.size(), peak + cut);

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

        // Kern K[b][tau]: späte Energie relativ zur Direktenergie, Ursprung = Frame des Peaks.
        const int tPeak = (int) (peak / hop);
        int Lk = std::max(1, irFrames - tPeak);
        std::vector<double> K((size_t) nb * Lk, 0.0);
        double kMax = 0.0;
        for (int t = 0; t < irFrames; ++t) {
            const int tau = std::max(0, t - tPeak);
            if (tau >= Lk) continue;
            for (int b = 0; b < nb; ++b) {
                const double v = El[(size_t) t * nb + b] / (dirTot[b] + 1e-30);
                K[(size_t) b * Lk + tau] += v;
                kMax = std::max(kMax, K[(size_t) b * Lk + tau]);
            }
        }
        int last = 0;
        for (int tau = 0; tau < Lk; ++tau)
            for (int b = 0; b < nb; ++b)
                if (K[(size_t) b * Lk + tau] > kMax * 1e-7) last = tau;
        const int LkUsed = last + 1;

        // Leistungs-Faltung pro Band (inkohärente Summe = Modell des diffusen Nachhalls).
        // Zwei reelle Folgen in einer komplexen FFT.
        const int Nf = nextPow2((long long) numFrames + LkUsed);
        Fft<double> fft(Nf);
        std::vector<std::complex<double>> z(Nf), p(Nf);
        for (int b = 0; b < nb; ++b) {
            for (int i = 0; i < Nf; ++i) {
                const double x = i < numFrames ? (double) m->pref[(size_t) i * nb + b] : 0.0;
                const double y = i < LkUsed ? K[(size_t) b * Lk + i] : 0.0;
                z[i] = { x, y };
            }
            fft.forward(z.data());
            for (int k = 0; k < Nf; ++k) {
                const std::complex<double> zk = z[k];
                const std::complex<double> zn = std::conj(z[(Nf - k) % Nf]);
                const std::complex<double> X = 0.5 * (zk + zn);
                const std::complex<double> Y = std::complex<double>(0.0, -0.5) * (zk - zn);
                p[k] = X * Y;
            }
            fft.inverse(p.data());
            for (int t = 0; t < numFrames; ++t)
                m->rexp[(size_t) t * nb + b] = (float) std::max(0.0, p[t].real());
        }
        m->hasRoom = true;
    }

    // Aktivitätskarte
    std::vector<double> tp(numFrames), tr(numFrames);
    double maxP = 0.0;
    for (int t = 0; t < numFrames; ++t) {
        double a = 0.0, c = 0.0;
        for (int b = 0; b < nb; ++b) {
            a += m->pref[(size_t) t * nb + b];
            c += m->pref[(size_t) t * nb + b] + m->rexp[(size_t) t * nb + b];
        }
        tp[t] = a;
        tr[t] = c;
        maxP = std::max(maxP, a);
    }
    m->voiced.assign(numFrames, 0);
    std::vector<int> sing(numFrames + 1, 0); // Präfixsumme
    for (int t = 0; t < numFrames; ++t) {
        m->voiced[t] = tp[t] > maxP * 1e-3 ? 1 : 0;              // −30 dB
        sing[t + 1] = sing[t] + (tp[t] > maxP * 1e-4 ? 1 : 0);   // −40 dB
    }
    const int D = (int) std::lround(0.05 * sampleRate / hop);
    m->busy.assign(numFrames, 0);
    for (int t = 0; t < numFrames; ++t) {
        const int a = std::max(0, t - D), b = std::min(numFrames, t + D + 1);
        if (sing[b] - sing[a] > 0) m->busy[t] = 2;
        else if (tr[t] - tp[t] > maxP * 1e-2) m->busy[t] = 1;  // Nachhall über −20 dB
    }
    return m;
}

} // namespace gh
