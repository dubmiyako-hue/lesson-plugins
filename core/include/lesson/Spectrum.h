#pragma once
// The AnalyserNode's analysis (Chromium RealtimeAnalyser::DoFFTAnalysis and getFloatFrequencyData) for the EQ's
// spectrum, on the message thread (design.md 2.6). Not part of the sound.
#include <vector>

namespace lesson {

class SpectrumAnalyser {
public:
    static constexpr int kFftSize = 8192; // the web app's AnalyserNode fftSize
    static constexpr int kBins = kFftSize / 2;
    static constexpr float kFloorDb = -300.0f; // a bin whose magnitude is 0

    // Allocates; the magnitudes start at 0, as an AnalyserNode's do.
    SpectrumAnalyser();
    // Magnitudes back to 0.
    void reset() noexcept;
    // One analysis of `samples` (kFftSize, oldest first): Blackman window (alpha 0.16), FFT, |X| times the AnalyserNode's
    // scale, and per bin m = k m + (1 - k) |X| with k = `smoothing` (0 to 1).
    void analyse(const float* samples, double smoothing) noexcept;
    // 20 log10 of the smoothed magnitudes of bins 0 to count - 1 (count at most kBins); kFloorDb where a magnitude is 0.
    void decibels(float* out, int count) const noexcept;
    // The smoothed magnitude of `bin` (0 to kBins - 1), linear; what decibels() takes the logarithm of.
    double magnitude(int bin) const noexcept { return magnitude_[static_cast<size_t>(bin)]; }
    // The bins the screen reads at `sampleRate` (Eq.tsx spectrum()): up to 20 kHz times 2^(1/24), and two more for its
    // interpolation; at most kBins.
    static int binsFor(double sampleRate) noexcept;

private:
    std::vector<double> window_;
    std::vector<double> cos_; // twiddle factors exp(-2 pi i k / kFftSize), k < kFftSize / 2
    std::vector<double> sin_;
    std::vector<int> reverse_; // bit reversal of 0 to kFftSize - 1
    std::vector<double> re_;
    std::vector<double> im_;
    std::vector<double> magnitude_;
};

} // namespace lesson
