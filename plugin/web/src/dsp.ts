// Pure math shared by the page, the worklet and the tests. No Web Audio here.

export const dbToGain = (db: number) => 10 ** (db / 20)
export const gainToDb = (g: number) => 20 * Math.log10(g)

export type Biquad = { b: [number, number, number]; a: [number, number, number] }

// ITU-R BS.1770 K-weighting for any sample rate (libebur128 / pyloudnorm design).
export function kWeighting(fs: number): [Biquad, Biquad] {
  const f0 = 1681.974450955533
  const G = 3.999843853973347
  const Q = 0.7071752369554196
  const K = Math.tan((Math.PI * f0) / fs)
  const Vh = 10 ** (G / 20)
  const Vb = Vh ** 0.4996667741545416
  const a0 = 1 + K / Q + K * K
  const shelf: Biquad = {
    b: [(Vh + (Vb * K) / Q + K * K) / a0, (2 * (K * K - Vh)) / a0, (Vh - (Vb * K) / Q + K * K) / a0],
    a: [1, (2 * (K * K - 1)) / a0, (1 - K / Q + K * K) / a0],
  }
  const f1 = 38.13547087602444
  const Q1 = 0.5003270373238773
  const K1 = Math.tan((Math.PI * f1) / fs)
  const d = 1 + K1 / Q1 + K1 * K1
  const highpass: Biquad = {
    b: [1, -2, 1],
    a: [1, (2 * (K1 * K1 - 1)) / d, (1 - K1 / Q1 + K1 * K1) / d],
  }
  return [shelf, highpass]
}

function runBiquad(x: Float32Array, f: Biquad): Float32Array {
  const y = new Float32Array(x.length)
  const [b0, b1, b2] = f.b
  const [, a1, a2] = f.a
  let x1 = 0
  let x2 = 0
  let y1 = 0
  let y2 = 0
  for (let i = 0; i < x.length; i++) {
    const v = b0 * x[i] + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
    x2 = x1
    x1 = x[i]
    y2 = y1
    y1 = v
    y[i] = v
  }
  return y
}

// K-weighted loudness without gating (the loop is short). `from` skips a pre-roll.
export function loudness(channels: Float32Array[], fs: number, from = 0): number {
  const [shelf, hp] = kWeighting(fs)
  let sum = 0
  for (const ch of channels) {
    const k = runBiquad(runBiquad(ch, shelf), hp)
    let ms = 0
    for (let i = from; i < k.length; i++) ms += k[i] * k[i]
    sum += ms / Math.max(1, k.length - from)
  }
  return -0.691 + 10 * Math.log10(sum || 1e-20)
}

// The ratio that stands for ∞:1, a limiter. An AudioParam cannot hold Infinity, and
// a large finite ratio would still let a little through (100:1 lets 0.2 dB of 20 dB pass).
export const RATIO_INF = 1000

// dB the compressor wants to take away at `levelDb` (no knee, no makeup gain).
export function targetGr(levelDb: number, threshold: number, ratio: number): number {
  const over = Math.max(0, levelDb - threshold)
  return ratio >= RATIO_INF ? over : over - over / ratio
}

// Peak compressor, stereo-linked. The detector is the peak of |x| over the last
// ~20 ms (instant rise, 20 ms hold), so a steady sine reads its peak level and the
// GR stays flat. Attack and release smooth the GR in dB; 0 ms = instant.
export class CompressorCore {
  static readonly SUB = 48 // samples per sub-block of the hold window
  static readonly HOLD_MS = 20
  gr = 0
  private ring: Float32Array
  private ringPos = 0
  private ringMax = 0
  private partial = 0
  private partialCount = 0

  constructor(private fs: number) {
    const n = Math.max(1, Math.round(((CompressorCore.HOLD_MS / 1000) * fs) / CompressorCore.SUB))
    this.ring = new Float32Array(n)
  }

