#include <lesson/AutoGain.h>
#include <lesson/Compressor.h>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace lesson {

double compMakeupDb(float threshold, float ratio) noexcept {
    return std::min(kMaxMakeupDb, targetGr(0, threshold, ratio) / 2);
}

std::array<BiquadCoefficients, 2> kWeighting(double sampleRate) noexcept {
    const double f0 = 1681.974450955533;
    const double G = 3.999843853973347;
    const double Q = 0.7071752369554196;
    const double K = std::tan((std::numbers::pi * f0) / sampleRate);
    const double Vh = std::pow(10, G / 20);
    const double Vb = std::pow(Vh, 0.4996667741545416);
    const double a0 = 1 + K / Q + K * K;
    const BiquadCoefficients shelf { (Vh + (Vb * K) / Q + K * K) / a0, (2 * (K * K - Vh)) / a0,
        (Vh - (Vb * K) / Q + K * K) / a0, (2 * (K * K - 1)) / a0, (1 - K / Q + K * K) / a0 };
    const double f1 = 38.13547087602444;
    const double Q1 = 0.5003270373238773;
    const double K1 = std::tan((std::numbers::pi * f1) / sampleRate);
    const double d = 1 + K1 / Q1 + K1 * K1;
    const BiquadCoefficients highpass { 1, -2, 1, (2 * (K1 * K1 - 1)) / d, (1 - K1 / Q1 + K1 * K1) / d };
    return { shelf, highpass };
}

void EqLoudness::prepare(double sampleRate, const EqBands& bands) {
    sampleRate_ = sampleRate;
    cosw_.resize(kPoints);
    cos2w_.resize(kPoints);
    k2_.resize(kPoints);
    const auto k = kWeighting(sampleRate);
    kSum_ = 0;
    for (int i = 0; i < kPoints; ++i) {
        const double f = kLowHz * std::pow(kHighHz / kLowHz, (i + 0.5) / kPoints);
        const double w = 2 * std::numbers::pi * f / sampleRate;
        const size_t n = static_cast<size_t>(i);
        cosw_[n] = std::cos(w);
        cos2w_[n] = std::cos(2 * w);
        k2_[n] = magnitudeSquared(k[0], cosw_[n], cos2w_[n]) * magnitudeSquared(k[1], cosw_[n], cos2w_[n]);
        kSum_ += k2_[n];
    }
    busy_ = hasPending_ = false;
    result_ = compute(bands);
}

double EqLoudness::compute(const EqBands& bands) const noexcept {
    std::array<BiquadCoefficients, kEqStages> stages;
    const int count = eqActiveCoefficients(bands, sampleRate_, stages);
    double sum = 0;
    for (int i = 0; i < kPoints; ++i) {
        const size_t n = static_cast<size_t>(i);
        double h = k2_[n];
        for (int s = 0; s < count; ++s) h *= magnitudeSquared(stages[static_cast<size_t>(s)], cosw_[n], cos2w_[n]);
        sum += h;
    }
    return finish(sum, kSum_);
}

void EqLoudness::request(const EqBands& bands) noexcept {
    if (busy_) {
        pending_ = bands;
        hasPending_ = true;
        return;
    }
    start(bands);
}

void EqLoudness::start(const EqBands& bands) noexcept {
    stageCount_ = eqActiveCoefficients(bands, sampleRate_, stages_);
    busy_ = true;
    next_ = 0;
    sum_ = 0;
}

double EqLoudness::point(int i) const noexcept {
    const size_t n = static_cast<size_t>(i);
    double h = k2_[n];
    for (int s = 0; s < stageCount_; ++s) h *= magnitudeSquared(stages_[static_cast<size_t>(s)], cosw_[n], cos2w_[n]);
    return h;
}

bool EqLoudness::tick() noexcept {
    if (!busy_) return false;
    const int end = std::min(kPoints, next_ + kPointsPerSample);
    for (; next_ < end; ++next_) sum_ += point(next_);
    if (next_ < kPoints) return false;
    result_ = finish(sum_, kSum_);
    busy_ = false;
    if (hasPending_) {
        hasPending_ = false;
        start(pending_);
    }
    return true;
}

double EqLoudness::finish(double weighted, double total) noexcept {
    return std::clamp(-10 * std::log10(weighted / total), -kMaxMakeupDb, kMaxMakeupDb);
}

} // namespace lesson
