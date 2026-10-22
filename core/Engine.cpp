#include "Engine.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace gh {

#ifndef POW_MS
#define POW_MS 0.008
#endif

static inline float dbToGain(float db) { return std::pow(10.0f, db / 20.0f); }

void Engine::prepare(double sampleRate, float aecLengthMs, int bands) {
    sr = sampleRate;
    const int P = (int) std::ceil(aecLengthMs * 0.001 * sr / H);
    aec.prepare(P);

    layout.build(sr, M, bands);
    an.init(layout);
    nb = layout.num();

    predHop.assign(H, 0.0f); xHop.assign(H, 0.0f); fullHop.assign(H, 0.0f);
    subHop.assign(H, 0.0f);  dHop.assign(H, 0.0f);
    histSub.assign(M, 0.0f); histFull.assign(M, 0.0f); histPred.assign(M, 0.0f);
    bOut.assign(nb, 0.0f); bOutS.assign(nb, 0.0f); bFull.assign(nb, 0.0f); bPred.assign(nb, 0.0f);
    leak.assign(nb, 0.3f); gS.assign(nb, 1.0f); gT.assign(nb, 1.0f); tmpB.assign(nb, 1.0f);
    binG.assign(layout.numBins, 1.0f);

    hOld.assign(L, 0.0f); hNew.assign(L, 0.0f);
    dl.assign(2 * L, 0.0f);
    cep.init(M);
    cbuf.assign(M, {0.0f, 0.0f});

    attC = (float) std::exp(-H / (sr * 0.005)); // Absenkung (5 ms)
    relC = (float) std::exp(-H / (sr * 0.030)); // Rückkehr (30 ms)
    powC = (float) std::exp(-H / (sr * POW_MS)); // Signalleistung symmetrisch glätten
    reset();
}

void Engine::reset() {
    aec.reset();
    std::fill(predHop.begin(), predHop.end(), 0.0f);
    std::fill(histSub.begin(), histSub.end(), 0.0f);
    std::fill(histFull.begin(), histFull.end(), 0.0f);
    std::fill(histPred.begin(), histPred.end(), 0.0f);
    std::fill(dl.begin(), dl.end(), 0.0f);
    std::fill(gS.begin(), gS.end(), 1.0f);
    std::fill(bOutS.begin(), bOutS.end(), 0.0f);
    std::fill(leak.begin(), leak.end(), 0.3f);
    std::fill(hOld.begin(), hOld.end(), 0.0f); hOld[0] = 1.0f;
    std::fill(hNew.begin(), hNew.end(), 0.0f); hNew[0] = 1.0f;
    oldDelta = newDelta = true;
    hopIdx = 0; dlPos = 0;
    logLevel = 0.0f; levelCount = 0;
    divergeCnt = 0; eD = eE = 0.0f; erleDb = 0.0f;
    startHop(0);
}

void Engine::setAmounts(float vocal01, float instr01) {
    const float v = std::clamp(vocal01, 0.0f, 1.0f);
    const float i = std::clamp(instr01, 0.0f, 1.0f);
    wSub = std::min(1.0f, 2.0f * i);
    gamma = std::max(0.0f, 2.0f * i - 1.0f);
    gminI = dbToGain(-20.0f * gamma);
    beta = 2.0f * v;
    gminV = dbToGain(-24.0f * v);
}

void Engine::setSyncMs(float ms) {
    syncSamples = (long long) std::llround(ms * 0.001 * sr);
}

float Engine::firOut(float x, float a) {
    dlPos = (dlPos - 1 + L) % L;
    dl[dlPos] = x;
    dl[dlPos + L] = x;
    if (oldDelta && newDelta) return x;
    const float* s = &dl[dlPos];
    float yo = x, yn = x;
    if (!oldDelta) { yo = 0.0f; for (int k = 0; k < L; ++k) yo += hOld[k] * s[k]; }
    if (!newDelta) { yn = 0.0f; for (int k = 0; k < L; ++k) yn += hNew[k] * s[k]; }
    return yo + a * (yn - yo);
}

void Engine::setAux(const float* const* in, float* const* out, int num) {
    aux.resize(num);
    for (int i = 0; i < num; ++i) {
        aux[i].in = in[i]; aux[i].out = out[i];
        if ((int) aux[i].dl.size() != 2 * L) aux[i].dl.assign(2 * L, 0.0f);
    }
}

