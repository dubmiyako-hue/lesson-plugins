// Review 02, points 1 and 2 (approved by the user, 2026-10-31): one Inf or NaN in the input does not leave the EQ or the
// compressor putting out NaN for good.
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
            // 20 ms of hold, then the GR comes back from the largest float's level (about 770 dB) at Release 100 ms
            CHECK(finiteAfter<CompEngine>(s, std::numeric_limits<float>::infinity(), 200));
        }
}
