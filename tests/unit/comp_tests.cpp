// AC-07: the compressor against the web app's CompressorCore run as its worklet runs it (tests/golden/comp, written by
// tests/golden/gen-comp.mjs). AC-08: the web app's own compressor tests (mix-course tests/dsp.test.ts).
#include "Golden.h"
#include <doctest.h>
#include <lesson/Compressor.h>
#include <lesson/NoDenormals.h>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace lesson;

namespace {
using Channels = std::vector<std::vector<float>>;

double run(Compressor& comp, Channels& x, int block) {
    std::vector<float*> pointers;
    for (auto& channel : x) pointers.push_back(channel.data());
    const int frames = static_cast<int>(x[0].size());
    double last = 0;
    for (int start = 0; start < frames; start += block) {
        const int n = std::min(block, frames - start);
        std::vector<float*> at;
        for (float* p : pointers) at.push_back(p + start);
        last = comp.process(at.data(), static_cast<int>(at.size()), n);
    }
    return last;
}

std::vector<float> sine(double freq, double amp, double seconds, double fs) {
    std::vector<float> x(static_cast<size_t>(std::lround(seconds * fs)));
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = static_cast<float>(amp * std::sin(2 * std::numbers::pi * freq * static_cast<double>(i) / fs));
    return x;
}

double peakDb(const std::vector<float>& x, size_t from) {
    float peak = 0;
    for (size_t i = from; i < x.size(); ++i) peak = std::max(peak, std::fabs(x[i]));
    return 20 * std::log10(peak);
}
}

TEST_CASE("[golden-comp] the web app's CompressorCore in 128-sample blocks") {
    const NoDenormals noDenormals;
    double worst = 0;
    long long samples = 0;
    long long exact = 0;
    for (const auto& row : golden::rows("comp/cases.tsv")) {
        REQUIRE(row.size() == 8);
        const std::string& name = row[0];
        const double fs = golden::number(row[1]);
        const int channels = static_cast<int>(golden::number(row[2]));
        const int frames = static_cast<int>(golden::number(row[3]));
        const CompSettings settings { static_cast<float>(golden::number(row[4])), static_cast<float>(golden::number(row[5])),
            static_cast<float>(golden::number(row[6])), static_cast<float>(golden::number(row[7])) };
        const Channels expected = golden::planar("comp/" + name + ".out.f32", channels, frames);
        const Channels input = golden::planar("comp/" + name + ".in.f32", channels, frames);
        for (const int block : { 128, 1, 480 }) {
            CAPTURE(name);
            CAPTURE(block);
            Compressor comp;
            comp.prepare(fs, settings);
            Channels x = input;
            run(comp, x, block);
            double diff = 0;
            for (size_t c = 0; c < x.size(); ++c)
                for (size_t i = 0; i < x[c].size(); ++i) {
                    const double d = std::fabs(static_cast<double>(x[c][i]) - expected[c][i]);
                    diff = std::max(diff, d);
                    if (block == 128) {
                        ++samples;
                        exact += x[c][i] == expected[c][i];
                    }
                }
            CHECK(diff <= 1e-6);
            worst = std::max(worst, diff);
        }
    }
    MESSAGE("max |diff| " << worst << ", bit-exact " << exact << " of " << samples << " samples");
}

TEST_CASE("[comp] 4:1 takes 6 dB off a tone 8 dB over the threshold") {
    CHECK(targetGr(-12, -20, 4) == 6);
    CHECK(targetGr(-30, -20, 4) == 0);
}

TEST_CASE("[comp] ∞:1 takes all of the excess") {
    CHECK(targetGr(-12, -20, kRatioInfinite) == 8);
    CHECK(targetGr(-30, -20, kRatioInfinite) == 0);
}

TEST_CASE("[comp] a tone at ∞:1 with the fastest attack stays at the threshold") {
    const NoDenormals noDenormals;
    const double fs = 48000;
    const double threshold = -20;
    Compressor comp;
    comp.prepare(fs, { static_cast<float>(threshold), static_cast<float>(kRatioInfinite), 0.0f, 100.0f });
    Channels x { sine(1000, std::pow(10, (threshold + 12) / 20), 0.5, fs) };
    run(comp, x, 128);
    CHECK(peakDb(x[0], 0) <= threshold + 1e-6);
}

TEST_CASE("[comp] the GR settles at 6 dB on a sine") {
    const NoDenormals noDenormals;
    const double fs = 48000;
    const double threshold = -20;
    const double amp = std::pow(10, (threshold + 8) / 20);
    for (const auto [attack, release] : { std::pair { 10.0f, 100.0f }, std::pair { 0.0f, 0.0f } })
        for (const double freq : { 60.0, 1000.0 }) {
            CAPTURE(attack);
            CAPTURE(freq);
            Compressor comp;
            comp.prepare(fs, { static_cast<float>(threshold), 4.0f, attack, release });
            Channels x { sine(freq, amp, 1, fs) };
            const double last = run(comp, x, 128);
            // toBeCloseTo(6, 1): within 0.05
            CHECK(std::fabs(last - 6) < 0.05);
            CHECK(std::fabs(comp.gr() - 6) < 0.05);
            CHECK(std::fabs(peakDb(x[0], x[0].size() - 4800) - 20 * std::log10(amp) + 6) < 0.05);
        }
}

TEST_CASE("[comp] nothing happens under the threshold") {
    const NoDenormals noDenormals;
    Compressor comp;
    comp.prepare(48000, { -20.0f, 4.0f, 10.0f, 100.0f });
    Channels x { sine(1000, 0.05, 0.5, 48000) };
    const Channels before = x;
    run(comp, x, static_cast<int>(x[0].size()));
    CHECK(comp.gr() == 0);
    CHECK(x == before);
}
