// AC-10: the Auto Gain amounts against an independent reference (tests/golden/gen-autogain.py, which writes
// tests/golden/autogain.tsv) and the examples of requirements.md "Auto Gain の式".
#include "Golden.h"
#include <doctest.h>
#include <lesson/AutoGain.h>
#include <lesson/Compressor.h>
#include <lesson/Eq.h>
#include <cmath>
#include <string>
#include <vector>

using namespace lesson;

namespace {
// 0.01 dB: provisional (AC-10), the requirement's figure
constexpr double kToleranceDb = 0.01;
constexpr int kTicks = EqLoudness::kPoints / EqLoudness::kPointsPerSample; // 512

enum Band { HP, LS, Bell1, Bell2, Bell3, HS, LP };

struct Row {
    std::string name;
    double fs = 0;
    EqBands bands {};
    double expected = 0;
};

Row parse(const std::vector<std::string>& fields) {
    REQUIRE(fields.size() == 2 + 5 * kEqBands + 1);
    Row row;
    row.name = fields[0];
    row.fs = golden::number(fields[1]);
    for (int i = 0; i < kEqBands; ++i) {
        const size_t at = 2 + 5 * static_cast<size_t>(i);
        EqBandSettings& b = row.bands[static_cast<size_t>(i)];
        b.on = golden::number(fields[at]) != 0.0;
        b.freq = static_cast<float>(golden::number(fields[at + 1]));
        b.gain = static_cast<float>(golden::number(fields[at + 2]));
        b.q = static_cast<float>(golden::number(fields[at + 3]));
        b.slope = static_cast<int>(golden::number(fields[at + 4]));
    }
    row.expected = golden::number(fields.back());
    return row;
}

// The web app's DEFAULTS (src/chain.ts): HP off 80 Hz 24 dB/oct, LS on 120, bells on 300 / 1000 / 4000, HS on 8000,
// LP off 18000 Hz 12 dB/oct; the gains 0, Q 1.
EqBands defaults() {
    EqBands bands {};
    const float freq[kEqBands] = { 80, 120, 300, 1000, 4000, 8000, 18000 };
    const bool on[kEqBands] = { false, true, true, true, true, true, false };
    for (int i = 0; i < kEqBands; ++i) {
        EqBandSettings& b = bands[static_cast<size_t>(i)];
        b.on = on[i];
        b.freq = freq[i];
        b.gain = 0.0f;
        b.q = 1.0f;
        b.slope = (i == HP) ? 24 : 12;
    }
    return bands;
}

// A setting that moves the amount well away from the defaults'.
EqBands changed() {
    EqBands bands = defaults();
    bands[Bell2].gain = 18.0f;
    bands[Bell2].q = 0.5f;
    bands[LP].on = true;
    bands[LP].freq = 3000.0f;
    bands[LP].slope = 24;
    return bands;
}
EqBands changedAgain() {
    EqBands bands = defaults();
    bands[HP].on = true;
    bands[HP].freq = 200.0f;
    bands[HP].slope = 36;
    bands[HS].gain = -7.5f;
    bands[Bell3].gain = 12.0f;
    return bands;
}

// Calls tick() up to `limit` times; returns the number of the call that returned true, or 0.
int ticksUntilDone(EqLoudness& loudness, int limit) {
    for (int n = 1; n <= limit; ++n)
        if (loudness.tick()) return n;
    return 0;
}
}

TEST_CASE("[autogain] the EQ amount against the reference over every row of autogain.tsv") {
    double worst = 0;
    std::string worstName;
    int count = 0;
    for (const auto& fields : golden::rows("autogain.tsv")) {
        const Row row = parse(fields);
        EqLoudness loudness;
        loudness.prepare(row.fs, row.bands);
        const double got = loudness.makeupDb();
        const double diff = std::fabs(got - row.expected);
        if (diff > worst) {
            worst = diff;
            worstName = row.name;
        }
        CAPTURE(row.name);
        CAPTURE(row.fs);
        CAPTURE(row.expected);
        CAPTURE(got);
        CHECK(diff <= kToleranceDb);
        // prepare computes the settings it is given at once, so compute() gives the same number
        CHECK(loudness.compute(row.bands) == got);
        CHECK_FALSE(loudness.busy());
        CHECK(std::fabs(got) <= kMaxMakeupDb);
        ++count;
    }
    CHECK(count >= 30);
    MESSAGE("autogain.tsv: " << count << " rows, the largest difference " << worst << " dB (" << worstName << ")");
}

