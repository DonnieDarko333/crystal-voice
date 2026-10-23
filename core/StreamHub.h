// Crystal Voice – Signalaustausch zwischen Instanzen im selben Host-Prozess.
//
// Instanzen im SEND-Modus (auf der Instrumental- bzw. Referenz-Spur) schreiben ihr Signal
// hier hinein, adressiert über die Song-Position in Samples. Die Hauptinstanz auf dem
// Mikrokanal liest an der passenden Position. Dadurch ist egal, in welcher Reihenfolge oder
// mit welchem Vorlauf der Host die Spuren rechnet.
//
// Audio-Thread: write()/read() sind sperrfrei und allozieren nicht.
// Nachrichten-Thread: claim()/release()/Namen/Liste (mit Mutex, nie im Audio-Thread).
#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace gh {

class StreamHub {
public:
    enum Role { None = 0, Instrumental = 1, Reference = 2 };
    static constexpr int kSlots = 16;
    static constexpr int kRingBits = 18;             // 262144 Samples (5,4 s bei 48 kHz)
    static constexpr int64_t kRing = int64_t(1) << kRingBits, kMask = kRing - 1;
    static constexpr int kChunkBits = 6;             // Gültigkeit je 64 Samples
    static constexpr int64_t kTags = kRing >> kChunkBits;

    static StreamHub& get() {
        static StreamHub hub;
        return hub;
    }

    // --- Nachrichten-Thread ---------------------------------------------------------
    int claim(uint32_t instanceId) {
        std::lock_guard<std::mutex> lk(mtx);
        for (int i = 0; i < kSlots; ++i) {
            Slot& s = slots[(size_t) i];
            if (s.used.load()) continue;
            if (!s.ring) {
                s.ring.reset(new float[(size_t) kRing]());
                s.tags.reset(new std::atomic<int64_t>[(size_t) kTags]);
            }
            for (int64_t k = 0; k < kTags; ++k) s.tags[(size_t) k].store(-1, std::memory_order_relaxed);
            s.instance.store(instanceId);
            s.lastWriteMs.store(0);
            s.name.clear();
            s.used.store(1, std::memory_order_release);
            return i;
        }
        return -1;
    }

    void release(int i) {
        if (i < 0 || i >= kSlots) return;
        std::lock_guard<std::mutex> lk(mtx);
        slots[(size_t) i].used.store(0, std::memory_order_release);
        slots[(size_t) i].role.store(None);
        // Speicher bleibt bestehen: ein Leser im Audio-Thread darf nie ins Leere greifen.
    }

    void setName(int i, const std::string& n) {
        if (i < 0 || i >= kSlots) return;
        std::lock_guard<std::mutex> lk(mtx);
        slots[(size_t) i].name = n;
    }

    struct Info {
        int slot;
        int role;
        std::string name;
        bool live; // hat in der letzten Sekunde geschrieben
        uint32_t instance;
    };

    std::vector<Info> list() const {
        std::lock_guard<std::mutex> lk(mtx);
        std::vector<Info> v;
        const int64_t now = nowMs();
        for (int i = 0; i < kSlots; ++i) {
            const Slot& s = slots[(size_t) i];
            if (!s.used.load(std::memory_order_acquire)) continue;
            v.push_back({ i, s.role.load(), s.name, now - s.lastWriteMs.load() < 1000, s.instance.load() });
        }
        return v;
    }

    bool isLive(int i) const {
        if (i < 0 || i >= kSlots || !slots[(size_t) i].used.load()) return false;
        return nowMs() - slots[(size_t) i].lastWriteMs.load() < 1000;
    }

    // --- Audio-Thread -----------------------------------------------------------------
    void setRole(int i, int role) {
        if (i >= 0 && i < kSlots) slots[(size_t) i].role.store(role, std::memory_order_relaxed);
    }

    // Schreibt n Samples ab Song-Position pos.
    void write(int i, int64_t pos, const float* x, int n) {
        if (i < 0 || i >= kSlots || pos < 0) return;
        Slot& s = slots[(size_t) i];
        if (!s.used.load(std::memory_order_acquire) || !s.ring) return;
        int k = 0;
        while (k < n) {
            const int64_t p = pos + k;
            const int64_t chunk = p >> kChunkBits;
            const int len = (int) std::min<int64_t>(n - k, ((chunk + 1) << kChunkBits) - p);
            const int64_t at = p & kMask;
            const int first = (int) std::min<int64_t>(len, kRing - at); // Ringende
            std::memcpy(&s.ring[(size_t) at], x + k, sizeof(float) * (size_t) first);
            if (first < len) std::memcpy(&s.ring[0], x + k + first, sizeof(float) * (size_t) (len - first));
            s.tags[(size_t) (chunk & (kTags - 1))].store(chunk, std::memory_order_release);
            k += len;
        }
        s.lastWriteMs.store(nowMs(), std::memory_order_relaxed);
    }

    // Liest n Samples ab Song-Position pos; fehlende Abschnitte werden zu Stille.
    // Rückgabe: true, wenn mindestens ein Abschnitt gültig war.
    bool read(int i, int64_t pos, float* dst, int n) const {
        if (i < 0 || i >= kSlots) { std::memset(dst, 0, sizeof(float) * (size_t) n); return false; }
        const Slot& s = slots[(size_t) i];
        if (!s.used.load(std::memory_order_acquire) || !s.ring) { std::memset(dst, 0, sizeof(float) * (size_t) n); return false; }
        bool any = false;
        int k = 0;
        while (k < n) {
            const int64_t p = pos + k;
            if (p < 0) {
                const int len = (int) std::min<int64_t>(n - k, -p);
                std::memset(dst + k, 0, sizeof(float) * (size_t) len);
                k += len;
                continue;
            }
            const int64_t chunk = p >> kChunkBits;
            const int len = (int) std::min<int64_t>(n - k, ((chunk + 1) << kChunkBits) - p);
            if (s.tags[(size_t) (chunk & (kTags - 1))].load(std::memory_order_acquire) == chunk) {
                const int64_t at = p & kMask;
                const int first = (int) std::min<int64_t>(len, kRing - at);
                std::memcpy(dst + k, &s.ring[(size_t) at], sizeof(float) * (size_t) first);
                if (first < len) std::memcpy(dst + k + first, &s.ring[0], sizeof(float) * (size_t) (len - first));
                any = true;
            } else {
                std::memset(dst + k, 0, sizeof(float) * (size_t) len);
            }
            k += len;
        }
        return any;
    }

private:
    StreamHub() = default;
    static int64_t nowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    struct Slot {
        std::atomic<int> used { 0 }, role { None };
        std::atomic<uint32_t> instance { 0 };
        std::atomic<int64_t> lastWriteMs { 0 };
        std::string name; // nur unter mtx
        std::unique_ptr<float[]> ring;
        std::unique_ptr<std::atomic<int64_t>[]> tags;
    };
    std::array<Slot, kSlots> slots;
    mutable std::mutex mtx;
};

} // namespace gh
