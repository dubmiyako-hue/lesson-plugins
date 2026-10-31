// The web app's src/app.ts, as the copied screen (src/ui) uses it, on top of the plug-in: one track (the plug-in itself),
// its settings read from and written to the host's parameters, the spectra and the compressor's points from the plug-in
// instead of AnalyserNodes and the worklet. Only what the screen calls is here.
import { BANDS, CompHistory, DEFAULTS, EqFilters, SLOPES, defaults, type EqBand, type Makeup, type Settings } from './chain'
import { COMP_POINT } from './dsp'
import { BAND_PARAMS, PARAMS, showValue, type BandKey, type NumKey, type Param } from './params'
import { closeAll, connectGestures, held, phaseOf } from './gestures'
import { floats, host, type Plugin, type Ready, type Spectrum } from './host'

export type { Plugin }

// Counts what the plug-in has sent (spectrum, points; the tick only when it changes something, and that is an emit); the frames check it to know the display may have changed.
export const data = { stamp: 0 }
export const TICK_HZ = 30 // how often the plug-in sends its measurements (plugin/common/Bridge.h)

export type State = {
  plugin: Plugin
  settings: Settings[]
  makeup: Makeup[] // the Auto Gain the plug-in computed for the settings
  measuring: boolean[] // the web app measures the Auto Gain by rendering; the plug-in computes it at once
  selected: number
  band: number // the EQ band shown in the knob row
  pop: Plugin | null
}

export const state: State = {
  plugin: 'eq',
  settings: [defaults()],
  makeup: [{ eq: 0, comp: 0 }],
  measuring: [false],
  selected: 0,
  band: 2,
  pop: null,
}

const listeners = new Set<() => void>()
// A component subscribes in an effect, after it has drawn; the plug-in's first tick can come in between, so the new
// listener is called once at once to catch up.
export function subscribe(fn: () => void) {
  listeners.add(fn)
  fn()
  return () => listeners.delete(fn)
}
export function emit() {
  for (const fn of listeners) fn()
}

// ---- the parameters ----

const BAND_IDS = ['hp', 'ls', 'b1', 'b2', 'b3', 'hs', 'lp']
const NUM_IDS: Partial<Record<NumKey, string>> = { threshold: 'comp_threshold', ratio: 'comp_ratio', attack: 'comp_attack', release: 'comp_release' }
const AUTO_ID: Record<Plugin, string> = { eq: 'eq_auto_gain', comp: 'comp_auto_gain' }

const values: Record<string, number> = {} // what the screen shows, by parameter id
const fromHost: Record<string, number> = {} // the host's latest value, kept while a gesture holds the parameter

// the settings the screen reads, from the parameters
function read() {
  const s = state.settings[0]
  const v = (id: string, d: number) => values[id] ?? d
  if (state.plugin === 'eq') {
    s.bands = BANDS.map((_, k): EqBand => {
      const p = BAND_IDS[k]
      const d = DEFAULTS.bands[k]
      return {
        on: v(`${p}_on`, d.on ? 1 : 0) >= 0.5,
        freq: v(`${p}_freq`, d.freq),
        gain: v(`${p}_gain`, d.gain),
        q: v(`${p}_q`, d.q),
        slope: SLOPES[Math.round(v(`${p}_slope`, SLOPES.indexOf(d.slope)))] ?? d.slope,
      }
    })
    s.eqBypass = v('bypass', 0) >= 0.5
    s.eqAuto = v(AUTO_ID.eq, 1) >= 0.5
    curve.apply(s.bands)
  } else {
    for (const [k, id] of Object.entries(NUM_IDS) as [NumKey, string][]) s[k] = v(id, DEFAULTS[k])
    s.compBypass = v('bypass', 0) >= 0.5
    s.compAuto = v(AUTO_ID.comp, 1) >= 0.5
  }
}

// the parameter a knob's Param stands for: a band's for the band shown, the compressor's by its key
export function idOf(param: Param): string | undefined {
  for (const [key, p] of Object.entries(BAND_PARAMS)) if (p === param) return `${BAND_IDS[state.band]}_${key}`
  for (const [key, p] of Object.entries(PARAMS)) if (p === param) return NUM_IDS[key as NumKey]
  return undefined
}

// told of each change the screen makes (the window's footer shows it)
let changed: (id: string, value: number) => void = () => {}
export const onChange = (fn: typeof changed) => (changed = fn)

function send(id: string, value: number) {
  if (values[id] === value) return
  const phase = phaseOf(id) // before the new value: a gesture it ends ends at the value it left
  values[id] = value
  host().setParam(id, value, phase)
  changed(id, value)
}

// the window's power switch
export function setBypass(bypassed: boolean) {
  send('bypass', bypassed ? 1 : 0)
  read()
  emit()
}

// "Bell 1 Freq  1.00 kHz": what the footer shows for a change, in the host's words and the screen's value format
export function readout(names: Record<string, string>, id: string, value: number) {
  const name = names[id] ?? id
  if (id.endsWith('_on') || id === 'bypass' || id.endsWith('auto_gain')) return `${name}  ${value >= 0.5 ? 'On' : 'Off'}`
  if (id.endsWith('_slope')) return `${name}  ${SLOPES[Math.round(value)]} dB/oct`
  const key = id.slice(id.indexOf('_') + 1)
  const param = id.startsWith('comp_') ? PARAMS[key as NumKey] : BAND_PARAMS[key as BandKey]
  return param ? `${name}  ${showValue(param, value)}` : name
}

