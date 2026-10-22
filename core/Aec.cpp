#include "Aec.h"
#include <algorithm>

namespace gh {

void Aec::prepare(int numPartitions) {
    P = std::max(1, numPartitions);
    fft.init(F);
    W.assign((size_t) P * K, C(0, 0));
    Xh.assign((size_t) P * K, C(0, 0));
    buf.assign(F, C(0, 0));
    acc.assign(K, C(0, 0));
    E.assign(K, C(0, 0));
    S.assign(K, 0.0);
    pushCount = 0;
    xPrev.assign(N, 0.0f);
    head = rr = 0;
    refEnergy = 0.0f;
}

void Aec::reset() {
    std::fill(W.begin(), W.end(), C(0, 0));
}

bool Aec::healthy() const {
    for (int k = 0; k < K; ++k)            // Stichprobe genügt: NaN/Inf breitet sich aus
        if (!std::isfinite(W[k].real()) || !std::isfinite(W[k].imag())) return false;
    return true;
}

void Aec::toFull(const C* half) {
    for (int k = 0; k <= N; ++k) buf[k] = half[k];
    for (int k = 1; k < N; ++k) buf[F - k] = std::conj(half[k]);
}

void Aec::predict(float* y) {
    std::fill(acc.begin(), acc.end(), C(0, 0));
    for (int j = 0; j < P; ++j) {
        const C* w = &W[(size_t) j * K];
        const C* x = &Xh[(size_t) ((head + j) % P) * K];
        for (int k = 0; k < K; ++k) acc[k] += cmul(w[k], x[k]);
    }
    toFull(acc.data());
    fft.inverse(buf.data());
    for (int i = 0; i < N; ++i) y[i] = buf[N + i].real();
}

void Aec::adapt(const float* e, float muScale) {
    if (muScale <= 0.0f || refEnergy < 1e-10f) return;

    for (int i = 0; i < N; ++i) { buf[i] = C(0, 0); buf[N + i] = C(e[i], 0); }
    fft.forward(buf.data());

    double mean = 0.0;
    for (int k = 0; k < K; ++k) mean += S[k];
    mean /= (double) K;
    const double delta = 1e-2 * mean + 1e-9;
    const float mu = muBase * muScale;
    for (int k = 0; k < K; ++k) E[k] = buf[k] * (float) (mu / (S[k] + delta));

    for (int j = 0; j < P; ++j) {
        C* w = &W[(size_t) j * K];
        const C* x = &Xh[(size_t) ((head + j) % P) * K];
        for (int k = 0; k < K; ++k) w[k] += cmulConj(x[k], E[k]);
    }

    // Gradienten-Beschränkung reihum auf eine Partition (Kreisfaltungsanteile entfernen).
    C* w = &W[(size_t) rr * K];
    toFull(w);
    fft.inverse(buf.data());
    for (int i = 0; i < N; ++i) buf[i] = C(buf[i].real(), 0);
    for (int i = N; i < F; ++i) buf[i] = C(0, 0);
    fft.forward(buf.data());
    for (int k = 0; k < K; ++k) w[k] = buf[k];
    rr = (rr + 1) % P;
}

void Aec::pushRef(const float* x) {
    float en = 0.0f;
    for (int i = 0; i < N; ++i) {
        buf[i] = C(xPrev[i], 0);
        buf[N + i] = C(x[i], 0);
        en += x[i] * x[i];
    }
    fft.forward(buf.data());
    head = (head - 1 + P) % P;          // ältester Slot wird überschrieben
    C* d = &Xh[(size_t) head * K];
    for (int k = 0; k < K; ++k) {
        S[k] += (double) std::norm(buf[k]) - (double) std::norm(d[k]);
        d[k] = buf[k];
    }
    if (++pushCount >= P) {             // Rundungsdrift regelmäßig beseitigen
        pushCount = 0;
        std::fill(S.begin(), S.end(), 0.0);
        for (int j = 0; j < P; ++j)
            for (int k = 0; k < K; ++k) S[k] += (double) std::norm(Xh[(size_t) j * K + k]);
    }
    refEnergy = 0.95f * refEnergy + 0.05f * (en / (float) N);
    std::copy(x, x + N, xPrev.begin());
}

} // namespace gh