TEST_CASE("[autogain] the examples of the requirements at 48 kHz with the other bands at the defaults") {
    EqLoudness loudness;
    SUBCASE("bell 300 Hz +12 dB Q 1 gives -3.08 dB") {
        EqBands bands = defaults();
        bands[Bell1].gain = 12.0f;
        loudness.prepare(48000.0, bands);
        CHECK(std::fabs(loudness.makeupDb() - (-3.08)) <= kToleranceDb);
    }
    SUBCASE("high shelf 8 kHz +6 dB gives -2.14 dB") {
        EqBands bands = defaults();
        bands[HS].gain = 6.0f;
        loudness.prepare(48000.0, bands);
        CHECK(std::fabs(loudness.makeupDb() - (-2.14)) <= kToleranceDb);
    }
    SUBCASE("highpass 80 Hz 24 dB/oct gives +0.20 dB") {
        EqBands bands = defaults();
        bands[HP].on = true;
        bands[HP].freq = 80.0f;
        bands[HP].slope = 24;
        loudness.prepare(48000.0, bands);
        CHECK(std::fabs(loudness.makeupDb() - 0.20) <= kToleranceDb);
    }
}

TEST_CASE("[autogain] defaults and bands that do nothing give 0 dB") {
    EqLoudness loudness;
    for (const double fs : { 44100.0, 48000.0, 96000.0 }) {
        CAPTURE(fs);
        loudness.prepare(fs, defaults());
        CHECK(std::fabs(loudness.makeupDb()) <= 1e-6);
        // off bands with gains, and on bands with no gain, change nothing
        EqBands bands = defaults();
        bands[HP].gain = 12.0f;
        bands[LP].gain = -12.0f;
        bands[Bell1].on = false;
        bands[Bell1].gain = 18.0f;
        bands[Bell2].gain = 0.0f;
        bands[Bell2].q = 12.0f;
        loudness.prepare(fs, bands);
        CHECK(std::fabs(loudness.makeupDb()) <= 1e-6);
    }
}

TEST_CASE("[autogain] the amount is clamped to +-24 dB") {
    EqLoudness loudness;
    EqBands up = defaults();
    up[LP].on = true;
    up[LP].freq = 20.0f;
    up[LP].slope = 48;
    loudness.prepare(48000.0, up);
    CHECK(loudness.makeupDb() == doctest::Approx(24.0).epsilon(1e-9));

    EqBands down = defaults();
    down[LS].gain = 18.0f;
    down[HS].gain = 18.0f;
    for (const int bell : { Bell1, Bell2, Bell3 }) {
        down[static_cast<size_t>(bell)].gain = 18.0f;
        down[static_cast<size_t>(bell)].q = 0.3f;
    }
    loudness.prepare(48000.0, down);
    CHECK(loudness.makeupDb() == doctest::Approx(-24.0).epsilon(1e-9));
}

TEST_CASE("[autogain] the incremental computation finishes on call kPoints / kPointsPerSample") {
    static_assert(EqLoudness::kPoints == 2048 && EqLoudness::kPointsPerSample == 4);
    EqLoudness loudness;
    loudness.prepare(48000.0, defaults());
    const double before = loudness.makeupDb();
    CHECK_FALSE(loudness.busy());

    const EqBands target = changed();
    const double expected = loudness.compute(target);
    CHECK(std::fabs(expected - before) > 0.5); // the setting does change the amount, so the test can tell
    CHECK(loudness.makeupDb() == before);      // compute() does not touch the running state

    loudness.request(target);
    CHECK(loudness.busy());
    CHECK(loudness.makeupDb() == before); // nothing finished yet
    for (int n = 1; n < kTicks; ++n) {
        CAPTURE(n);
        REQUIRE_FALSE(loudness.tick());
        REQUIRE(loudness.makeupDb() == before);
        REQUIRE(loudness.busy());
    }
    CHECK(loudness.tick()); // call number 512
    CHECK_FALSE(loudness.busy());
    CHECK(loudness.makeupDb() == expected); // exactly what compute() gives
    CHECK_FALSE(loudness.tick());           // nothing left to compute
    CHECK(loudness.makeupDb() == expected);
}

