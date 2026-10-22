// Crystal Voice – DSP-Kern
// Einfache radix-2 FFT (komplex). Vorwärts unnormiert, invers mit 1/N.
#pragma once
#include <cmath>
#include <complex>
#include <utility>
#include <vector>

namespace gh {

constexpr double kPi = 3.14159265358979323846;

// Komplexe Multiplikation ohne NaN/Inf-Sonderbehandlung (schnell).
template <typename T>
inline std::complex<T> cmul(std::complex<T> a, std::complex<T> b) {
    return { a.real() * b.real() - a.imag() * b.imag(),
             a.real() * b.imag() + a.imag() * b.real() };
}
// conj(a) * b
template <typename T>
inline std::complex<T> cmulConj(std::complex<T> a, std::complex<T> b) {
    return { a.real() * b.real() + a.imag() * b.imag(),
             a.real() * b.imag() - a.imag() * b.real() };
}

template <typename T>
class Fft {
public:
    using C = std::complex<T>;

    Fft() = default;
    explicit Fft(int size) { init(size); }

    void init(int size) {
        n = size;
        if (n <= 0) return;
        int logN = 0;
        while ((1 << logN) < n) ++logN;
        rev.assign(n, 0);
        for (int i = 0; i < n; ++i) {
            int r = 0;
            for (int b = 0; b < logN; ++b)
                if (i & (1 << b)) r |= 1 << (logN - 1 - b);
            rev[i] = r;
        }
        tw.resize(n / 2);
        for (int k = 0; k < n / 2; ++k) {
            const double a = -2.0 * kPi * k / n;
            tw[k] = C((T) std::cos(a), (T) std::sin(a));
        }
    }

    int size() const { return n; }
    void forward(C* d) const { run(d, false); }
    void inverse(C* d) const {
        run(d, true);
        const T s = T(1) / T(n);
        for (int i = 0; i < n; ++i) d[i] *= s;
    }

private:
    void run(C* d, bool inv) const {
        for (int i = 0; i < n; ++i)
            if (i < rev[i]) std::swap(d[i], d[rev[i]]);
        for (int len = 2; len <= n; len <<= 1) {
            const int half = len >> 1, step = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < half; ++j) {
                    C w = tw[(size_t) j * step];
                    if (inv) w = std::conj(w);
                    const C u = d[i + j];
                    const C v = cmul(d[i + j + half], w);
                    d[i + j] = u + v;
                    d[i + j + half] = u - v;
                }
        }
    }

    int n = 0;
    std::vector<int> rev;
    std::vector<C> tw;
};

inline int nextPow2(long long x) {
    long long p = 1;
    while (p < x) p <<= 1;
    return (int) p;
}

} // namespace gh