  // Processes in place. Returns the largest GR (dB) seen in this block.
  process(
    channels: Float32Array[],
    threshold: number,
    ratio: number,
    attackMs: number,
    releaseMs: number,
  ): number {
    const att = timeCoef(attackMs, this.fs)
    const rel = timeCoef(releaseMs, this.fs)
    const n = channels[0]?.length ?? 0
    let blockMax = 0
    for (let i = 0; i < n; i++) {
      let peak = 0
      for (const ch of channels) {
        const a = Math.abs(ch[i])
        if (a > peak) peak = a
      }
      if (peak > this.partial) this.partial = peak
      const level = this.partial > this.ringMax ? this.partial : this.ringMax
      if (++this.partialCount === CompressorCore.SUB) this.pushPartial()

      const levelDb = 20 * Math.log10(level > 1e-9 ? level : 1e-9)
      const target = targetGr(levelDb, threshold, ratio)
      const coef = target > this.gr ? att : rel
      this.gr = target + coef * (this.gr - target)
      const g = 10 ** (-this.gr / 20)
      for (const ch of channels) ch[i] *= g
      if (this.gr > blockMax) blockMax = this.gr
    }
    return blockMax
  }

  private pushPartial() {
    this.ring[this.ringPos] = this.partial
    this.ringPos = (this.ringPos + 1) % this.ring.length
    let m = 0
    for (let k = 0; k < this.ring.length; k++) if (this.ring[k] > m) m = this.ring[k]
    this.ringMax = m
    this.partial = 0
    this.partialCount = 0
  }
}

// Samples per point of the compressor display.
export const COMP_POINT = 256

// One-pole coefficient for a time constant in ms; 0 ms means instant.
export function timeCoef(ms: number, fs: number): number {
  return ms <= 0 ? 0 : Math.exp(-1 / ((ms / 1000) * fs))
}

// 4/4 only.
export function barsToSeconds(bpm: number, startBar: number, endBar: number) {
  const bar = (4 * 60) / bpm
  return { start: (startBar - 1) * bar, end: endBar * bar }
}

// Q of each 2nd-order stage of a Butterworth filter of `order` (even), so that a
// cascade of them is maximally flat with −3.01 dB at the corner.
export function butterworthQs(order: number): number[] {
  const n = order / 2
  return Array.from({ length: n }, (_, k) => 1 / (2 * Math.cos(((2 * k + 1) * Math.PI) / (4 * n))))
}

// ---------- tempo ----------

// In-place radix-2 FFT (re, im of a power-of-two length), with the twiddles of each size
// kept.
const twiddles = new Map<number, Float32Array>()
function fft(re: Float32Array, im: Float32Array) {
  const n = re.length
  let tw = twiddles.get(n)
  if (!tw) {
    tw = new Float32Array(n)
    for (let k = 0; k < n / 2; k++) {
      tw[2 * k] = Math.cos((-2 * Math.PI * k) / n)
      tw[2 * k + 1] = Math.sin((-2 * Math.PI * k) / n)
    }
    twiddles.set(n, tw)
  }
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1
    for (; j & bit; bit >>= 1) j ^= bit
    j ^= bit
    if (i < j) {
      ;[re[i], re[j]] = [re[j], re[i]]
      ;[im[i], im[j]] = [im[j], im[i]]
    }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const step = (n / len) * 2
    for (let i = 0; i < n; i += len) {
      for (let k = 0; k < len / 2; k++) {
        const cr = tw[k * step]
        const ci = tw[k * step + 1]
        const a = i + k
        const b = a + len / 2
        const tr = re[b] * cr - im[b] * ci
        const ti = re[b] * ci + im[b] * cr
        re[b] = re[a] - tr
        im[b] = im[a] - ti
        re[a] += tr
        im[a] += ti
      }
    }
  }
}

// Frames per second of the onset envelope.
export const ONSET_RATE = 100

