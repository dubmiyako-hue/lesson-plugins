// AC-13: no allocation on the audio thread. Global operator new and delete are replaced by counting ones; the count runs
// only inside processBlock. Between blocks the parameters move (as a host's automation would), so that the blocks run
// the smoothing, the HP and LP fades, the EQ's Auto Gain computation and the bypass's fade.
// Only this thread counts: JUCE's timer thread posts messages (and allocates) whenever it likes.
// Each plug-in runs mono and stereo at 44.1, 48 and 192 kHz. Only operator new and delete are counted (the core calls
// nothing else that allocates and takes no lock: read in review 02). Exit code 0 when no block allocated or freed anything.
#include <comp/CompProcessor.h>
#include <eq/EqProcessor.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <random>

namespace {
thread_local bool counting = false;
std::atomic<long long> allocations { 0 };
std::atomic<long long> frees { 0 };

void* allocate(std::size_t size) {
    if (counting) allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
    throw std::bad_alloc();
}
void* allocate(std::size_t size, std::align_val_t align) {
    if (counting) allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = _aligned_malloc(size == 0 ? 1 : size, static_cast<std::size_t>(align))) return p;
    throw std::bad_alloc();
}
void release(void* p) noexcept {
    if (p != nullptr && counting) frees.fetch_add(1, std::memory_order_relaxed);
    std::free(p);
}
void release(void* p, std::align_val_t) noexcept {
    if (p != nullptr && counting) frees.fetch_add(1, std::memory_order_relaxed);
    _aligned_free(p);
}
}

void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try { return allocate(size); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try { return allocate(size); } catch (...) { return nullptr; }
}
void* operator new(std::size_t size, std::align_val_t align) { return allocate(size, align); }
void* operator new[](std::size_t size, std::align_val_t align) { return allocate(size, align); }
void operator delete(void* p) noexcept { release(p); }
void operator delete[](void* p) noexcept { release(p); }
void operator delete(void* p, std::size_t) noexcept { release(p); }
void operator delete[](void* p, std::size_t) noexcept { release(p); }
void operator delete(void* p, std::align_val_t align) noexcept { release(p, align); }
void operator delete[](void* p, std::align_val_t align) noexcept { release(p, align); }
void operator delete(void* p, std::size_t, std::align_val_t align) noexcept { release(p, align); }
void operator delete[](void* p, std::size_t, std::align_val_t align) noexcept { release(p, align); }

namespace {
template <typename Processor>
bool run(const char* name, unsigned seed, double sampleRate, int channels) {
    Processor processor;
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(set);
    layout.outputBuses.add(set);
    if (!processor.setBusesLayout(layout)) return false;
    const int maxBlock = 1024;
    processor.prepareToPlay(sampleRate, maxBlock);

    std::mt19937 random(seed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_int_distribution<int> length(1, maxBlock);
    juce::AudioBuffer<float> buffer(channels, maxBlock);
    juce::MidiBuffer midi;
    const auto& params = processor.getParameters();
    long long blocks = 0;
    long long samples = 0;
    for (int round = 0; round < 3000; ++round) {
        // Every few blocks some parameters move; now and then all of them, the bypass included.
        if (round % 4 == 0)
            for (auto* p : params)
                if (round % 64 == 0 || unit(random) < 0.2f) p->setValueNotifyingHost(unit(random));
        const int n = length(random);
        buffer.setSize(channels, n, false, false, true);
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < n; ++i) buffer.setSample(c, i, 0.9f * (2.0f * unit(random) - 1.0f));
        counting = true;
        processor.processBlock(buffer, midi);
        counting = false;
        ++blocks;
        samples += n;
    }
    processor.releaseResources();
    const long long a = allocations.exchange(0);
    const long long f = frees.exchange(0);
    std::printf("%s, %g Hz, %d ch: %lld blocks, %lld samples, %lld allocations, %lld frees in processBlock\n", name,
        sampleRate, channels, blocks, samples, a, f);
    return a == 0 && f == 0;
}
}

int main() {
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    // A check that the counter counts: an allocation while counting.
    counting = true;
    delete new int(1);
    counting = false;
    const bool counts = allocations.exchange(0) == 1 && frees.exchange(0) == 1;
    std::printf("counter self-check: %s\n", counts ? "ok" : "FAILED");
    bool ok = counts;
    ok &= run<lesson::plugin::EqProcessor>("Lesson EQ", 1, 48000.0, 2);
    ok &= run<lesson::plugin::EqProcessor>("Lesson EQ", 3, 44100.0, 1);
    ok &= run<lesson::plugin::EqProcessor>("Lesson EQ", 5, 192000.0, 2);
    ok &= run<lesson::plugin::CompProcessor>("Lesson Comp", 2, 48000.0, 2);
    ok &= run<lesson::plugin::CompProcessor>("Lesson Comp", 4, 44100.0, 1);
    ok &= run<lesson::plugin::CompProcessor>("Lesson Comp", 6, 192000.0, 2);
    std::printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
