#include <lesson/Engine.h>
#include <algorithm>
#include <cmath>

namespace lesson {

float gainOfDb(double db) noexcept {
    return static_cast<float>(std::pow(10.0, db / 20));
}

void WetDry::prepare(double sampleRate, float makeup, bool bypass) noexcept {
    for (Smoother* s : { &makeup_, &wet_, &dry_ }) s->prepare(sampleRate);
    makeup_.snap(makeup);
    wet_.snap(bypass ? 0.0f : 1.0f);
    dry_.snap(bypass ? 1.0f : 0.0f);
    beginSample();
}

void WetDry::setBypass(bool bypass) noexcept {
    wet_.setTarget(bypass ? 0.0f : 1.0f);
    dry_.setTarget(bypass ? 1.0f : 0.0f);
}

void WetDry::beginSample() noexcept {
    const bool moving = wet_.moving() || dry_.moving();
    makeupGain_ = makeup_.next();
    wetGain_ = wet_.next();
    dryGain_ = dry_.next();
    if (moving) route_ = Route::Both;
    else route_ = wetGain_ == 0.0f && dryGain_ == 1.0f ? Route::Dry : wetGain_ == 1.0f && dryGain_ == 0.0f ? Route::Wet : Route::Both;
}

float WetDry::mix(float dry, float processed) const noexcept {
    const float made = processed * makeupGain_;
    switch (route_) {
    case Route::Dry: return dry;
    case Route::Wet: return made;
    case Route::Both: break;
    }
    return made * wetGain_ + dry * dryGain_;
}

void EqEngine::prepare(double sampleRate, const EqEngineSettings& settings) {
    eq_.prepare(sampleRate, settings.bands);
    loudness_.prepare(sampleRate, settings.bands);
    requested_ = settings.bands;
    autoGain_ = settings.autoGain;
    wetDry_.prepare(sampleRate, autoGain_ ? gainOfDb(loudness_.makeupDb()) : 1.0f, settings.bypass);
}

void EqEngine::setTargets(const EqEngineSettings& settings) noexcept {
    eq_.setTarget(settings.bands);
    if (settings.bands != requested_) {
        requested_ = settings.bands;
        loudness_.request(settings.bands);
    }
    autoGain_ = settings.autoGain;
    wetDry_.setMakeup(autoGain_ ? gainOfDb(loudness_.makeupDb()) : 1.0f);
    wetDry_.setBypass(settings.bypass);
}

void EqEngine::process(float* const* channels, int numChannels, int numSamples, const EqEngineSettings& settings) noexcept {
    numChannels = std::min(numChannels, kMaxChannels);
    setTargets(settings);
    for (int i = 0; i < numSamples; ++i) {
        if (loudness_.tick() && autoGain_) wetDry_.setMakeup(gainOfDb(loudness_.makeupDb()));
        eq_.beginSample();
        wetDry_.beginSample();
        for (int c = 0; c < numChannels; ++c) {
            const float x = channels[c][i];
            channels[c][i] = wetDry_.mix(x, eq_.process(c, x));
        }
    }
}

void CompEngine::prepare(double sampleRate, const CompEngineSettings& settings) {
    comp_.prepare(sampleRate, settings.comp);
    makeupDb_ = compMakeupDb(settings.comp.threshold, settings.comp.ratio);
    wetDry_.prepare(sampleRate, settings.autoGain ? gainOfDb(makeupDb_) : 1.0f, settings.bypass);
}

void CompEngine::setTargets(const CompEngineSettings& settings) noexcept {
    comp_.setTarget(settings.comp);
    makeupDb_ = compMakeupDb(settings.comp.threshold, settings.comp.ratio);
    wetDry_.setMakeup(settings.autoGain ? gainOfDb(makeupDb_) : 1.0f);
    wetDry_.setBypass(settings.bypass);
}

void CompEngine::process(float* const* channels, int numChannels, int numSamples, const CompEngineSettings& settings) noexcept {
    numChannels = std::min(numChannels, kMaxChannels);
    setTargets(settings);
    for (int i = 0; i < numSamples; ++i) {
        float peak = 0;
        for (int c = 0; c < numChannels; ++c) {
            const float a = std::fabs(channels[c][i]);
            if (a > peak) peak = a;
        }
        const double g = comp_.gainFor(peak);
        wetDry_.beginSample();
        for (int c = 0; c < numChannels; ++c) {
            const float x = channels[c][i];
            channels[c][i] = wetDry_.mix(x, static_cast<float>(x * g));
        }
    }
}

} // namespace lesson
