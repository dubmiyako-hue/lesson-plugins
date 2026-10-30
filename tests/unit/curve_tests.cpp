// AC-17: the EQ curve the plugin's screen draws is the sound the EQ makes. tests/golden/curve (made by
// tests/golden/gen-curve.mjs) holds, per case, the screen's own product of the 13 biquads' getFrequencyResponse in Edge at
// a set of frequencies. Here the core's coefficients (eqCoefficients) give |H(e^jw)| of each stage in double; the product
// of the 13 is compared with the screen's, in dB, after both are clamped to the range the screen draws (plugin/web/src/ui/
// Eq.tsx: RANGE 18 plus 2 dB each way).
#include "Golden.h"
#include <doctest.h>
#include <lesson/Eq.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

using namespace lesson;

namespace {
// Eq.tsx clamps the curve to [-RANGE - 2, RANGE + 2] with RANGE = 18.
constexpr double kClampDb = 20.0;
// Provisional (not measured when it was written): the largest difference between the drawn values, in dB.
constexpr double kToleranceDb = 0.01;
constexpr int kPoints = 512 + kEqBands; // gen-curve.mjs: 512 log points and the 7 bands' own frequencies

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

// |H(e^jw)| of one stage, by the transfer function itself (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2), in double.
double magnitude(const BiquadCoefficients& c, double omega) {
    const std::complex<double> z1 = std::polar(1.0, -omega);
    const std::complex<double> z2 = z1 * z1;
    return std::abs((c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2));
}

double toDb(double magnitude) {
    return 20.0 * std::log10(magnitude); // 0 gives -infinity, which the clamp takes to the floor, as the screen's
}
double clamped(double db) {
    return std::clamp(db, -kClampDb, kClampDb);
}
}

TEST_CASE("[curve] the screen's EQ curve is the core's frequency response") {
    double worst = 0.0; // of the drawn (clamped) values
    std::string worstAt;
    double worstInside = 0.0; // of the values before the clamp, where both lie inside the range
    std::string worstInsideAt;
    double reach = 0.0; // the largest |dB| the screen drew, so that flat data cannot pass
    long long points = 0;
    long long inside = 0;
    std::array<bool, 4> rates {};
    const auto rows = golden::rows("curve/cases.tsv");
    for (const auto& row : rows) {
        REQUIRE(row.size() == 2 + 5 * kEqBands);
        const std::string& name = row[0];
        const double fs = golden::number(row[1]);
        rates[fs == 44100 ? 0 : fs == 48000 ? 1 : fs == 96000 ? 2 : fs == 192000 ? 3 : 0] = true;
        const EqBands bands = bandsOf(row, 2);
        const auto freqs = golden::planar("curve/" + name + ".freq.f32", 1, kPoints)[0];
        const auto mags = golden::planar("curve/" + name + ".mag.f32", 1, kPoints)[0];

        std::array<BiquadCoefficients, kEqStages> stages;
        eqCoefficients(bands, fs, stages);

        for (size_t k = 0; k < freqs.size(); ++k) {
            const double f = static_cast<double>(freqs[k]);
            const double omega = 2.0 * std::numbers::pi * f / fs;
            double product = 1.0;
            for (const auto& stage : stages) product *= magnitude(stage, omega);
            const double ours = toDb(product);
            const double screen = toDb(static_cast<double>(mags[k]));
            REQUIRE_MESSAGE(!std::isnan(ours), name << " at " << f << " Hz");
            REQUIRE_MESSAGE(!std::isnan(screen), name << " at " << f << " Hz");

            const std::string at = name + " (fs " + std::to_string(static_cast<int>(fs)) + ") at " + std::to_string(f) + " Hz";
            const double diff = std::fabs(clamped(ours) - clamped(screen));
            if (diff > worst) {
                worst = diff;
                worstAt = at;
            }
            reach = std::max(reach, std::fabs(clamped(screen)));
            if (std::fabs(ours) <= kClampDb && std::fabs(screen) <= kClampDb) {
                ++inside;
                const double raw = std::fabs(ours - screen);
                if (raw > worstInside) {
                    worstInside = raw;
                    worstInsideAt = at;
                }
            }
            ++points;
        }
    }
    MESSAGE("cases " << rows.size() << ", points " << points << " (" << inside << " inside the range)");
    MESSAGE("largest drawn difference " << worst << " dB at " << worstAt);
    MESSAGE("largest difference before the clamp, inside the range: " << worstInside << " dB at " << worstInsideAt);
    CHECK(worst <= kToleranceDb);
    // The data is not vacuous: every sample rate is used, the curves reach the clamp, and most points are inside the range.
    for (const bool used : rates) CHECK(used);
    CHECK(reach >= kClampDb - 1e-9);
    CHECK(inside > points / 2);
}
