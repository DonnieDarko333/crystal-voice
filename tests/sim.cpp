// Crystal Voice – Simulation: Halle, Stimme, Instrumental über PA.
// Misst, was die Engine in dB bringt, prüft Bypass-Exaktheit und Blockgrößen-Unabhängigkeit.
#include "../core/Engine.h"
#include <chrono>
#include <cstdio>
#include <random>
#include <cstdlib>

using namespace gh;
static const double SR = 48000.0;
static int gBands = 32;

static std::vector<float> fftConv(const std::vector<float>& a, const std::vector<float>& b) {
    const size_t n = a.size() + b.size() - 1;
    const int N = nextPow2((long long) n);
    Fft<double> f(N);
    std::vector<std::complex<double>> A(N), B(N);
    for (size_t i = 0; i < a.size(); ++i) A[i] = a[i];
    for (size_t i = 0; i < b.size(); ++i) B[i] = b[i];
    f.forward(A.data()); f.forward(B.data());
    for (int k = 0; k < N; ++k) A[k] *= B[k];
    f.inverse(A.data());
    std::vector<float> out(a.size());
    for (size_t i = 0; i < a.size(); ++i) out[i] = (float) A[i].real();
    return out;
}

// Raumantwort: Direktschall + frühe Reflexion + diffuser Nachhall (Rauschen × Exponentialabfall)
static std::vector<float> makeIr(unsigned seed, int delay, float direct, double earlyMs, float early,
                                 double tailStartMs, double t60, float tailEnergy, double lenS) {
    std::mt19937 rng(seed);
    std::normal_distribution<float> nd(0.f, 1.f);
    std::vector<float> h((size_t) (lenS * SR), 0.f);
    h[delay] = direct;
    h[delay + (int) (earlyMs * 0.001 * SR)] += early;
    const int ts = delay + (int) (tailStartMs * 0.001 * SR);
    double e = 0;
    std::vector<float> tail(h.size(), 0.f);
    for (size_t i = ts; i < h.size(); ++i) {
        const double t = (double) (i - ts) / SR;
        tail[i] = nd(rng) * (float) std::exp(-6.91 * t / t60);
        e += tail[i] * tail[i];
    }
    const float s = (float) std::sqrt(tailEnergy / e);
    for (size_t i = 0; i < h.size(); ++i) h[i] += tail[i] * s;
    return h;
}

struct Phrase { double a, b; };

static std::vector<float> run(const std::vector<float>& mic, const std::vector<float>& ref,
                              const VocalModel* m, float v, float in, int blockMode, double* secs = nullptr,
                              const std::vector<const std::vector<float>*>& auxIn = {},
                              std::vector<std::vector<float>>* auxOut = nullptr) {
    Engine e;
    e.prepare(SR, 500.0f, gBands);
    e.setAmounts(v, in);
    std::vector<float> out(mic.size());
    std::mt19937 rng(7);
    std::uniform_int_distribution<int> bd(1, 700);
    auto t0 = std::chrono::steady_clock::now();
    size_t pos = 0;
    while (pos < mic.size()) {
        int n = blockMode == 0 ? 256 : bd(rng);
        n = (int) std::min<size_t>(n, mic.size() - pos);
        if (!auxIn.empty()) {
            if (auxOut->size() != auxIn.size()) auxOut->assign(auxIn.size(), std::vector<float>(mic.size()));
            const float* ai[4]; float* ao[4];
            for (size_t k = 0; k < auxIn.size(); ++k) { ai[k] = auxIn[k]->data() + pos; ao[k] = (*auxOut)[k].data() + pos; }
            e.setAux(ai, ao, (int) auxIn.size());
        }
        e.process(&mic[pos], ref.empty() ? nullptr : &ref[pos], &out[pos], n, true, (long long) pos, m);
        pos += n;
    }
    if (secs) *secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return out;
}

static double energy(const std::vector<float>& x, double a, double b) {
    double s = 0;
    for (size_t i = (size_t) (a * SR); i < (size_t) (b * SR) && i < x.size(); ++i) s += (double) x[i] * x[i];
    return s;
}
static double db(double x) { return 10.0 * std::log10(x + 1e-30); }

