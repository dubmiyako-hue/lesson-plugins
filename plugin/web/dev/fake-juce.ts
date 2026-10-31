// A fake plug-in for the page in a browser on its own (npm run web:dev, then dev/index.html). It answers the page's
// calls as plugin/common/Bridge.cpp does, records them in window.__calls, and sends made-up measurements, so the screen
// can be looked at and driven without a host. The address picks the state:
//   ?plugin=comp          the compressor (default: the EQ)
//   &theme=dark&hints=0   the settings
//   &playing=0            no audio coming (no spectra, an empty level history)
//   &set=hp_on:1,b1_gain:6   parameter values
//   &rate=96000           the sample rate
import type { Phase, Ready, Scales } from '../src/host'

const q = new URLSearchParams(location.search)
const plugin = q.get('plugin') === 'comp' ? 'comp' : 'eq'
const rate = Number(q.get('rate') ?? 48000)
const playing = q.get('playing') !== '0'

const EQ: Record<string, [string, number]> = {
  bypass: ['Bypass', 0], hp_on: ['HP On', 0], hp_freq: ['HP Freq', 80], hp_slope: ['HP Slope', 1],
  ls_on: ['LS On', 1], ls_freq: ['LS Freq', 120], ls_gain: ['LS Gain', 0],
  b1_on: ['Bell 1 On', 1], b1_freq: ['Bell 1 Freq', 300], b1_gain: ['Bell 1 Gain', 0], b1_q: ['Bell 1 Q', 1],
  b2_on: ['Bell 2 On', 1], b2_freq: ['Bell 2 Freq', 1000], b2_gain: ['Bell 2 Gain', 0], b2_q: ['Bell 2 Q', 1],
  b3_on: ['Bell 3 On', 1], b3_freq: ['Bell 3 Freq', 4000], b3_gain: ['Bell 3 Gain', 0], b3_q: ['Bell 3 Q', 1],
  hs_on: ['HS On', 1], hs_freq: ['HS Freq', 8000], hs_gain: ['HS Gain', 0],
  lp_on: ['LP On', 0], lp_freq: ['LP Freq', 18000], lp_slope: ['LP Slope', 0], eq_auto_gain: ['Auto Gain', 1],
}
const COMP: Record<string, [string, number]> = {
  bypass: ['Bypass', 0], comp_threshold: ['Threshold', 0], comp_ratio: ['Ratio', 4], comp_attack: ['Attack', 10],
  comp_release: ['Release', 100], comp_auto_gain: ['Auto Gain', 1],
}
const table = plugin === 'eq' ? EQ : COMP
const values: Record<string, number> = Object.fromEntries(Object.entries(table).map(([id, [, d]]) => [id, d]))
for (const pair of (q.get('set') ?? '').split(',').filter(Boolean)) {
  const [id, v] = pair.split(':')
  if (id in values) values[id] = Number(v)
}
const changed = new Set<string>()
let settings = { scale: Number(q.get('scale') ?? 1), theme: (q.get('theme') ?? 'light') as Ready['theme'], hints: q.get('hints') !== '0' }
const scales = (): Scales => ({ scale: settings.scale, scales: [1, 1.25, 1.5, 2].map((value) => ({ value, enabled: value < 2 })) })

type Listener = (payload: never) => void
const listeners = new Map<string, Listener[]>()
const dispatch = (name: string, payload: unknown) => listeners.get(name)?.forEach((fn) => fn(payload as never))
const calls: unknown[][] = []
;(window as unknown as { __calls: unknown[][] }).__calls = calls

