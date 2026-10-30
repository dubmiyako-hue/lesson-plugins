import { butterworthQs } from './dsp'

export type BandKind = 'hp' | 'ls' | 'bell' | 'hs' | 'lp'
export type Slope = 12 | 24 | 36 | 48
export type EqBand = { on: boolean; freq: number; gain: number; q: number; slope: Slope }

// The EQ's bands, in order: HP, low shelf, three bells, high shelf, LP.
export const BANDS: { name: string; kind: BandKind }[] = [
  { name: 'HP', kind: 'hp' },
  { name: 'LS', kind: 'ls' },
  { name: '1', kind: 'bell' },
  { name: '2', kind: 'bell' },
  { name: '3', kind: 'bell' },
  { name: 'HS', kind: 'hs' },
  { name: 'LP', kind: 'lp' },
]
export const SLOPES: Slope[] = [12, 24, 36, 48]
const STAGES = 4 // biquads per HP or LP, enough for 48 dB/oct

export type Settings = {
  volume: number // dB
  bands: EqBand[]
  threshold: number // dB
  ratio: number
  attack: number // ms, 0 = fastest
  release: number // ms, 0 = fastest
  eqBypass: boolean
  compBypass: boolean
  eqAuto: boolean // Auto Gain: makeup so the plugin does not change the loudness
  compAuto: boolean
}

// dB each plugin's Auto Gain adds, measured by autoGain.ts
export type Makeup = { eq: number; comp: number }
export const NO_MAKEUP: Makeup = { eq: 0, comp: 0 }

const band = (on: boolean, freq: number, slope: Slope = 12): EqBand => ({ on, freq, gain: 0, q: 1, slope })

// Shelves and bells start on at 0 dB, so a Gain knob is heard at once; HP and LP start off.
export const DEFAULTS: Settings = {
  volume: 0,
  bands: [band(false, 80, 24), band(true, 120), band(true, 300), band(true, 1000), band(true, 4000), band(true, 8000), band(false, 18000)],
  threshold: 0,
  ratio: 4,
  attack: 10,
  release: 100,
  eqBypass: false,
  compBypass: false,
  eqAuto: true,
  compAuto: true,
}

export const defaults = (): Settings => structuredClone(DEFAULTS)

// The EQ's biquads, in series: 4 stages for HP and LP, 1 for each other band.
// Shared by the live chain and the offline Auto Gain render, so both hear the same EQ.
export class EqFilters {
  readonly filters: BiquadFilterNode[][]
  private shapes = new Map<BiquadFilterNode, string>() // HP and LP stages: type and Q

  constructor(private ctx: BaseAudioContext) {
    this.filters = BANDS.map((b) =>
      Array.from({ length: b.kind === 'hp' || b.kind === 'lp' ? STAGES : 1 }, () => {
        const f = ctx.createBiquadFilter()
        f.type = b.kind === 'ls' ? 'lowshelf' : b.kind === 'hs' ? 'highshelf' : 'peaking'
        f.gain.value = 0
        return f
      }),
    )
  }

  // the stages that change the signal; the others pass it unchanged
  active(bands: EqBand[]) {
    return this.filters.flatMap((stages, i) => {
      const v = bands[i]
      const kind = BANDS[i].kind
      if (!v.on) return []
      if (kind === 'hp' || kind === 'lp') return stages.slice(0, v.slope / 12)
      return v.gain === 0 ? [] : stages
    })
  }

  // `smooth` ramps the change over a few ms so live edits do not click.
  apply(bands: EqBand[], smooth: boolean) {
    const set = (p: AudioParam, v: number) => {
      if (smooth) p.setTargetAtTime(v, this.ctx.currentTime, 0.005)
      else {
        p.cancelScheduledValues(0)
        p.value = v
      }
    }
    // A type change resets the stage at once; ramping across types would click anyway.
    const retype = (f: BiquadFilterNode, type: BiquadFilterType, q: number) => {
      const shape = `${type} ${q}`
      if (this.shapes.get(f) === shape) return false
      this.shapes.set(f, shape)
      f.type = type
      for (const p of [f.Q, f.gain]) p.cancelScheduledValues(0)
      f.Q.value = q
      f.gain.value = 0
      return true
    }
    BANDS.forEach((b, i) => {
      const v = bands[i]
      const stages = this.filters[i]
      if (b.kind === 'hp' || b.kind === 'lp') {
        // Off and unused stages are 0 dB peaking filters, which pass the signal
        // unchanged. A highpass parked at 1 Hz lifted 1 kHz by about 0.025 dB in Chrome.
        const qs = v.on ? butterworthQs(v.slope / 6) : []
        stages.forEach((f, k) => {
          if (k < qs.length) {
            // Web Audio reads highpass and lowpass Q in dB.
            const fresh = retype(f, b.kind === 'hp' ? 'highpass' : 'lowpass', 20 * Math.log10(qs[k]))
            if (fresh) f.frequency.value = v.freq
            else set(f.frequency, v.freq)
          } else retype(f, 'peaking', 1)
        })
        return
      }
      // shelves ignore Q in Web Audio; off is 0 dB, which passes the signal unchanged
      const f = stages[0]
      set(f.frequency, v.freq)
      set(f.gain, v.on ? v.gain : 0)
      if (b.kind === 'bell') set(f.Q, v.q)
    })
  }
}

// The compressor display's ring of points: input peak, output peak (linear), GR (dB).
export class CompHistory {
  readonly inp: Float32Array
  readonly out: Float32Array
  readonly gr: Float32Array
  pos = 0 // the next point to write; the oldest point
  at = 0 // performance.now() of the last batch, so the display can scroll between batches
  constructor(
    readonly size: number,
    readonly rate: number, // points per second
  ) {
    this.inp = new Float32Array(size)
    this.out = new Float32Array(size)
    this.gr = new Float32Array(size)
  }
  push(batch: Float32Array) {
    for (let k = 0; k < batch.length; k += 3) {
      this.inp[this.pos] = batch[k]
      this.out[this.pos] = batch[k + 1]
      this.gr[this.pos] = batch[k + 2]
      this.pos = (this.pos + 1) % this.size
    }
    this.at = performance.now()
  }
  // the latest GR, held over the last `points`
  recentGr(points: number) {
    let m = 0
    for (let k = 1; k <= points; k++) m = Math.max(m, this.gr[(this.pos - k + this.size) % this.size])
    return m
  }
}
