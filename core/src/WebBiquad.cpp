#include <lesson/Fdlibm.h>
#include <lesson/WebBiquad.h>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace lesson {

namespace {
// Blink's pow10: fdlibm::expf of the product rounded to float, so the result has a float's precision.
double pow10(double x) noexcept {
    return fdlibm::expf(static_cast<float>(x * std::numbers::ln10));
}

// Biquad::SetNormalizedCoefficients multiplies by 1 / a0.
BiquadCoefficients normalized(double b0, double b1, double b2, double a0, double a1, double a2) noexcept {
    const double a0Inverse = 1 / a0;
    return { b0 * a0Inverse, b1 * a0Inverse, b2 * a0Inverse, a1 * a0Inverse, a2 * a0Inverse };
}

BiquadCoefficients lowpass(double cutoff, double resonance) noexcept {
    cutoff = std::clamp(cutoff, 0.0, 1.0);
    if (cutoff == 1) return normalized(1, 0, 0, 1, 0, 0);
    if (cutoff > 0) {
        resonance = pow10(resonance / 20);
        const double theta = std::numbers::pi * cutoff;
        const double alpha = fdlibm::sin(theta) / (2 * resonance);
        const double cosw = fdlibm::cos(theta);
        const double beta = (1 - cosw) / 2;
        return normalized(beta, 2 * beta, beta, 1 + alpha, -2 * cosw, 1 - alpha);
    }
    return normalized(0, 0, 0, 1, 0, 0);
}

BiquadCoefficients highpass(double cutoff, double resonance) noexcept {
    cutoff = std::clamp(cutoff, 0.0, 1.0);
    if (cutoff == 1) return normalized(0, 0, 0, 1, 0, 0);
    if (cutoff > 0) {
        resonance = pow10(resonance / 20);
        const double theta = std::numbers::pi * cutoff;
        const double alpha = fdlibm::sin(theta) / (2 * resonance);
        const double cosw = fdlibm::cos(theta);
        const double beta = (1 + cosw) / 2;
        return normalized(beta, -2 * beta, beta, 1 + alpha, -2 * cosw, 1 - alpha);
    }
    return normalized(1, 0, 0, 1, 0, 0);
}

BiquadCoefficients lowShelf(double frequency, double dbGain) noexcept {
    frequency = std::clamp(frequency, 0.0, 1.0);
    const double a = pow10(dbGain / 40);
    if (frequency == 1) return normalized(a * a, 0, 0, 1, 0, 0);
    if (frequency > 0) {
        const double w0 = std::numbers::pi * frequency;
        const double s = 1; // filter slope (1 is max value)
        const double alpha = 0.5 * fdlibm::sin(w0) * std::sqrt((a + 1 / a) * (1 / s - 1) + 2);
        const double k = fdlibm::cos(w0);
        const double k2 = 2 * std::sqrt(a) * alpha;
        const double aPlusOne = a + 1;
        const double aMinusOne = a - 1;
        const double b0 = a * (aPlusOne - aMinusOne * k + k2);
        const double b1 = 2 * a * (aMinusOne - aPlusOne * k);
        const double b2 = a * (aPlusOne - aMinusOne * k - k2);
        const double a0 = aPlusOne + aMinusOne * k + k2;
        const double a1 = -2 * (aMinusOne + aPlusOne * k);
        const double a2 = aPlusOne + aMinusOne * k - k2;
        return normalized(b0, b1, b2, a0, a1, a2);
    }
    return normalized(1, 0, 0, 1, 0, 0);
}

BiquadCoefficients highShelf(double frequency, double dbGain) noexcept {
    frequency = std::clamp(frequency, 0.0, 1.0);
    const double a = pow10(dbGain / 40);
    if (frequency == 1) return normalized(1, 0, 0, 1, 0, 0);
    if (frequency > 0) {
        const double w0 = std::numbers::pi * frequency;
        const double s = 1; // filter slope (1 is max value)
        const double alpha = 0.5 * fdlibm::sin(w0) * std::sqrt((a + 1 / a) * (1 / s - 1) + 2);
        const double k = fdlibm::cos(w0);
        const double k2 = 2 * std::sqrt(a) * alpha;
        const double aPlusOne = a + 1;
        const double aMinusOne = a - 1;
        const double b0 = a * (aPlusOne + aMinusOne * k + k2);
        const double b1 = -2 * a * (aMinusOne + aPlusOne * k);
        const double b2 = a * (aPlusOne + aMinusOne * k - k2);
        const double a0 = aPlusOne - aMinusOne * k + k2;
        const double a1 = 2 * (aMinusOne - aPlusOne * k);
        const double a2 = aPlusOne - aMinusOne * k - k2;
        return normalized(b0, b1, b2, a0, a1, a2);
    }
    return normalized(a * a, 0, 0, 1, 0, 0);
}

BiquadCoefficients peaking(double frequency, double q, double dbGain) noexcept {
    frequency = std::clamp(frequency, 0.0, 1.0);
    q = std::max(0.0, q);
    const double a = pow10(dbGain / 40);
    if (frequency > 0 && frequency < 1) {
        if (q > 0) {
            const double w0 = std::numbers::pi * frequency;
            const double alpha = fdlibm::sin(w0) / (2 * q);
            const double k = fdlibm::cos(w0);
            return normalized(1 + alpha * a, -2 * k, 1 - alpha * a, 1 + alpha / a, -2 * k, 1 - alpha / a);
        }
        return normalized(a * a, 0, 0, 1, 0, 0);
    }
    return normalized(1, 0, 0, 1, 0, 0);
}
}

BiquadCoefficients webBiquadCoefficients(BiquadType type, float frequency, float q, float gain, double nyquist) noexcept {
    const double normalizedFrequency = frequency / nyquist;
    switch (type) {
    case BiquadType::Lowpass: return lowpass(normalizedFrequency, q);
    case BiquadType::Highpass: return highpass(normalizedFrequency, q);
    case BiquadType::LowShelf: return lowShelf(normalizedFrequency, gain);
    case BiquadType::HighShelf: return highShelf(normalizedFrequency, gain);
    case BiquadType::Peaking: return peaking(normalizedFrequency, q, gain);
    }
    return {};
}

} // namespace lesson
