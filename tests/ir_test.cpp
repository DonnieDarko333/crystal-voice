// Prüft die Sweep-Messung gegen eine bekannte, synthetische Halle:
// Latenz, Lautsprecher-Klirr, Grundrauschen. Vergleicht IR und Nachhallzeit.
#include "../core/IrMeasure.h"
#include "../core/Fft.h"
#include <cstdio>
#include <random>
#include <algorithm>

using namespace gh;

static std::vector<float> conv(const std::vector<float>& a, const std::vector<float>& b, size_t outLen) {
    const int N = nextPow2((long long) (a.size() + b.size()));
    Fft<double> f(N);
    std::vector<std::complex<double>> A(N), B(N);
    for (size_t i = 0; i < a.size(); ++i) A[i] = a[i];
    for (size_t i = 0; i < b.size(); ++i) B[i] = b[i];
    f.forward(A.data()); f.forward(B.data());
    for (int k = 0; k < N; ++k) A[k] *= B[k];
    f.inverse(A.data());
    std::vector<float> o(outLen);
    for (size_t i = 0; i < outLen && i < (size_t) N; ++i) o[i] = (float) A[i].real();
    return o;
}

int main() {
    int fails = 0;
    auto check = [&](bool ok, const char* what) { std::printf("[%s] %s\n", ok ? " OK " : "FEHL", what); if (!ok) ++fails; };

    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (double t60 : { 0.8, 1.6, 2.6 }) {
            std::mt19937 rng(7);
            std::normal_distribution<float> nd(0.f, 1.f);
            // Wahre IR: Direktschall, Reflexion, diffuser Nachhall (−10 dB)
            std::vector<float> h((size_t) (3.5 * sr), 0.f);
            h[0] = 1.f;
            h[(size_t) (0.006 * sr)] = 0.2f;
            double e = 0;
            std::vector<float> tail(h.size(), 0.f);
            for (size_t i = (size_t) (0.012 * sr); i < h.size(); ++i) {
                tail[i] = nd(rng) * (float) std::exp(-6.91 * ((double) i / sr - 0.012) / t60);
                e += tail[i] * tail[i];
            }
            for (size_t i = 0; i < h.size(); ++i) h[i] += tail[i] * (float) std::sqrt(0.1 / e);

            // Ausgabe: Ruhe + Sweep; Lautsprecher mit Klirr; Interface-Latenz 7 ms
            const auto sweep = IrMeasurement::makeSweep(sr);
            const size_t pre = IrMeasurement::preLength(sr), len = IrMeasurement::recordLength(sr);
            const size_t lat = (size_t) (0.007 * sr);
            std::vector<float> spk(len, 0.f);
            // Klirr wie bei einem analogen Lautsprecher hinter dem Wandler-Tiefpass: Oberwellen
            // (bis zur 3.) nur, solange sie unter der Nyquist-Frequenz liegen – sonst Aliasing im Modell
            const double Ls = IrMeasurement::kSweepSeconds / std::log(std::min(IrMeasurement::kF2, 0.45 * sr) / IrMeasurement::kF1);
            for (size_t i = 0; i < sweep.size(); ++i) {
                const double fi = IrMeasurement::kF1 * std::exp((double) i / sr / Ls);
                const float mix = (float) std::clamp((0.45 * sr / 3.0 - fi) / (0.05 * sr), 0.0, 1.0);
                const float dist = std::tanh(sweep[i] * 3.0f) / 3.0f; // ca. 1 % Klirr
                spk[pre + lat + i] = mix * dist + (1.0f - mix) * sweep[i];
            }
            auto rec = conv(spk, h, len);
            for (auto& v : rec) v = 0.3f * v + 3e-5f * nd(rng); // Pegel am Mikro, Rauschen ca. −90 dBFS

            const auto r = IrMeasurement::analyze(rec, sweep, sr);

            // Wahre IR auf dasselbe Band begrenzen wie die Messung (30 Hz … 0,85·f2)
            {
                const int Nb = nextPow2((long long) h.size() * 2);
                Fft<double> fb(Nb);
                std::vector<std::complex<double>> Hc(Nb);
                for (size_t i = 0; i < h.size(); ++i) Hc[i] = h[i];
                fb.forward(Hc.data());
                const double f2 = std::min(IrMeasurement::kF2, 0.45 * sr);
                for (int k = 0; k < Nb; ++k) {
                    const double f = (double) (k <= Nb / 2 ? k : Nb - k) * sr / Nb;
                    double w = 1.0;
                    if (f <= 30.0 || f >= f2) w = 0.0;
                    else if (f < 48.0) w = 0.5 - 0.5 * std::cos(kPi * (f - 30.0) / 18.0);
                    else if (f > 0.85 * f2) w = 0.5 + 0.5 * std::cos(kPi * (f - 0.85 * f2) / (0.15 * f2));
                    Hc[k] *= w;
                }
                fb.inverse(Hc.data());
                for (size_t i = 0; i < h.size(); ++i) h[i] = (float) Hc[i].real();
            }
            // Vergleich der ersten 300 ms mit der wahren IR (Pegel normiert)
            const size_t off = (size_t) (0.002 * sr), n = (size_t) (0.3 * sr);
            // (der band-begrenzte Direktschall hat Vorschwinger; die gemessene IR beginnt 2 ms davor)
            double sh = 0, sm = 0, sx = 0;
            for (size_t i = 0; i < n; ++i) { sh += (double) h[i] * h[i]; sm += (double) r.ir[off + i] * r.ir[off + i]; sx += (double) h[i] * r.ir[off + i]; }
            const double gain = sx / sm;
            double err = 0;
            for (size_t i = 0; i < n; ++i) { const double d = h[i] - gain * r.ir[off + i]; err += d * d; }
            const double errDb = 10 * std::log10(err / sh);

            std::printf("%6.0f Hz, T60 %.1f s:  gemessen T60 %.2f s, SNR %.0f dB, Abweichung IR %.1f dB, Länge %.2f s %s\n",
                        sr, t60, r.t60, r.snrDb, errDb, (double) r.ir.size() / sr, r.warning.c_str());
            char buf[160];
            std::snprintf(buf, sizeof buf, "  T60 innerhalb 10 %% (%.0f Hz, %.1f s)", sr, t60);
            check(std::abs(r.t60 - t60) < 0.1 * t60, buf);
            std::snprintf(buf, sizeof buf, "  IR-Form trifft (Fehler < -20 dB)");
            check(errDb < -20.0, buf);
        }

    // Übersteuerung wird erkannt
    {
        const double sr = 48000.0;
        const auto sweep = IrMeasurement::makeSweep(sr);
        std::vector<float> rec(IrMeasurement::recordLength(sr), 0.f);
        for (size_t i = 0; i < sweep.size(); ++i) rec[IrMeasurement::preLength(sr) + i] = std::clamp(sweep[i] * 8.f, -1.f, 1.f);
        const auto r = IrMeasurement::analyze(rec, sweep, sr);
        check(r.clipped && !r.warning.empty(), "Übersteuerung erkannt und gemeldet");
    }
    std::printf("\n%s\n", fails == 0 ? "Alle Prüfungen bestanden." : "FEHLER vorhanden.");
    return fails;
}
