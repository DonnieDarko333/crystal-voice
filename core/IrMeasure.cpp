#include "IrMeasure.h"
#include "Fft.h"
#include <algorithm>
#include <cmath>

namespace gh {

size_t IrMeasurement::preLength(double sr) { return (size_t) (kPreSeconds * sr); }

size_t IrMeasurement::recordLength(double sr) {
    return (size_t) ((kPreSeconds + kSweepSeconds + kTailSeconds) * sr);
}

std::vector<float> IrMeasurement::makeSweep(double sr) {
    const size_t n = (size_t) (kSweepSeconds * sr);
    const double f2 = std::min(kF2, 0.45 * sr);
    const double L = kSweepSeconds / std::log(f2 / kF1);
    std::vector<float> x(n);
    const size_t fin = (size_t) (0.05 * sr), fout = (size_t) (0.01 * sr);
    for (size_t i = 0; i < n; ++i) {
        const double t = (double) i / sr;
        double v = std::sin(2.0 * kPi * kF1 * L * (std::exp(t / L) - 1.0));
        if (i < fin) v *= 0.5 - 0.5 * std::cos(kPi * (double) i / (double) fin);
        if (i >= n - fout) v *= 0.5 - 0.5 * std::cos(kPi * (double) (n - 1 - i) / (double) fout);
        x[i] = (float) (kAmplitude * v);
    }
    return x;
}

IrMeasurement::Result IrMeasurement::analyze(const std::vector<float>& rec, const std::vector<float>& sweep, double sr) {
    Result r;
    if (rec.size() <= sweep.size() || sweep.empty()) { r.warning = "Aufnahme zu kurz."; return r; }

    float peakIn = 0.0f;
    for (float v : rec) peakIn = std::max(peakIn, std::fabs(v));
    r.clipped = peakIn > 0.98f;
    if (peakIn < 1e-5f) { r.warning = "Kein Signal am Mikrofon."; return r; }

    // Regularisierte Dekonvolution im Frequenzbereich, auf das Sweep-Band begrenzt
    const int N = nextPow2((long long) (rec.size() + sweep.size()));
    Fft<double> fft(N);
    std::vector<std::complex<double>> X(N), Y(N);
    for (size_t i = 0; i < sweep.size(); ++i) X[i] = sweep[i];
    for (size_t i = 0; i < rec.size(); ++i) Y[i] = rec[i];
    fft.forward(X.data());
    fft.forward(Y.data());

    double maxX = 0.0;
    for (int k = 0; k <= N / 2; ++k) maxX = std::max(maxX, std::norm(X[k]));
    const double eps = 1e-5 * maxX;
    const double f2 = std::min(kF2, 0.45 * sr);
    auto band = [&](double f) {
        const double lo0 = kF1, lo1 = kF1 * 1.6, hi1 = f2 * 0.85, hi0 = f2;
        if (f <= lo0 || f >= hi0) return 0.0;
        if (f < lo1) return 0.5 - 0.5 * std::cos(kPi * (f - lo0) / (lo1 - lo0));
        if (f > hi1) return 0.5 + 0.5 * std::cos(kPi * (f - hi1) / (hi0 - hi1));
        return 1.0;
    };
    for (int k = 0; k < N; ++k) {
        const int kk = k <= N / 2 ? k : N - k;
        const double w = band((double) kk * sr / N);
        Y[k] = w * Y[k] * std::conj(X[k]) / (std::norm(X[k]) + eps);
    }
    fft.inverse(Y.data());

    // Direktschall suchen (nur kausaler, gültiger Bereich)
    const size_t maxLag = rec.size() - sweep.size();
    size_t peak = 0;
    double pv = 0.0;
    for (size_t i = 0; i < maxLag; ++i) {
        const double v = std::fabs(Y[i].real());
        if (v > pv) { pv = v; peak = i; }
    }

    // Rauschboden: spätes Stück nach dem Direktschall
    const size_t avail = maxLag - peak;
    const size_t n0 = peak + (size_t) (avail * 0.85), n1 = maxLag;
    double noise = 0.0;
    for (size_t i = n0; i < n1; ++i) noise += Y[i].real() * Y[i].real();
    noise = noise / (double) std::max<size_t>(1, n1 - n0) + 1e-30;
    r.snrDb = (float) (10.0 * std::log10(pv * pv / noise));

    // Ende der IR: wo die Hüllkurve (20 ms) bis auf 3 dB an das Rauschen kommt
    const size_t win = (size_t) (0.02 * sr);
    size_t cut = std::min(n0, peak + (size_t) (3.5 * sr));
    for (size_t i = peak + win; i + win < n0; i += win) {
        double e = 0.0;
        for (size_t j = 0; j < win; ++j) e += Y[i + j].real() * Y[i + j].real();
        if (e / (double) win < 2.0 * noise) { cut = i; break; }
    }

    const size_t start = peak > (size_t) (0.002 * sr) ? peak - (size_t) (0.002 * sr) : 0;
    r.ir.resize(cut - start);
    const double g = 0.9 / pv;
    for (size_t i = 0; i < r.ir.size(); ++i) r.ir[i] = (float) (Y[start + i].real() * g);
    const size_t fade = std::min(r.ir.size() / 4, (size_t) (0.03 * sr));
    for (size_t i = 0; i < fade; ++i)
        r.ir[r.ir.size() - 1 - i] *= (float) (0.5 - 0.5 * std::cos(kPi * (double) i / (double) fade));

    // Nachhallzeit: Schroeder-Rückwärtsintegration ab 20 ms nach dem Direktschall
    // (bei Nahmessung würde der starke Direktschall die Kurve sonst verfälschen),
    // Gerade zwischen −5 und −25 dB
    if (r.ir.size() > (peak - start) + (size_t) (0.1 * sr)) {
        const size_t p0 = peak - start + (size_t) (0.02 * sr);
        std::vector<double> sch(r.ir.size() - p0);
        double acc = 0.0;
        for (size_t i = sch.size(); i-- > 0;) {
            acc += (double) r.ir[p0 + i] * r.ir[p0 + i];
            sch[i] = acc;
        }
        auto tAt = [&](double db) -> double {
            const double thr = sch[0] * std::pow(10.0, db / 10.0);
            for (size_t i = 0; i < sch.size(); ++i)
                if (sch[i] < thr) return (double) i / sr;
            return -1.0;
        };
        const double t5 = tAt(-5.0), t25 = tAt(-25.0), t15 = tAt(-15.0);
        if (t5 >= 0.0 && t25 > t5) r.t60 = (float) (3.0 * (t25 - t5));
        else if (t5 >= 0.0 && t15 > t5) r.t60 = (float) (6.0 * (t15 - t5));
    }

    if (r.clipped) r.warning = "Mikrofon übersteuert – Pegel senken und wiederholen.";
    else if (r.snrDb < 30.0f) r.warning = "Rauschabstand gering – lauter messen oder Ruhe im Raum.";
    return r;
}

} // namespace gh