float Engine::auxOut(Aux& ax, float x, float a) {
    ax.pos = (ax.pos - 1 + L) % L;
    ax.dl[ax.pos] = x; ax.dl[ax.pos + L] = x;
    const float* s = &ax.dl[ax.pos];
    float yo = x, yn = x;
    if (!oldDelta) { yo = 0.0f; for (int k = 0; k < L; ++k) yo += hOld[k] * s[k]; }
    if (!newDelta) { yn = 0.0f; for (int k = 0; k < L; ++k) yn += hNew[k] * s[k]; }
    return yo + a * (yn - yo);
}

void Engine::process(const float* mic, const float* ref, float* out, int n,
                     bool isPlaying, long long timelinePos, const VocalModel* m) {
    playing = isPlaying;
    modelOk = m && m->sampleRate == sr && m->numBands == nb;
    model = modelOk ? m : nullptr;

    for (int i = 0; i < n; ++i) {
        const float d = mic[i];
        const float x = ref ? ref[i] : 0.0f;
        const float y = predHop[hopIdx];
        const float eF = d - y;
        const float eS = d - wSub * y;
        dHop[hopIdx] = d; xHop[hopIdx] = x; fullHop[hopIdx] = eF; subHop[hopIdx] = eS;

        const float a = (float) (hopIdx + 1) / (float) H;
        for (auto& ax : aux) ax.out[i] = auxOut(ax, ax.in[i], a);
        out[i] = firOut(eS, a);

        if (++hopIdx == H) {
            hopIdx = 0;
            endHop();
            startHop(timelinePos + i + 1);
        }
    }
}

void Engine::endHop() {
    float ed = 0.0f, ee = 0.0f;
    for (int i = 0; i < H; ++i) { ed += dHop[i] * dHop[i]; ee += fullHop[i] * fullHop[i]; }

    // Schutz gegen Divergenz
    if (!(ee <= 4.0f * ed + 1e-9f)) ++divergeCnt; else divergeCnt = 0; // fängt auch NaN
    if (divergeCnt > 30 || !aec.healthy()) { aec.reset(); divergeCnt = 0; }

    // Wirkungsanzeige nur in gesangsfreien Abschnitten
    if (muNext > 0.25f) {
        eD = 0.995f * eD + 0.005f * ed;
        eE = 0.995f * eE + 0.005f * ee;
        if (eD > 1e-12f) erleDb = 10.0f * std::log10((eD + 1e-20f) / (eE + 1e-20f));
    }

    aec.adapt(fullHop.data(), muNext);
    aec.pushRef(xHop.data());

    std::memmove(histSub.data(), histSub.data() + H, sizeof(float) * (M - H));
    std::memmove(histFull.data(), histFull.data() + H, sizeof(float) * (M - H));
    std::memmove(histPred.data(), histPred.data() + H, sizeof(float) * (M - H));
    std::memcpy(histSub.data() + (M - H), subHop.data(), sizeof(float) * H);
    std::memcpy(histFull.data() + (M - H), fullHop.data(), sizeof(float) * H);
    std::memcpy(histPred.data() + (M - H), predHop.data(), sizeof(float) * H);
}