TEST_CASE("[autogain] a request made while a computation runs is computed after it") {
    EqLoudness loudness;
    loudness.prepare(48000.0, defaults());
    const double start = loudness.makeupDb();
    const EqBands first = changed();
    const EqBands second = changedAgain();
    const double firstValue = loudness.compute(first);
    const double secondValue = loudness.compute(second);
    REQUIRE(std::fabs(firstValue - secondValue) > 0.5);
    REQUIRE(std::fabs(firstValue - start) > 0.5);

    loudness.request(first);
    for (int n = 0; n < 100; ++n) REQUIRE_FALSE(loudness.tick());
    loudness.request(second); // while busy
    CHECK(loudness.busy());
    CHECK(loudness.makeupDb() == start);

    // the running computation is not cut short: it finishes on its own 512th call, with the first setting's result
    for (int n = 101; n < kTicks; ++n) REQUIRE_FALSE(loudness.tick());
    CHECK(loudness.tick());
    CHECK(loudness.makeupDb() == firstValue);
    CHECK(loudness.busy()); // the second one has begun

    // and the second one's result appears 512 calls later
    for (int n = 1; n < kTicks; ++n) {
        CAPTURE(n);
        REQUIRE_FALSE(loudness.tick());
        REQUIRE(loudness.makeupDb() == firstValue);
    }
    CHECK(loudness.tick());
    CHECK(loudness.makeupDb() == secondValue);
    CHECK_FALSE(loudness.busy());
    CHECK_FALSE(loudness.tick());
}

TEST_CASE("[autogain] the incremental result agrees with the reference") {
    // Some reference rows that change the amount, run through request() and tick() from the defaults.
    int checked = 0;
    for (const auto& fields : golden::rows("autogain.tsv")) {
        const Row row = parse(fields);
        if (row.name.rfind("random-0", 0) != 0) continue;
        EqLoudness loudness;
        loudness.prepare(row.fs, defaults());
        loudness.request(row.bands);
        CAPTURE(row.name);
        CHECK(ticksUntilDone(loudness, kTicks) == kTicks);
        CHECK(std::fabs(loudness.makeupDb() - row.expected) <= kToleranceDb);
        ++checked;
    }
    CHECK(checked >= 5);
}

TEST_CASE("[autogain] comp is half the GR of a 0 dBFS sound and at most 24 dB") {
    CHECK(kRatioInfinite == 1000.0);
    // targetGr(0, T, R) / 2
    CHECK(std::fabs(compMakeupDb(-28.0f, 4.0f) - 10.5) <= kToleranceDb);
    CHECK(std::fabs(compMakeupDb(-20.0f, 4.0f) - 7.5) <= kToleranceDb);
    CHECK(std::fabs(compMakeupDb(0.0f, 4.0f) - 0.0) <= kToleranceDb);       // nothing over the threshold
    CHECK(std::fabs(compMakeupDb(-30.0f, 1.0f) - 0.0) <= kToleranceDb);     // 1:1 does not compress
    CHECK(std::fabs(compMakeupDb(-60.0f, 1000.0f) - 24.0) <= kToleranceDb); // 30 dB, capped
    // above 0 dBFS the threshold is never reached
    CHECK(std::fabs(compMakeupDb(6.0f, 4.0f) - 0.0) <= kToleranceDb);
    // ∞:1 takes all of what is over; 999:1 still lets a little through
    CHECK(std::fabs(compMakeupDb(-10.0f, static_cast<float>(kRatioInfinite)) - 5.0) <= kToleranceDb);
    CHECK(std::fabs(compMakeupDb(-10.0f, 999.0f) - 5.0 * (1.0 - 1.0 / 999.0)) <= kToleranceDb);
    // at the cap and just under it
    CHECK(std::fabs(compMakeupDb(-48.0f, static_cast<float>(kRatioInfinite)) - 24.0) <= kToleranceDb);
    CHECK(std::fabs(compMakeupDb(-47.0f, static_cast<float>(kRatioInfinite)) - 23.5) <= kToleranceDb);
}
