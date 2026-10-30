#!/usr/bin/env python
"""Writes tests/golden/autogain.tsv: the EQ Auto Gain of docs/dev-loop/requirements.md "Auto Gain の式", for core_tests
"[autogain]" (AC-10).

An independent reference: it does not share a line with core/src. It is the W3C Web Audio BiquadFilterNode formulas
(Audio EQ Cookbook) in double precision, the K-weighting of mix-course src/dsp.ts (kWeighting), and the Butterworth Qs of
butterworthQs, summed over the centres of 2048 equal log steps from 20 Hz to 20 kHz:
    makeup = clamp(-10 log10( sum |H|^2 |K|^2 / sum |K|^2 ), -24, 24)
H is the product of the stages EqFilters.active keeps (HP and LP on: the first slope / 12 stages; shelves and bells on with
gain != 0).

Row (tab separated): name, fs, 7 bands x (on 0/1, freq, gain, q, slope) in the order HP, LS, Bell 1, Bell 2, Bell 3, HS, LP,
makeup dB. The inputs are rounded to float32 first, and written as that float32's exact value, so a C++ float reads back
the same number the reference used.

Usage: python tests/golden/gen-autogain.py      (numpy; writes next to this file)
"""
import math
import pathlib

import numpy as np

POINTS = 2048
LOW_HZ, HIGH_HZ = 20.0, 20000.0
LIMIT_DB = 24.0
HP, LS, B1, B2, B3, HS, LP = range(7)
KINDS = ["hp", "ls", "bell", "bell", "bell", "hs", "lp"]
OUT = pathlib.Path(__file__).resolve().parent / "autogain.tsv"


def f32(x):
    return float(np.float32(x))


# ---- the web app's pieces (src/dsp.ts) ----

def butterworth_qs(order):
    n = order // 2
    return [1.0 / (2.0 * math.cos((2 * k + 1) * math.pi / (4 * n))) for k in range(n)]


def k_weighting(fs):
    f0, g, q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    k = math.tan(math.pi * f0 / fs)
    vh = 10 ** (g / 20)
    vb = vh ** 0.4996667741545416
    a0 = 1 + k / q + k * k
    shelf = ([(vh + vb * k / q + k * k) / a0, 2 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0],
             [1.0, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0])
    f1, q1 = 38.13547087602444, 0.5003270373238773
    k1 = math.tan(math.pi * f1 / fs)
    d = 1 + k1 / q1 + k1 * k1
    high = ([1.0, -2.0, 1.0], [1.0, 2 * (k1 * k1 - 1) / d, (1 - k1 / q1 + k1 * k1) / d])
    return [shelf, high]


# ---- W3C Web Audio BiquadFilterNode (Audio EQ Cookbook), normalised so that a0 = 1 ----

