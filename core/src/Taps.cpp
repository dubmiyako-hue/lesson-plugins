#include <lesson/Taps.h>
#include <algorithm>

namespace lesson {

static_assert((SignalRing::kSize & (SignalRing::kSize - 1)) == 0, "the ring's index is masked");
static_assert((CompPoints::kCapacity & (CompPoints::kCapacity - 1)) == 0, "the queue's index is masked");

void SignalRing::push(float x) noexcept {
    const std::uint64_t w = written_.load(std::memory_order_relaxed); // only this thread writes it
    data_[w & (kSize - 1)].store(x, std::memory_order_relaxed);
    written_.store(w + 1, std::memory_order_release);
}

void SignalRing::latest(float* out, int n) const noexcept {
    if (n <= 0) return;
    const std::uint64_t w = written_.load(std::memory_order_acquire);
    const std::uint64_t want = static_cast<std::uint64_t>(n);
    // Samples older than the ring holds, or never written (fewer than `n` pushed so far), read as 0.
    const std::uint64_t held = std::min<std::uint64_t>(w, kSize);
    const std::uint64_t zeros = want > held ? want - held : 0;
    for (std::uint64_t i = 0; i < zeros; ++i) out[i] = 0.0f;
    for (std::uint64_t i = zeros; i < want; ++i) out[i] = data_[(w - want + i) & (kSize - 1)].load(std::memory_order_relaxed);
}

void CompPoints::add(float inPeak, float outPeak, double gr) noexcept {
    // The worklet's Math.max over the blocks of the point; the GR starts at 0 (this.gr = 0).
    if (inPeak > in_) in_ = inPeak;
    if (outPeak > out_) out_ = outPeak;
    if (gr > gr_) gr_ = gr;
    if (++count_ < kSamplesPerPoint) return;

    const std::uint32_t head = head_.load(std::memory_order_relaxed); // only this thread writes it
    const std::uint32_t tail = tail_.load(std::memory_order_acquire);
    if (head - tail < static_cast<std::uint32_t>(kCapacity)) { // else the queue is full and the point is dropped
        queue_[head & (kCapacity - 1)] = { in_, out_, static_cast<float>(gr_) };
        head_.store(head + 1, std::memory_order_release);
    }
    restart();
}

void CompPoints::restart() noexcept {
    count_ = 0;
    in_ = 0.0f;
    out_ = 0.0f;
    gr_ = 0.0;
}

int CompPoints::take(CompPoint* out, int max) noexcept {
    if (max <= 0) return 0;
    const std::uint32_t tail = tail_.load(std::memory_order_relaxed); // only this thread writes it
    const std::uint32_t head = head_.load(std::memory_order_acquire);
    const std::uint32_t n = std::min<std::uint32_t>(head - tail, static_cast<std::uint32_t>(max));
    for (std::uint32_t i = 0; i < n; ++i) out[i] = queue_[(tail + i) & (kCapacity - 1)];
    tail_.store(tail + n, std::memory_order_release);
    return static_cast<int>(n);
}

} // namespace lesson
