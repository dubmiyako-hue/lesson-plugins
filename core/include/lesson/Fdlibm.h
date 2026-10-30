#pragma once
// fdlibm's exp, sin and cos as Chromium builds them (see src/Fdlibm.cpp for the origin and the licence), so that the
// biquad coefficients are Blink's to the bit.

namespace lesson::fdlibm {

double exp(double x);
double sin(double x);
double cos(double x);
// Blink's fdlibm::expf: exp in double, rounded to float.
float expf(float x);

} // namespace lesson::fdlibm