function answer(name: string, params: unknown[]): unknown {
  switch (name) {
    case 'ready':
      return {
        plugin, version: 'dev', sampleRate: rate, values: { ...values },
        names: Object.fromEntries(Object.entries(table).map(([id, [n]]) => [id, n])),
        makeup: 0, ...scales(), theme: settings.theme, hints: settings.hints,
      } satisfies Ready
    case 'setParam': {
      const [id, value] = params as [string, number, Phase]
      values[id] = value
      changed.add(id)
      return undefined
    }
    case 'parseValue': {
      const t = String(params[1]).trim().replace(/\s*(dB|Hz|ms|:1)$/i, '')
      const m = t.match(/^([-+]?\d*\.?\d+)(k?)$/i)
      return m ? Number(m[1]) * (m[2] ? 1000 : 1) : null
    }
    case 'scales':
      return scales()
    case 'setScale':
      settings = { ...settings, scale: Number(params[0]) }
      return scales()
    case 'setTheme':
      settings = { ...settings, theme: params[0] as Ready['theme'] }
      return undefined
    case 'setHints':
      settings = { ...settings, hints: !!params[0] }
      return undefined
  }
  return undefined
}

window.__JUCE__ = {
  backend: {
    emitEvent(name: string, payload: unknown) {
      if (name !== '__juce__invoke') return
      const { name: fn, params, resultId } = payload as { name: string; params: unknown[]; resultId: number }
      calls.push([fn, ...params])
      const result = answer(fn, params)
      setTimeout(() => dispatch('__juce__complete', { promiseId: resultId, result }), 0)
    },
    addEventListener(name: string, fn: Listener) {
      listeners.set(name, [...(listeners.get(name) ?? []), fn])
    },
  },
}

// ---- made-up measurements, 30 times a second ----
const b64 = (a: Float32Array) => btoa(String.fromCharCode(...new Uint8Array(a.buffer)))
const bin = rate / 8192
const bins = Math.ceil((20000 * 2 ** (1 / 24)) / bin) + 2
let t = 0
setInterval(() => {
  t += 1 / 30
  if (changed.size) {
    dispatch('params', Object.fromEntries([...changed].map((id) => [id, values[id]])))
    changed.clear()
  }
  dispatch('tick', { makeup: plugin === 'eq' ? 0 : Math.max(0, -values.comp_threshold * (1 - 1 / values.comp_ratio) / 2), playing, sampleRate: rate })
  if (!playing) return
  if (plugin === 'eq') {
    const pre = new Float32Array(bins)
    const post = new Float32Array(bins)
    for (let k = 0; k < bins; k++) {
      const f = Math.max(bin, k * bin)
      const db = -52 - 4.5 * Math.log2(f / 1000) - (f < 40 ? 20 : 0) - (f > 16000 ? 30 : 0) + 3 * Math.sin(k * 0.37 + t * 9)
      pre[k] = db
      post[k] = db - 2
    }
    dispatch('spectrum', { pre: b64(pre), post: b64(post), bin, fftSize: 8192 })
  } else {
    const n = Math.round(rate / 256 / 30)
    const pts = new Float32Array(3 * n)
    const th = values.comp_threshold
    const r = values.comp_ratio >= 1000 ? Infinity : values.comp_ratio
    for (let k = 0; k < n; k++) {
      const s = t + k / (rate / 256)
      const levelDb = -14 + 10 * Math.sin(s * 2.1) + 6 * Math.sin(s * 7.3)
      const gr = levelDb > th ? (levelDb - th) * (1 - 1 / r) : 0
      pts.set([10 ** (levelDb / 20), 10 ** ((levelDb - gr) / 20), gr], 3 * k)
    }
    dispatch('points', b64(pts))
  }
}, 1000 / 30)

// ?band=N: the EQ band N chosen on the band selector once the page has drawn; ?click=selector: that element clicked;
// ?hover=selector: the pointer come onto that element (its Hint shown)
const band = q.get('band')
if (band !== null)
  setTimeout(() => document.querySelectorAll<HTMLButtonElement>('[role=radiogroup] > button')[Number(band)]?.click(), 300)
const click = q.get('click')
if (click !== null) setTimeout(() => document.querySelector<HTMLElement>(click)?.click(), 300)
const hover = q.get('hover')
if (hover !== null)
  setTimeout(() => {
    // at the element's middle, which picks the Hint's side
    const el = document.querySelector(hover)
    const r = el?.getBoundingClientRect()
    if (r) el!.dispatchEvent(new PointerEvent('pointerover', { bubbles: true, clientX: r.left + r.width / 2, clientY: r.top + r.height / 2 }))
  }, 400)
