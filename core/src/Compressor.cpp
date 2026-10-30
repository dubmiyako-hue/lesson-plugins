#include <lesson/Compressor.h>
#include <lesson/Format.h>
#include <algorithm>
#include <cmath>

namespace lesson {

double targetGr(double levelDb, double threshold, double ratio) noexcept {
    const double over = std::max(0.0, levelDb - threshold);
    return ratio >= kRatioInfinite ? over : over - over / ratio;
}

double timeCoef(double ms, double sampleRate) noexcept {
    return ms <= 0 ? 0 : std::exp(-1 / ((ms / 1000) * sampleRate));
}

namespace {
float inverseOf(float ratio) noexcept {
    return ratio >= kRatioInfinite ? 0.0f : static_cast<float>(1.0 / ratio);
}
}

void Compressor::prepare(double sampleRate, const CompSettings& settings) {
    sampleRate_ = sampleRate;
    const double held = ((kHoldMs / 1000) * sampleRate) / kSub;
    ring_.assign(static_cast<size_t>(std::max(1.0, jsRound(held))), 0.0f);
    ringPos_ = 0;
    ringMax_ = 0.0f;
    partial_ = 0.0f;
    partialCount_ = 0;
    gr_ = 0.0;
    threshold_.prepare(sampleRate);
    inverseRatio_.prepare(sampleRate);
    threshold_.snap(settings.threshold);
    inverseRatio_.snap(inverseOf(settings.ratio));
    ratio_ = settings.ratio;
    attack_ = timeCoef(settings.attack, sampleRate);
    release_ = timeCoef(settings.release, sampleRate);
}

void Compressor::setTarget(const CompSettings& settings) noexcept {
    threshold_.setTarget(settings.threshold);
    inverseRatio_.setTarget(inverseOf(settings.ratio));
    ratio_ = settings.ratio;
    attack_ = timeCoef(settings.attack, sampleRate_);
    release_ = timeCoef(settings.release, sampleRate_);
}

double Compressor::gainFor(float peak) noexcept {
    if (peak > partial_) partial_ = peak;
    const float level = partial_ > ringMax_ ? partial_ : ringMax_;
    if (++partialCount_ == kSub) pushPartial();

    const double levelDb = 20 * std::log10(level > 1e-9 ? static_cast<double>(level) : 1e-9);
    const bool moving = !settled();
    const double threshold = threshold_.next();
    const double inverseRatio = inverseRatio_.next();
    double target;
    if (moving) {
        const double over = std::max(0.0, levelDb - threshold);
        target = over - over * inverseRatio;
    } else {
        target = targetGr(levelDb, threshold, ratio_);
    }
    const double coef = target > gr_ ? attack_ : release_;
    gr_ = target + coef * (gr_ - target);
    return std::pow(10.0, -gr_ / 20);
}

double Compressor::process(float* const* channels, int numChannels, int numSamples) noexcept {
    double blockMax = 0;
    for (int i = 0; i < numSamples; ++i) {
        float peak = 0;
        for (int c = 0; c < numChannels; ++c) {
            const float a = std::fabs(channels[c][i]);
            if (a > peak) peak = a;
        }
        const double g = gainFor(peak);
        for (int c = 0; c < numChannels; ++c) channels[c][i] = static_cast<float>(channels[c][i] * g);
        if (gr_ > blockMax) blockMax = gr_;
    }
    return blockMax;
}

void Compressor::pushPartial() noexcept {
    ring_[ringPos_] = partial_;
    ringPos_ = (ringPos_ + 1) % ring_.size();
    float m = 0;
    for (const float v : ring_)
        if (v > m) m = v;
    ringMax_ = m;
    partial_ = 0;
    partialCount_ = 0;
}

} // namespace lesson