def cookbook(kind, fs, freq, gain_db, q_value):
    """kind: lowpass | highpass | peaking | lowshelf | highshelf. q_value: dB for lowpass/highpass, linear for peaking,
    ignored for the shelves (S = 1). Returns (b, a)."""
    w0 = 2 * math.pi * freq / fs
    c, s = math.cos(w0), math.sin(w0)
    a_lin = 10 ** (gain_db / 40)
    if kind == "lowpass":
        alpha = s / (2 * 10 ** (q_value / 20))
        b = [(1 - c) / 2, 1 - c, (1 - c) / 2]
        a = [1 + alpha, -2 * c, 1 - alpha]
    elif kind == "highpass":
        alpha = s / (2 * 10 ** (q_value / 20))
        b = [(1 + c) / 2, -(1 + c), (1 + c) / 2]
        a = [1 + alpha, -2 * c, 1 - alpha]
    elif kind == "peaking":
        alpha = s / (2 * q_value)
        b = [1 + alpha * a_lin, -2 * c, 1 - alpha * a_lin]
        a = [1 + alpha / a_lin, -2 * c, 1 - alpha / a_lin]
    elif kind in ("lowshelf", "highshelf"):
        alpha = s / 2 * math.sqrt((a_lin + 1 / a_lin) * (1 / 1.0 - 1) + 2)  # S = 1
        t = 2 * math.sqrt(a_lin) * alpha
        if kind == "lowshelf":
            b = [a_lin * ((a_lin + 1) - (a_lin - 1) * c + t),
                 2 * a_lin * ((a_lin - 1) - (a_lin + 1) * c),
                 a_lin * ((a_lin + 1) - (a_lin - 1) * c - t)]
            a = [(a_lin + 1) + (a_lin - 1) * c + t,
                 -2 * ((a_lin - 1) + (a_lin + 1) * c),
                 (a_lin + 1) + (a_lin - 1) * c - t]
        else:
            b = [a_lin * ((a_lin + 1) + (a_lin - 1) * c + t),
                 -2 * a_lin * ((a_lin - 1) + (a_lin + 1) * c),
                 a_lin * ((a_lin + 1) + (a_lin - 1) * c - t)]
            a = [(a_lin + 1) - (a_lin - 1) * c + t,
                 2 * ((a_lin - 1) - (a_lin + 1) * c),
                 (a_lin + 1) - (a_lin - 1) * c - t]
    else:
        raise ValueError(kind)
    return [x / a[0] for x in b], [x / a[0] for x in a]


def mag2(ba, w):
    """|H(e^jw)|^2 of (b, a) over the array w."""
    b, a = ba
    z1 = np.exp(-1j * w)
    z2 = z1 * z1
    num = b[0] + b[1] * z1 + b[2] * z2
    den = a[0] + a[1] * z1 + a[2] * z2
    return (num.real ** 2 + num.imag ** 2) / (den.real ** 2 + den.imag ** 2)


