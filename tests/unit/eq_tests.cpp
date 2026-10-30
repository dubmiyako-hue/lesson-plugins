// AC-06: the EQ against the web app's EqFilters rendered by Edge's OfflineAudioContext (tests/golden/eq, written by
// tests/golden/gen-eq.mjs).
#include "Golden.h"
#include <doctest.h>
#include <lesson/Eq.h>
#include <lesson/NoDenormals.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace lesson;

namespace {
using Channels = std::vector<std::vector<float>>;

// gen-eq.mjs's input: an impulse of 1 - 0.5 c, silence, then from the middle 0.25 times the LCG's noise.
Channels goldenInput(int channels, int frames) {
    Channels x(static_cast<size_t>(channels), std::vector<float>(static_cast<size_t>(frames), 0.0f));
    for (int c = 0; c < channels; ++c) {
        auto& channel = x[static_cast<size_t>(c)];
        channel[0] = 1.0f - 0.5f * static_cast<float>(c);
        std::uint32_t s = 99u + static_cast<std::uint32_t>(c);
        for (int i = frames / 2; i < frames; ++i) {
            s = s * 1664525u + 1013904223u;
            channel[static_cast<size_t>(i)] =
                static_cast<float>(0.25 * ((static_cast<double>(s >> 8) - 8388608.0) / 8388608.0));
        }
    }
    return x;
}

EqBands bandsOf(const std::vector<std::string>& row, size_t from) {
    EqBands bands;
    for (size_t b = 0; b < bands.size(); ++b) {
        const size_t at = from + 5 * b;
        bands[b] = { golden::number(row[at]) != 0, static_cast<float>(golden::number(row[at + 1])),
            static_cast<float>(golden::number(row[at + 2])), static_cast<float>(golden::number(row[at + 3])),
            static_cast<int>(golden::number(row[at + 4])) };
    }
    return bands;
}

void run(Eq& eq, Channels& x, int block) {
    const int frames = static_cast<int>(x[0].size());
    for (int start = 0; start < frames; start += block) {
        std::vector<float*> at;
        for (auto& channel : x) at.push_back(channel.data() + start);
        eq.process(at.data(), static_cast<int>(at.size()), std::min(block, frames - start));
    }
}
}

TEST_CASE("[golden-eq] the HP and LP stages' Q is the web app's") {
    for (const auto& row : golden::rows("eq/qdb.tsv")) {
        REQUIRE(row.size() == 3);
        const int slope = static_cast<int>(golden::number(row[0]));
        const int stage = static_cast<int>(golden::number(row[1]));
        CAPTURE(slope);
        CAPTURE(stage);
        CHECK(butterworthQDb(slope, stage) == static_cast<float>(golden::number(row[2])));
    }
}

TEST_CASE("[golden-eq] the web app's EqFilters in Edge") {
    const NoDenormals noDenormals;
    double worst = 0;
    long long samples = 0;
    long long exact = 0;
    for (const auto& row : golden::rows("eq/cases.tsv")) {
        REQUIRE(row.size() == 4 + 5 * kEqBands);
        const std::string& name = row[0];
        const double fs = golden::number(row[1]);
        const int channels = static_cast<int>(golden::number(row[2]));
        const int frames = static_cast<int>(golden::number(row[3]));
        const EqBands bands = bandsOf(row, 4);
        const Channels expected = golden::planar("eq/" + name + ".out.f32", channels, frames);
        for (const int block : { 128, 1, 4096 }) {
            CAPTURE(name);
            CAPTURE(block);
            Eq eq;
            eq.prepare(fs, bands);
            Channels x = goldenInput(channels, frames);
            run(eq, x, block);
            double diff = 0;
            for (size_t c = 0; c < x.size(); ++c)
                for (size_t i = 0; i < x[c].size(); ++i) {
                    diff = std::max(diff, std::fabs(static_cast<double>(x[c][i]) - expected[c][i]));
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