void Engine::startHop(long long pos) {
    aec.predict(predHop.data());

    an.analyze(histSub.data(), bOut.data());
    an.analyze(histPred.data(), bPred.data());
    if (wSub < 1.0f) an.analyze(histFull.data(), bFull.data());
    else bFull = bOut;

    // --- Referenz-Take / Raum-Modell -------------------------------------------
    vocalOn = false;
    int t = -1;
    bool valid = false;
    if (model && playing) {
        const long long tt = pos + syncSamples;
        if (tt >= 0) { t = (int) (tt / H); valid = t < model->numFrames; }
    }
    const float* R = nullptr;
    if (valid) {
        muNext = model->busy[t] == 2 ? 0.0f : (model->busy[t] == 1 ? 0.3f : 1.0f); // Vorgriff
        if (t > 0 && model->voiced[t - 1]) {
            const float* pr = model->prefAt(t - 1);
            const float* rx = model->rexpAt(t - 1);
            double num = 0.0, den = 0.0;
            for (int b = 0; b < nb; ++b) { num += bFull[b]; den += pr[b] + rx[b]; }
            if (num > 1e-20 && den > 1e-20) {
                const float lr = (float) std::log(num / den);
                const float c = levelCount < 200 ? 0.05f : (float) (1.0 - std::exp(-H / (sr * 1.5)));
                logLevel = levelCount == 0 ? lr : logLevel + c * (lr - logLevel);
                ++levelCount;
            }
        }
        if (model->hasRoom && levelCount > 0 && beta > 0.0f) {
            vocalOn = true;
            R = model->rexpAt(t);
        }
    } else {
        muNext = 0.3f; // ohne Modell: vorsichtiger nachführen
    }

    // --- Rest-Playback messen (nur ohne Gesang) ---------------------------------
    {
        float predTot = 0.0f;
        for (int b = 0; b < nb; ++b) predTot += bPred[b];
        if (muNext > 0.25f && predTot > 1e-9f)
            for (int b = 0; b < nb; ++b) {
                const float r = std::clamp(bFull[b] / (bPred[b] + 1e-12f), 1e-3f, 1.0f);
                leak[b] = 0.97f * leak[b] + 0.03f * r;
            }
    }

    // --- Band-Gains ---------------------------------------------------------------
    for (int b = 0; b < nb; ++b) bOutS[b] = powC * bOutS[b] + (1.0f - powC) * bOut[b];
    const float lev = std::exp(logLevel);
    if (vocalOn) { double a = 0, c = 0; for (int b = 0; b < nb; ++b) { a += lev * R[b]; c += bOutS[b]; } dbgRatio = (float) (10.0 * std::log10((a + 1e-20) / (c + 1e-20))); }
    for (int b = 0; b < nb; ++b) {
        float gp = 1.0f;
        if (vocalOn) {
            const float gv = 1.0f - beta * lev * R[b] / (bOutS[b] + 1e-12f);
            gp *= std::max(gv, gminV * gminV);
        }
        if (gamma > 0.0f) {
            const float gi = 1.0f - 2.0f * gamma * leak[b] * bPred[b] / (bOutS[b] + 1e-12f);
            gp *= std::max(gi, gminI * gminI);
        }
        gT[b] = std::sqrt(gp);
    }
    for (int b = 0; b < nb; ++b) {
        const float c = gT[b] < gS[b] ? attC : relC;
        gS[b] = c * gS[b] + (1.0f - c) * gT[b];
    }
    bool identity = true;
    for (int b = 0; b < nb; ++b) {
        const float l = gS[std::max(0, b - 1)], r = gS[std::min(nb - 1, b + 1)];
        tmpB[b] = 0.25f * l + 0.5f * gS[b] + 0.25f * r;
        if (std::fabs(tmpB[b] - 1.0f) > 1e-4f) identity = false;
    }

    // --- Filter für den nächsten Hop (Überblendung im Hop) -------------------------
    hOld.swap(hNew);
    oldDelta = newDelta;
    if (identity) {
        std::fill(hNew.begin(), hNew.end(), 0.0f);
        hNew[0] = 1.0f;
        newDelta = true;
    } else {
        layout.toBins(tmpB.data(), binG.data());
        computeFir(binG.data());
        newDelta = false;
    }
}

// Minimalphasiges FIR aus Betragsgang über das reelle Cepstrum.
void Engine::computeFir(const float* g) {
    const int half = M / 2;
    for (int k = 0; k <= half; ++k) cbuf[k] = { std::log(std::max(g[k], 1e-4f)), 0.0f };
    for (int k = half + 1; k < M; ++k) cbuf[k] = cbuf[M - k];
    cep.inverse(cbuf.data());

    cbuf[0] = { cbuf[0].real(), 0.0f };
    for (int i = 1; i < half; ++i) cbuf[i] = { 2.0f * cbuf[i].real(), 0.0f };
    cbuf[half] = { cbuf[half].real(), 0.0f };
    for (int i = half + 1; i < M; ++i) cbuf[i] = { 0.0f, 0.0f };

    cep.forward(cbuf.data());
    for (int k = 0; k < M; ++k) {
        const float e = std::exp(cbuf[k].real());
        cbuf[k] = { e * std::cos(cbuf[k].imag()), e * std::sin(cbuf[k].imag()) };
    }
    cep.inverse(cbuf.data());

    const int fade = 32;
    for (int k = 0; k < L; ++k) {
        float w = 1.0f;
        if (k >= L - fade)
            w = 0.5f + 0.5f * std::cos((float) kPi * (float) (k - (L - fade) + 1) / (float) fade);
        hNew[k] = cbuf[k].real() * w;
    }
}

} // namespace gh
