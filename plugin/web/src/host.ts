// The plug-in's side of the page (plugin/common/Bridge.cpp), on top of the window.__JUCE__ that JUCE puts in the page.
// juce/index.js is not used; the few steps it would give are here (as in subbass-vst's sb-bridge.js):
//   a call:  backend.emitEvent('__juce__invoke', { name, params, resultId }); the answer is the event '__juce__complete'
//            with { promiseId: resultId, result }
//   events:  backend.addEventListener(name, fn)
// Outside the plug-in (a browser on its own) dev/fake-juce.ts puts a fake __JUCE__ in the page before this runs.

export type Plugin = 'eq' | 'comp'
export type Theme = 'light' | 'dark' | 'studio'
// a host gesture: begin and end bracket a drag; complete is begin, the value and end at once
export type Phase = 'begin' | 'move' | 'end' | 'complete'
export type ScaleChoice = { value: number; enabled: boolean }
export type Scales = { scale: number; scales: ScaleChoice[] }

export type Ready = Scales & {
  plugin: Plugin
  version: string
  sampleRate: number
  // every parameter's plain value as the plug-in holds it: Hz, dB, ms, the choice's index, 0 or 1
  values: Record<string, number>
  names: Record<string, string> // the host's names, for the footer
  makeup: number // dB, the Auto Gain for the settings (also while it is off)
  theme: Theme
  hints: boolean
}

// 30 times a second, while the window is open
export type Tick = { makeup: number; playing: boolean; sampleRate: number }
// base64 float32 dB per FFT bin, from 0 Hz up to just above 20 kHz; `bin` is the Hz per bin
export type Spectrum = { pre: string; post: string; bin: number; fftSize: number }

type Backend = {
  emitEvent(name: string, payload: unknown): void
  addEventListener(name: string, fn: (payload: never) => void): unknown
}
declare global {
  interface Window {
    __JUCE__?: { backend: Backend }
  }
}

function connect() {
  const backend = window.__JUCE__?.backend
  if (!backend) throw new Error('Lesson: window.__JUCE__ is missing (the page runs only inside the plug-in or dev/)')
  const pending = new Map<number, (result: unknown) => void>()
  let lastId = 0
  backend.addEventListener('__juce__complete', (payload: { promiseId: number; result: unknown }) => {
    const resolve = pending.get(payload.promiseId)
    if (!resolve) return
    pending.delete(payload.promiseId)
    resolve(payload.result)
  })
  const invoke = <T>(name: string, ...params: unknown[]) =>
    new Promise<T>((resolve) => {
      const resultId = lastId++
      pending.set(resultId, resolve as (result: unknown) => void)
      backend.emitEvent('__juce__invoke', { name, params, resultId })
    })
  // a call whose answer nobody reads
  const send = (name: string, ...params: unknown[]) => void invoke(name, ...params)
  return {
    ready: () => invoke<Ready>('ready'),
    setParam: (id: string, value: number, phase: Phase) => send('setParam', id, value, phase),
    // the typed text as the plug-in reads it (the core's parseValue, as the host's reading does), or null when it cannot be read
    parseValue: (id: string, text: string) => invoke<number | null>('parseValue', id, text),
    setScale: (scale: number) => invoke<Scales>('setScale', scale),
    scales: () => invoke<Scales>('scales'),
    setTheme: (theme: Theme) => send('setTheme', theme),
    setHints: (on: boolean) => send('setHints', on),
    on: (event: 'params', fn: (values: Record<string, number>) => void) => void backend.addEventListener(event, fn),
    onTick: (fn: (t: Tick) => void) => void backend.addEventListener('tick', fn),
    onSpectrum: (fn: (s: Spectrum) => void) => void backend.addEventListener('spectrum', fn),
    // base64 float32 points, three values each: input peak, output peak before the makeup (linear), GR (dB)
    onPoints: (fn: (points: string) => void) => void backend.addEventListener('points', fn),
  }
}

export type Host = ReturnType<typeof connect>
let one: Host | null = null
export const host = () => (one ??= connect())

export function floats(base64: string) {
  const bytes = Uint8Array.from(atob(base64), (c) => c.charCodeAt(0))
  return new Float32Array(bytes.buffer, 0, bytes.length >> 2)
}
