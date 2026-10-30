#include <lesson/Spectrum.h>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace lesson {

namespace {
constexpr int kN = SpectrumAnalyser::kFftSize;
// The scale of |X|, X the plain DFT of the windowed samples. Chromium's RealtimeAnalyser writes 1 / fftSize, but its FFT
// backends may return 2 X (then the effective scale would be 2 / fftSize). Decided by Edge 156's AnalyserNode
// (tests/golden/spectrum, 5 signals, bins above -90 dB, 1 / N against Edge's getFloatFrequencyData): the median offset is
// -1e-6 dB (largest |offset| 0.0007 dB), so 1 / N is Chromium's whole scale. 2 / N would sit 6.02 dB (20 log10 2) higher.
constexpr double kScale = 1.0 / kN;
}

SpectrumAnalyser::SpectrumAnalyser()
    : window_(kN), cos_(kN / 2), sin_(kN / 2), reverse_(kN), re_(kN), im_(kN), magnitude_(kBins, 0.0) {
    // Blackman with alpha 0.16 (RealtimeAnalyser::ApplyWindow).
    constexpr double alpha = 0.16;
    constexpr double a0 = 0.5 * (1 - alpha);
    constexpr double a1 = 0.5;
    constexpr double a2 = 0.5 * alpha;
    constexpr double twoPi = 2 * std::numbers::pi;
    for (int i = 0; i < kN; ++i) {
        const double x = static_cast<double>(i) / kN;
        window_[static_cast<size_t>(i)] = a0 - a1 * std::cos(twoPi * x) + a2 * std::cos(2 * twoPi * x);
    }
    for (int k = 0; k < kN / 2; ++k) {
        const double angle = twoPi * k / kN;
        cos_[static_cast<size_t>(k)] = std::cos(angle);
        sin_[static_cast<size_t>(k)] = -std::sin(angle);
    }
    int bits = 0;
    while ((1 << bits) < kN) ++bits;
    for (int i = 0; i < kN; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        reverse_[static_cast<size_t>(i)] = r;
    }
}

void SpectrumAnalyser::reset() noexcept {
    std::fill(magnitude_.begin(), magnitude_.end(), 0.0);
}

void SpectrumAnalyser::analyse(const float* samples, double smoothing) noexcept {
    // Windowed samples in bit-reversed order, so that the transform runs in place.
    for (int i = 0; i < kN; ++i) {
        const size_t at = static_cast<size_t>(reverse_[static_cast<size_t>(i)]);
        re_[at] = static_cast<double>(samples[i]) * window_[static_cast<size_t>(i)];
        im_[at] = 0.0;
    }
    // Radix-2 decimation in time.
    for (int half = 1; half < kN; half *= 2) {
        const int step = kN / (2 * half);
        for (int start = 0; start < kN; start += 2 * half) {
            for (int j = 0; j < half; ++j) {
                const size_t a = static_cast<size_t>(start + j);
                const size_t b = a + static_cast<size_t>(half);
                const double wr = cos_[static_cast<size_t>(j * step)];
                const double wi = sin_[static_cast<size_t>(j * step)];
                const double tr = re_[b] * wr - im_[b] * wi;
                const double ti = re_[b] * wi + im_[b] * wr;
                re_[b] = re_[a] - tr;
                im_[b] = im_[a] - ti;
                re_[a] += tr;
                im_[a] += ti;
            }
        }
    }
    // Chromium packs the Nyquist component into imag[0] and blows it away: bin 0 is the DC's real part alone.
    im_[0] = 0.0;
    const double k = std::clamp(smoothing, 0.0, 1.0);
    for (size_t i = 0; i < static_cast<size_t>(kBins); ++i) {
        const double scalar = std::hypot(re_[i], im_[i]) * kScale;
        magnitude_[i] = k * magnitude_[i] + (1 - k) * scalar;
    }
}

void SpectrumAnalyser::decibels(float* out, int count) const noexcept {
    count = std::min(count, kBins);
    for (int i = 0; i < count; ++i) {
        const double m = magnitude_[static_cast<size_t>(i)];
        out[i] = m == 0.0 ? kFloorDb : static_cast<float>(20 * std::log10(m));
    }
}

int SpectrumAnalyser::binsFor(double sampleRate) noexcept {
    const double bin = sampleRate / kFftSize;
    const double top = std::ceil(20000.0 * std::pow(2.0, 1.0 / 24) / bin) + 2;
    return static_cast<int>(std::min(static_cast<double>(kBins), top));
}

} // namespace lesson
