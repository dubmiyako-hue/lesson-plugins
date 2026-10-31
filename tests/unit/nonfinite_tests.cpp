// Review 02, points 1 and 2 (approved by the user, 2026-10-31): one Inf or NaN in the input does not leave the EQ or the
// compressor putting out NaN for good. And no sample put out is an Inf or a NaN, also the bad one's and the rest of its
// block's (the review of fad038f). The compressor's detector does not hear them (review 04, point 5, approved by
// the user).
#include <doctest.h>
#include <lesson/Engine.h>
#include <lesson/NoDenormals.h>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

using namespace lesson;

namespace {
std::vector<float> noise(int frames, std::uint32_t seed) {
    std::vector<float> x(static_cast<size_t>(frames));
    for (float& v : x) {
        seed = seed * 1664525u + 1013904223u;
        v = 0.5f * static_cast<float>((static_cast<double>(seed >> 8) - 8388608.0) / 8388608.0);
    }
    return x;
}

template <class Engine, class Settings>
bool finiteAfter(const Settings& settings, float bad, int settleBlocks) {
    Engine engine;
    engine.prepare(48000, settings);
    std::vector<float> x = noise(480 * (settleBlocks + 2), 7);
    x[100] = bad;
    for (size_t start = 0; start < x.size(); start += 480) {
        float* channel = x.data() + start;
        engine.process(&channel, 1, 480, settings);
    }
    // the last block, after the glitch has passed
    for (size_t i = x.size() - 480; i < x.size(); ++i)
        if (!std::isfinite(x[i])) return false;
    return true;
}
}

TEST_CASE("[nonfinite] the EQ puts out numbers again after one Inf or NaN") {
    const NoDenormals noDenormals;
    EqEngineSettings s;
    s.bands = { EqBandSettings { true, 80, 0, 1, 24 }, { true, 120, 3, 1, 12 }, { true, 300, -4, 2, 12 }, { true, 1000, 6, 1, 12 },
        { true, 4000, 0, 1, 12 }, { true, 8000, 2, 1, 12 }, { true, 18000, 0, 1, 12 } };
    for (const float bad : { std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
             std::numeric_limits<float>::quiet_NaN() })
        CHECK(finiteAfter<EqEngine>(s, bad, 1));
}

TEST_CASE("[nonfinite] the compressor puts out numbers again after one Inf") {
    const NoDenormals noDenormals;
    for (const float ratio : { 4.0f, 1000.0f })
        for (const float attack : { 0.0f, 10.0f }) {
            CompEngineSettings s;
            s.comp = { -20.0f, ratio, attack, 100.0f };
            CAPTURE(ratio);
            CAPTURE(attack);
            CHECK(finiteAfter<CompEngine>(s, std::numeric_limits<float>::infinity(), 1));
        }
}

namespace {
// One bad sample at 1000 in an 8192-sample block, on the first channel: every sample put out is a number, and the
// samples before the bad one are the clean run's, bit for bit.
template <class Engine, class Settings>
void noNonFiniteOut(const Settings& settings, float bad, int channels) {
    constexpr int kFrames = 8192;
    constexpr int kAt = 1000;
    std::vector<std::vector<float>> clean(static_cast<size_t>(channels));
    for (int c = 0; c < channels; ++c) clean[static_cast<size_t>(c)] = noise(kFrames, 7u + static_cast<std::uint32_t>(c));
    std::vector<std::vector<float>> spoiled = clean;
    spoiled[0][kAt] = bad;
    std::vector<float*> a;
    std::vector<float*> b;
    for (int c = 0; c < channels; ++c) {
        a.push_back(clean[static_cast<size_t>(c)].data());
        b.push_back(spoiled[static_cast<size_t>(c)].data());
    }
    Engine cleanEngine;
    Engine spoiledEngine;
    cleanEngine.prepare(48000, settings);
    spoiledEngine.prepare(48000, settings);
    cleanEngine.process(a.data(), channels, kFrames, settings);
    spoiledEngine.process(b.data(), channels, kFrames, settings);
    int nonFinite = 0;
    int differentBefore = 0;
    for (size_t c = 0; c < spoiled.size(); ++c)
        for (int i = 0; i < kFrames; ++i) {
            if (!std::isfinite(spoiled[c][static_cast<size_t>(i)])) ++nonFinite;
            if (i < kAt && spoiled[c][static_cast<size_t>(i)] != clean[c][static_cast<size_t>(i)]) ++differentBefore;
        }
    CHECK_MESSAGE(nonFinite == 0, nonFinite << " samples put out are not numbers");
    CHECK(differentBefore == 0);
}

const float kBad[] = { std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                       std::numeric_limits<float>::quiet_NaN() };
}