// How strongly something starts at each frame: the rise of the log spectrum from one frame
// to the next (spectral flux), less its mean over the surrounding second, so that only
// attacks are left. Envelopes of several stems at any sample rates can be summed. Above
// 24 kHz the audio is first halved in rate (averaging pairs) until it is not: attacks need
// nothing above 12 kHz, and the FFT gets shorter.
export function onsetEnvelope(x: Float32Array, fs: number): Float32Array {
  while (fs > 24000) {
    const half = new Float32Array(x.length >> 1)
    for (let i = 0; i < half.length; i++) half[i] = (x[2 * i] + x[2 * i + 1]) / 2
    x = half
    fs /= 2
  }
  const hop = fs / ONSET_RATE
  let size = 512
  while (size < fs * 0.04) size *= 2
  const win = new Float32Array(size).map((_, i) => 0.5 - 0.5 * Math.cos((2 * Math.PI * i) / size))
  const frames = Math.max(0, Math.floor((x.length - size) / hop) + 1)
  const flux = new Float32Array(frames)
  const re = new Float32Array(size)
  const im = new Float32Array(size)
  let prev = new Float32Array(size / 2)
  let cur = new Float32Array(size / 2)
  for (let t = 0; t < frames; t++) {
    const at = Math.round(t * hop)
    for (let i = 0; i < size; i++) {
      re[i] = x[at + i] * win[i]
      im[i] = 0
    }
    fft(re, im)
    let sum = 0
    for (let k = 0; k < size / 2; k++) {
      cur[k] = Math.log1p(100 * Math.sqrt(re[k] * re[k] + im[k] * im[k]))
      if (t > 0 && cur[k] > prev[k]) sum += cur[k] - prev[k]
    }
    flux[t] = sum
    ;[prev, cur] = [cur, prev]
  }
  const half = ONSET_RATE / 2
  const out = new Float32Array(frames)
  let acc = 0
  for (let i = 0; i < Math.min(frames, half); i++) acc += flux[i]
  for (let t = 0; t < frames; t++) {
    if (t + half < frames) acc += flux[t + half]
    if (t - half - 1 >= 0) acc -= flux[t - half - 1]
    const n = Math.min(frames - 1, t + half) - Math.max(0, t - half) + 1
    out[t] = Math.max(0, flux[t] - acc / n)
  }
  return out
}

// The tempo of an onset envelope (ONSET_RATE frames a second), or undefined if it has no
// steady beat to go by. Each BPM from 40 to 240 is scored by the autocorrelation at 1 to 8
// beats (two bars of 4/4: a dotted-quarter rhythm, strong at 1.5 beats, falls off the bar
// lines there), less a part for a beat that splits in three rather than two, weighted toward
// 120 (one octave either side) to choose between half and double tempo, then refined on
// every beat that fits. Within 0.25 of a whole BPM it gives the
// whole number, otherwise one decimal.
export function estimateBpm(onsets: Float32Array): number | undefined {
  const n = onsets.length
  if (n < ONSET_RATE * 6) return undefined
  // a triangle 70 ms wide: the peaks of the autocorrelation get wider than a frame, so a
  // period between two frames is read between them instead of falling off its peak
  const env = new Float32Array(n)
  for (let i = 0; i < n; i++) for (let d = -3; d <= 3; d++) env[i] += (onsets[i + d] ?? 0) * (4 - Math.abs(d))
  const maxLag = Math.floor(n / 2)
  const ac = new Float32Array(maxLag + 2)
  for (let lag = 0; lag < ac.length; lag++) {
    let s = 0
    for (let i = 0; i + lag < n; i++) s += env[i] * env[i + lag]
    ac[lag] = s / (n - lag)
  }
  if (!(ac[0] > 1e-9)) return undefined
  const at = (lag: number) => {
    const i = Math.floor(lag)
    return i + 1 < ac.length ? ac[i] + (ac[i + 1] - ac[i]) * (lag - i) : 0
  }
  const period = (bpm: number) => (60 * ONSET_RATE) / bpm
  const comb = (bpm: number, beats: number) => {
    let s = 0
    let k = 1
    for (; k <= beats && k * period(bpm) <= maxLag; k++) s += at(k * period(bpm))
    return s / (k - 1 || 1)
  }
  // a beat of 4/4 splits in two: a period whose thirds are stronger than its half is three
  // pulses of a faster beat (98 for a song at 147 with even eighths)
  const triple = (bpm: number) => {
    const p = period(bpm)
    return Math.max(0, (at(p / 3) + at((2 * p) / 3)) / 2 - at(p / 2))
  }
  let best = 0
  let bestScore = -Infinity
  for (let bpm = 40; bpm <= 240; bpm += 0.05) {
    const s = (comb(bpm, 8) - 0.5 * triple(bpm)) * Math.exp(-0.5 * Math.log2(bpm / 120) ** 2)
    if (s > bestScore) [best, bestScore] = [bpm, s]
  }
  if (!(bestScore > 0.05 * ac[0])) return undefined
  let fine = best
  let fineScore = -Infinity
  for (let bpm = best * 0.99; bpm <= best * 1.01; bpm += 0.01) {
    const s = comb(bpm, Infinity)
    if (s > fineScore) [fine, fineScore] = [bpm, s]
  }
  return Math.abs(fine - Math.round(fine)) <= 0.25 ? Math.round(fine) : Math.round(fine * 10) / 10
}