export function setParam(_track: number, key: NumKey, value: number) {
  const id = NUM_IDS[key]
  if (!id) return
  send(id, value)
  read()
  emit()
}

export function resetParam(track: number, key: NumKey) {
  setParam(track, key, DEFAULTS[key])
}

export function setBand(_track: number, band: number, patch: Partial<EqBand>) {
  const p = BAND_IDS[band]
  for (const [key, x] of Object.entries(patch) as [keyof EqBand, EqBand[keyof EqBand]][]) {
    if (key === 'on') send(`${p}_on`, x ? 1 : 0)
    else if (key === 'slope') send(`${p}_slope`, SLOPES.indexOf(x as EqBand['slope']))
    else send(`${p}_${key}`, x as number)
  }
  read()
  emit()
}

export function resetBand(track: number, band: number, key: BandKey) {
  setBand(track, band, { [key]: DEFAULTS.bands[band][key] })
}

export function selectBand(band: number) {
  state.band = band
  emit()
}

export function togglePlugin(_plugin: Plugin) {
  send('bypass', values.bypass >= 0.5 ? 0 : 1)
  read()
  emit()
}

export function toggleAuto(plugin: Plugin) {
  send(AUTO_ID[plugin], values[AUTO_ID[plugin]] >= 0.5 ? 0 : 1)
  read()
  emit()
}

// no popup in the plug-in: its window is the large view
export function setPop(_plugin: Plugin | null) {}

// ---- what the plug-in measures: the spectra, the compressor's points, whether audio is coming ----

const HISTORY_S = 4 // seconds of the compressor display (src/chain.ts)

// What the EQ display reads from an AnalyserNode (src/ui/Eq.tsx spectrum()), filled from the plug-in's spectrum.
class Analyser {
  readonly fftSize = 8192
  readonly frequencyBinCount = 4096
  readonly context = { sampleRate: 48000 }
  readonly data = new Float32Array(4096).fill(-300)
  getFloatFrequencyData(out: Float32Array) {
    out.set(this.data.subarray(0, out.length))
  }
  set(dbs: Float32Array) {
    this.data.set(dbs.subarray(0, this.data.length))
    this.data.fill(-300, Math.min(dbs.length, this.data.length))
  }
}

// The EQ's curve: the web app's own biquads (src/chain.ts EqFilters) on a context that never renders, set straight to
// the settings, so getFrequencyResponse gives the response that is heard.
class Curve {
  private ctx: OfflineAudioContext | null = null
  filters: EqFilters | null = null
  private bands: EqBand[] | null = null
  setRate(fs: number) {
    if (this.ctx?.sampleRate === fs) return
    this.ctx = new OfflineAudioContext(1, 128, fs)
    this.filters = new EqFilters(this.ctx)
    if (this.bands) this.filters.apply(this.bands, false)
  }
  apply(bands: EqBand[]) {
    this.bands = bands.map((b) => ({ ...b }))
    this.filters?.apply(this.bands, false)
  }
}

const pre = new Analyser()
const post = new Analyser()
const curve = new Curve()

export const player = {
  playing: false,
  sampleRate: 48000,
  tracks: [
    {
      chain: {
        pre: pre as unknown as AnalyserNode,
        post: post as unknown as AnalyserNode,
        get eq() {
          return curve.filters!
        },
        history: null as CompHistory | null,
      },
    },
  ],
}

function setRate(fs: number) {
  if (fs === player.sampleRate && (state.plugin !== 'comp' || player.tracks[0].chain.history)) return
  player.sampleRate = fs
  pre.context.sampleRate = post.context.sampleRate = fs
  curve.setRate(fs)
  const rate = fs / COMP_POINT
  player.tracks[0].chain.history = state.plugin === 'comp' ? new CompHistory(Math.round(HISTORY_S * rate), rate) : null
}

function spectrum(s: Spectrum) {
  pre.set(floats(s.pre))
  post.set(floats(s.post))
}

// ---- start ----

export async function start(): Promise<Ready> {
  const h = host()
  const ready = await h.ready()
  state.plugin = ready.plugin
  Object.assign(values, ready.values)
  curve.setRate(ready.sampleRate)
  player.sampleRate = 0
  setRate(ready.sampleRate)
  state.makeup[0] = { ...state.makeup[0], [ready.plugin]: ready.makeup }
  read()
  connectGestures(
    (id) => h.setParam(id, values[id], 'end'),
    (ids) => {
      // the host's value for what the gesture held, now that it is let go
      for (const id of ids)
        if (id in fromHost) {
          values[id] = fromHost[id]
          delete fromHost[id]
        }
      read()
      emit()
    },
  )
  h.on('params', (changed) => {
    for (const [id, v] of Object.entries(changed)) {
      if (held(id)) fromHost[id] = v
      else values[id] = v
    }
    read()
    emit()
  })
  h.onTick((t) => {
    setRate(t.sampleRate)
    const was = state.makeup[0][state.plugin]
    state.makeup[0] = { ...state.makeup[0], [state.plugin]: t.makeup }
    const changed = was !== t.makeup || player.playing !== t.playing
    player.playing = t.playing
    if (changed) emit()
  })
  h.onSpectrum((s) => {
    data.stamp++
    spectrum(s)
  })
  h.onPoints((b64) => {
    data.stamp++
    player.tracks[0].chain.history?.push(floats(b64))
  })
  addEventListener('pagehide', closeAll)
  return ready
}