TEST_CASE("[nonfinite] one Inf or NaN in a block puts no Inf or NaN out (Lesson EQ)") {
    const NoDenormals noDenormals;
    EqEngineSettings s;
    s.bands = { EqBandSettings { true, 80, 0, 1, 24 }, { true, 120, 3, 1, 12 }, { true, 300, -4, 2, 12 }, { true, 1000, 6, 1, 12 },
        { true, 4000, 0, 1, 12 }, { true, 8000, 2, 1, 12 }, { true, 18000, 0, 1, 12 } };
    for (const bool autoGain : { false, true })
        for (const bool bypass : { false, true })
            for (const int channels : { 1, 2 })
                for (const float bad : kBad) {
                    s.autoGain = autoGain;
                    s.bypass = bypass;
                    CAPTURE(autoGain);
                    CAPTURE(bypass);
                    CAPTURE(channels);
                    CAPTURE(bad);
                    noNonFiniteOut<EqEngine>(s, bad, channels);
                }
}

TEST_CASE("[nonfinite] one Inf or NaN in a block puts no Inf or NaN out (Lesson Comp)") {
    const NoDenormals noDenormals;
    CompEngineSettings s;
    s.comp = { -20.0f, 4.0f, 3.0f, 100.0f };
    for (const bool autoGain : { false, true })
        for (const bool bypass : { false, true })
            for (const int channels : { 1, 2 })
                for (const float bad : kBad) {
                    s.autoGain = autoGain;
                    s.bypass = bypass;
                    CAPTURE(autoGain);
                    CAPTURE(bypass);
                    CAPTURE(channels);
                    CAPTURE(bad);
                    noNonFiniteOut<CompEngine>(s, bad, channels);
                }
}

TEST_CASE("[nonfinite] the compressor's detector does not hear an Inf or a NaN") {
    // The run with a bad sample is the run with 0 in its place, bit for bit: the bad sample puts out 0 (finiteOr0), and
    // the GR goes on as if it were silence (one channel bad: the other channel's level).
    const NoDenormals noDenormals;
    CompEngineSettings s;
    s.comp = { -20.0f, 1000.0f, 0.1f, 1000.0f };
    for (const bool autoGain : { false, true })
        for (const bool bypass : { false, true })
            for (const int channels : { 1, 2 })
                for (const float bad : kBad) {
                    s.autoGain = autoGain;
                    s.bypass = bypass;
                    CAPTURE(autoGain);
                    CAPTURE(bypass);
                    CAPTURE(channels);
                    CAPTURE(bad);
                    constexpr int kFrames = 8192;
                    constexpr int kAt = 1000;
                    std::vector<std::vector<float>> zero(static_cast<size_t>(channels));
                    for (int c = 0; c < channels; ++c) zero[static_cast<size_t>(c)] = noise(kFrames, 7u + static_cast<std::uint32_t>(c));
                    zero[0][kAt] = 0.0f;
                    std::vector<std::vector<float>> spoiled = zero;
                    spoiled[0][kAt] = bad;
                    std::vector<float*> a;
                    std::vector<float*> b;
                    for (int c = 0; c < channels; ++c) {
                        a.push_back(zero[static_cast<size_t>(c)].data());
                        b.push_back(spoiled[static_cast<size_t>(c)].data());
                    }
                    CompEngine zeroEngine;
                    CompEngine spoiledEngine;
                    zeroEngine.prepare(48000, s);
                    spoiledEngine.prepare(48000, s);
                    CompPoints zeroPoints;
                    CompPoints spoiledPoints;
                    zeroEngine.process(a.data(), channels, kFrames, s, &zeroPoints);
                    spoiledEngine.process(b.data(), channels, kFrames, s, &spoiledPoints);
                    CHECK(zeroEngine.gr() == spoiledEngine.gr());
                    int different = 0;
                    for (size_t c = 0; c < zero.size(); ++c)
                        for (int i = 0; i < kFrames; ++i) different += zero[c][static_cast<size_t>(i)] != spoiled[c][static_cast<size_t>(i)];
                    CHECK_MESSAGE(different == 0, different << " samples differ from the run with 0 in the bad one's place");
                    // the screen's points are numbers, and the same
                    CompPoint p[CompPoints::kCapacity];
                    CompPoint q[CompPoints::kCapacity];
                    const int n = zeroPoints.take(p, CompPoints::kCapacity);
                    REQUIRE(spoiledPoints.take(q, CompPoints::kCapacity) == n);
                    for (int k = 0; k < n; ++k) {
                        CHECK(std::isfinite(q[k].in));
                        CHECK(p[k].in == q[k].in);
                        CHECK(p[k].out == q[k].out);
                        CHECK(p[k].gr == q[k].gr);
                    }
                }
}