def active_stages(bands, fs):
    """EqFilters.active: the stages that change the signal, as (b, a)."""
    stages = []
    for i, (on, freq, gain, q, slope) in enumerate(bands):
        kind = KINDS[i]
        if not on:
            continue
        if kind in ("hp", "lp"):
            for qq in butterworth_qs(slope // 6)[: slope // 12]:
                stages.append(cookbook("highpass" if kind == "hp" else "lowpass", fs, freq, 0.0, 20 * math.log10(qq)))
        elif gain != 0:
            if kind == "ls":
                stages.append(cookbook("lowshelf", fs, freq, gain, 1.0))
            elif kind == "hs":
                stages.append(cookbook("highshelf", fs, freq, gain, 1.0))
            else:
                stages.append(cookbook("peaking", fs, freq, gain, q))
    return stages


def raw_makeup(fs, bands):
    i = np.arange(POINTS)
    f = LOW_HZ * (HIGH_HZ / LOW_HZ) ** ((i + 0.5) / POINTS)  # 20 * 1000^((i + 0.5) / 2048)
    w = 2 * np.pi * f / fs
    k2 = np.ones(POINTS)
    for ba in k_weighting(fs):
        k2 *= mag2(ba, w)
    h2 = np.ones(POINTS)
    for ba in active_stages(bands, fs):
        h2 *= mag2(ba, w)
    return -10 * math.log10(np.sum(h2 * k2) / np.sum(k2))


def makeup(fs, bands):
    return max(-LIMIT_DB, min(LIMIT_DB, raw_makeup(fs, bands)))


# ---- cases ----

def band(on, freq, gain=0.0, q=1.0, slope=12):
    return (1 if on else 0, f32(freq), f32(gain), f32(q), int(slope))


def defaults():
    """The web app's DEFAULTS (src/chain.ts)."""
    return [band(False, 80, slope=24), band(True, 120), band(True, 300), band(True, 1000), band(True, 4000),
            band(True, 8000), band(False, 18000, slope=12)]


def with_band(bands, index, new):
    out = list(bands)
    out[index] = new
    return out


def cases():
    d = defaults()
    yield "example-bell-300-p12", 48000, with_band(d, B1, band(True, 300, 12, 1)), -3.08
    yield "example-hs-8k-p6", 48000, with_band(d, HS, band(True, 8000, 6, 1)), -2.14
    yield "example-hp-80-24", 48000, with_band(d, HP, band(True, 80, slope=24)), 0.20
    yield "defaults", 48000, d, 0.0
    yield "defaults-44100", 44100, d, 0.0
    yield "all-off-with-gains", 48000, [band(False, f, 12) for f in (80, 120, 300, 1000, 4000, 8000, 18000)], 0.0
    yield "on-with-zero-gain-odd-q", 96000, with_band(d, B2, band(True, 1500, 0, 7.5)), 0.0
    yield "all-off-gains-nonzero-q", 48000, [band(False, 100, 18, 0.3, 48), band(False, 200, 18), band(False, 400, -18, 12),
                                            band(False, 800, 18), band(False, 1600, 18), band(False, 3200, -18),
                                            band(False, 12800, 18, 1, 48)], 0.0
    # beyond +24: the loudness collapses (a 20 Hz lowpass and a 20 kHz highpass at 48 dB/oct)
    yield "clamp-plus-24", 48000, with_band(d, LP, band(True, 20, slope=48)), None
    yield "clamp-plus-24-hp", 44100, with_band(d, HP, band(True, 20000, slope=48)), None
    # beyond -24: every gain stage boosts +18 dB
    boost = [band(False, 80, slope=24), band(True, 100, 18), band(True, 500, 18, 0.3), band(True, 1000, 18, 0.3),
             band(True, 3000, 18, 0.3), band(True, 9000, 18), band(False, 18000)]
    yield "clamp-minus-24", 48000, boost, None
    # edges of the stage counts
    for slope in (12, 24, 36, 48):
        yield f"hp-1000-{slope}", 48000, with_band(d, HP, band(True, 1000, slope=slope)), None
        yield f"lp-4000-{slope}", 44100, with_band(d, LP, band(True, 4000, slope=slope)), None
    yield "bell-neg18-q12-19k", 44100, with_band(d, B3, band(True, 19000, -18, 12)), None
    yield "bell-pos18-q03-20hz", 96000, with_band(d, B1, band(True, 20, 18, 0.3)), None

    rng = np.random.default_rng(20261031)
    for n in range(40):
        fs = int(rng.choice([44100, 48000, 96000]))
        bands = []
        for i, kind in enumerate(KINDS):
            on = bool(rng.random() < 0.7)
            freq = f32(20 * 1000 ** rng.random())
            gain = f32(rng.integers(-36, 37) * 0.5)
            q = f32(0.3 * 40 ** rng.random())
            slope = int(rng.choice([12, 24, 36, 48])) if kind in ("hp", "lp") else 12
            bands.append(band(on, freq, gain, q, slope))
        yield f"random-{n:02d}", fs, bands, None


def main():
    lines = []
    for name, fs, bands, expected in cases():
        raw = raw_makeup(fs, bands)
        value = max(-LIMIT_DB, min(LIMIT_DB, raw)) + 0.0  # + 0.0: no negative zero
        if expected is not None:
            assert abs(value - expected) <= 0.01, (name, value, expected)
        if name == "clamp-plus-24":
            assert raw > LIMIT_DB, (name, raw)
        if name == "clamp-plus-24-hp":
            assert raw > LIMIT_DB, (name, raw)
        if name == "clamp-minus-24":
            assert raw < -LIMIT_DB, (name, raw)
        fields = [name, str(fs)]
        for on, freq, gain, q, slope in bands:
            fields += [str(on), repr(freq), repr(gain), repr(q), str(slope)]
        fields.append(repr(value))
        lines.append("\t".join(fields))
        print(f"{name:28s} fs={fs:6d} raw={raw:10.4f} makeup={value:9.4f}")
    OUT.write_text("\n".join(lines) + "\n", newline="\n")
    print(f"{len(lines)} rows -> {OUT}")


if __name__ == "__main__":
    main()
