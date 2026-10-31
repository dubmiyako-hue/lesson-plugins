#pragma once
// What the screen reads of the sound while the plug-in's window is open (design.md 2.6, 4). The audio thread writes,
// the message thread reads; neither waits for the other, and neither changes the sound.
#include <array>
#include <atomic>
#include <cstdint>

namespace lesson {

// A mono signal: the AnalyserNode's input (the web app's chain.pre and chain.post). One writer (the audio thread) and
// one reader (the message thread). The reader takes the latest samples; a writer that laps it while it reads only
// mixes old and new samples in what the screen shows.
class SignalRing {
public:
    static constexpr int kSize = 16384; // a power of two, at least the FFT's 8192

    // Audio thread.
    void push(float x) noexcept;
    // Audio thread (or prepare and the host's reset, which never run with it): the samples pushed so far read as 0 from
    // now on, as when the ring is new (the window opened again, the sound was prepared at another rate, or reset).
    void restart() noexcept;
    // Message thread: the latest `n` (at most kSize) samples, oldest first. Samples never written, or written before the
    // last restart, read as 0, as an AnalyserNode's buffer starts silent.
    void latest(float* out, int n) const noexcept;

private:
    std::array<std::atomic<float>, kSize> data_ {};
    std::atomic<std::uint64_t> written_ { 0 }; // samples pushed (never wraps in practice)
    std::atomic<std::uint64_t> start_ { 0 };   // written_ at the last restart
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
};

// One point of the compressor's level history (src/compressor.worklet.ts collect): over kSamplesPerPoint samples, the
// largest input peak, the largest output peak before the makeup (both linear), and the largest GR (dB, at least 0).
struct CompPoint {
    float in = 0.0f;
    float out = 0.0f;
    float gr = 0.0f;
};

// The points as the worklet makes them, queued for the screen. One writer (the audio thread), one reader (the message
// thread). When the queue is full a new point is dropped.
class CompPoints {
public:
    static constexpr int kSamplesPerPoint = 256; // src/dsp.ts COMP_POINT
    static constexpr int kCapacity = 4096;       // points; more than 5 s at 192 kHz

    // Audio thread, once per sample: the sample's input peak (largest |x| of the channels), its output peak before the
    // makeup, and the GR (dB) after it.
    void add(float inPeak, float outPeak, double gr) noexcept;
    // Audio thread (or prepare and the host's reset): forgets what was gathered toward the next point. Queued points stay.
    void restart() noexcept;
    // Message thread: up to `max` queued points, oldest first, into `out`; returns how many.
    int take(CompPoint* out, int max) noexcept;

private:
    std::array<CompPoint, kCapacity> queue_ {};
    std::atomic<std::uint32_t> head_ { 0 }; // next to write (audio thread)
    std::atomic<std::uint32_t> tail_ { 0 }; // next to read (message thread)
    int count_ = 0;
    float in_ = 0.0f;
    float out_ = 0.0f;
    double gr_ = 0.0;
};

} // namespace lesson