int main(int argc, char** argv) {
    if (argc > 1) gBands = std::atoi(argv[1]);
    const double dur = 30.0;
    const size_t N = (size_t) (dur * SR);
    std::mt19937 rng(1);
    std::normal_distribution<float> nd(0.f, 1.f);

    // --- Instrumental: Akkorde (Obertonreich) + perkussive Rauschimpulse ---
    std::vector<float> instr(N, 0.f);
    {
        const float notes[] = { 110, 147, 165, 196, 220, 262, 294, 330 };
        double ph[3] = { 0, 0, 0 };
        float f[3] = { 110, 165, 220 };
        float lp = 0;
        for (size_t i = 0; i < N; ++i) {
            if (i % (size_t) (0.5 * SR) == 0)
                for (int k = 0; k < 3; ++k) f[k] = notes[rng() % 8] * (k == 2 ? 2.f : 1.f);
            float s = 0;
            for (int k = 0; k < 3; ++k) {
                ph[k] += f[k] / SR;
                for (int h = 1; h <= 10; ++h) s += (float) std::sin(2 * kPi * ph[k] * h) / h;
            }
            const double tb = std::fmod((double) i / SR, 0.25);
            const float hit = nd(rng) * (float) std::exp(-tb / 0.04);
            lp += 0.3f * (hit - lp);
            instr[i] = 0.08f * s + 0.6f * lp;
        }
        float pk = 0; for (float x : instr) pk = std::max(pk, std::fabs(x));
        for (float& x : instr) x *= 0.5f / pk;
    }

    // --- Stimme: Pulsfolge durch Formantfilter, Phrasen mit Pausen ---
    std::vector<float> voice(N, 0.f);
    std::vector<Phrase> phr;
    {
        double s = 1.0; bool lng = false;
        while (s < dur - 3.0) { const double l = lng ? 2.4 : 1.6; phr.push_back({ s, s + l }); s += l + 1.3; lng = !lng; }
        double ph = 0;
        float y[3][2] = {};
        for (size_t i = 0; i < N; ++i) {
            const double t = (double) i / SR;
            double env = 0;
            for (auto& p : phr) if (t >= p.a && t < p.b)
                env = std::min({ 1.0, (t - p.a) / 0.03, (p.b - t) / 0.04 });
            const double f0 = 200 + 30 * std::sin(2 * kPi * 0.7 * t);
            ph += f0 / SR;
            float ex = 0;
            if (ph >= 1) { ph -= 1; ex = 1; }
            ex += 0.03f * nd(rng);
            const double fr[3] = { 500 + 250 * std::sin(2 * kPi * 0.9 * t), 1500 + 400 * std::sin(2 * kPi * 0.6 * t), 2700 };
            const double bw[3] = { 90, 120, 180 };
            const float wt[3] = { 1.f, 0.6f, 0.35f };
            float o = 0;
            for (int k = 0; k < 3; ++k) {
                const double r = std::exp(-kPi * bw[k] / SR);
                const double a1 = -2 * r * std::cos(2 * kPi * fr[k] / SR), a2 = r * r;
                const float yn = (float) ((1 - r) * ex - a1 * y[k][0] - a2 * y[k][1]);
                y[k][1] = y[k][0]; y[k][0] = yn;
                o += wt[k] * yn;
            }
            voice[i] = (float) env * o;
        }
        float pk = 0; for (float x : voice) pk = std::max(pk, std::fabs(x));
        for (float& x : voice) x *= 0.5f / pk;
    }

    // --- Räume ---
    // Stimme → Mikro: nah besprochen, Nachhall −10 dB unter Direktschall, T60 1,6 s
    auto irVoice = makeIr(11, 0, 1.0f, 6.0, 0.15f, 12.0, 1.6, 0.1f, 2.5);
    // Messung an (fast) derselben Stelle: andere Feinstruktur, gleiche Hüllkurve
    auto irMeas = makeIr(99, 0, 1.0f, 6.4, 0.13f, 12.0, 1.6, 0.1f, 2.5);
    // PA → Mikro: 12 ms Laufzeit, halliger Weg, Gesamtpegel −8 dB
    auto irPa = makeIr(23, (int) (0.012 * SR), 0.4f, 9.0, 0.1f, 4.0, 1.6, 0.08f, 2.5);

    std::vector<float> irDir(irVoice.size(), 0.f), irLate(irVoice.size(), 0.f);
    for (size_t i = 0; i < irVoice.size(); ++i) (i < (size_t) (0.010 * SR) ? irDir : irLate)[i] = irVoice[i];
    const auto micDir = fftConv(voice, irDir);   // erwünscht: Direktschall (+ unvermeidbare 6-ms-Reflexion)
    const auto micLate = fftConv(voice, irLate); // unerwünscht: Raum
    std::vector<float> micV(N);
    for (size_t i = 0; i < N; ++i) micV[i] = micDir[i] + micLate[i];
    const auto micI = fftConv(instr, irPa);
    std::vector<float> micMix(N);
    for (size_t i = 0; i < N; ++i) micMix[i] = micV[i] + micI[i];

    // Referenz-Take: 1 ms Versatz, anderer Pegel
    std::vector<float> refTake(N, 0.f);
    const int shift = 48 + (std::getenv("REFSHIFT") ? (int) (std::atof(std::getenv("REFSHIFT")) * 0.001 * SR) : 0);
    for (size_t i = (size_t) std::max(0, shift); i < N; ++i)
        if ((long long) i - shift < (long long) N) refTake[i] = 0.8f * voice[i - shift];

    std::string err;
    auto t0 = std::chrono::steady_clock::now();
    auto model = VocalModel::build(refTake, SR, irMeas, SR, SR, 5.0f, err, gBands);
    const double tBuild = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (!model) { std::printf("Modellfehler: %s\n", err.c_str()); return 1; }
    std::printf("Modell: %d Frames x %d Bänder, Aufbau %.2f s für %.0f s Song\n\n",
                model->numFrames, model->numBands, tBuild, dur);

    const std::vector<float> none;
    if (std::getenv("PROBE")) {
        Engine e; e.prepare(SR, 500.f, gBands); e.setAmounts(1.f, 0.f);
        std::vector<float> o(N);
        for (size_t p = 0; p < N; p += 64) {
            e.process(&micV[p], nullptr, &o[p], 64, true, (long long) p, model.get());
            const double t = (double) p / SR;
            if (t > 12.0 && t < 15.5 && p % 2400 == 0)
                std::printf("t=%.2f  gain %6.1f dB  Hall/Signal %6.1f dB  Pegel %5.1f dB\n", t, e.debugMeanGainDb(), e.debugRatioDb(), e.debugLevelDb());
        }
        for (auto& ph : phr) if (ph.a > 11 && ph.a < 16) std::printf("Phrase %.2f–%.2f\n", ph.a, ph.b);
        return 0;
    }
    const double skip = 8.0; // Einschwingzeit ausklammern

    // 1) Bypass exakt?
    {
        auto o = run(micMix, instr, model.get(), 0.f, 0.f, 1);
        double mx = 0; for (size_t i = 0; i < N; ++i) mx = std::max(mx, (double) std::fabs(o[i] - micMix[i]));
        std::printf("Bypass (0/0): max. Abweichung %.3g\n", mx);
    }
    // 2) Blockgrößen-unabhängig (= keine versteckte Latenz)?
    {
        auto a = run(micMix, instr, model.get(), 1.f, 1.f, 0);
        auto b = run(micMix, instr, model.get(), 1.f, 1.f, 1);
        double mx = 0; for (size_t i = 0; i < N; ++i) mx = std::max(mx, (double) std::fabs(a[i] - b[i]));
        std::printf("Blockgröße 256 vs. zufällig 1..700: max. Abweichung %.3g\n\n", mx);
    }

    // 3) Instrumental-Übersprechen allein (Gesangspausen-Steuerung aktiv)
    std::printf("PLAYBACK-ÜBERSPRECHEN (nur Instrumental am Mikro), ab %.0f s\n", skip);
    for (float in : { 0.25f, 0.5f, 0.75f, 1.0f }) {
        double secs;
        auto o = run(micI, instr, model.get(), 0.f, in, 0, &secs);
        std::printf("  Instr %3.0f:  %5.1f dB weniger   (Echtzeitfaktor %.3f)\n",
                    in * 100, db(energy(micI, skip, dur) / energy(o, skip, dur)), secs / dur);
    }

    // 4) Raumhall der Stimme allein – Direktschall und Hall getrennt durch denselben Filter
    std::printf("\nRAUMHALL DER STIMME (nur Stimme am Mikro, ab %.0f s)\n", skip);
    std::printf("            Direktschall | Hall gesamt | Hall im Gesang | Hall in Pausen | Direkt/Hall-Gewinn\n");
    auto winSum = [&](const std::vector<float>& x, bool pauses) {
        double e = 0;
        for (auto& p : phr) {
            if (p.a < skip) continue;
            e += pauses ? energy(x, p.b + 0.06, p.b + 1.2) : energy(x, p.a + 0.1, p.b - 0.05);
        }
        return e;
    };
    for (float v : { 0.25f, 0.5f, 0.75f, 1.0f }) {
        std::vector<std::vector<float>> ao;
        run(micV, none, model.get(), v, 0.f, 0, nullptr, { &micDir, &micLate }, &ao);
        const double dDir = db(energy(ao[0], skip, dur) / energy(micDir, skip, dur));
        const double dLate = db(energy(ao[1], skip, dur) / energy(micLate, skip, dur));
        const double dSing = db(winSum(ao[1], false) / winSum(micLate, false));
        const double dPause = db(winSum(ao[1], true) / winSum(micLate, true));
        std::printf("  Vocal %3.0f:  %+6.1f dB   %+6.1f dB     %+6.1f dB       %+6.1f dB         %+5.1f dB\n",
                    v * 100, dDir, dLate, dSing, dPause, dDir - dLate);
    }

    // 5) Komplettes Mikrosignal
    std::printf("\nKOMPLETT (Stimme + Halle + Playback): Direktschall zu Störanteil (Hall + Playback)\n");
    double base = 0;
    for (auto [v, in] : { std::pair{ 0.f, 0.f }, { 0.f, 1.f }, { 1.f, 0.f }, { 0.5f, 0.5f }, { 0.75f, 0.75f }, { 1.f, 1.f } }) {
        std::vector<std::vector<float>> ao;
        auto o = run(micMix, instr, model.get(), v, in, 0, nullptr, { &micDir, &micLate }, &ao);
        std::vector<float> resid(N);
        for (size_t i = 0; i < N; ++i) resid[i] = o[i] - ao[0][i];
        const double r = db(energy(ao[0], skip, dur) / energy(resid, skip, dur));
        if (v == 0.f && in == 0.f) base = r;
        std::printf("  Vocal %3.0f / Instr %3.0f:  %5.1f dB  (%+5.1f dB)\n", v * 100, in * 100, r, r - base);
    }
    return 0;
}
