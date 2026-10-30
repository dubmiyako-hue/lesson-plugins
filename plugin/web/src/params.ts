import { RATIO_INF } from './dsp'

export type NumKey = 'volume' | 'threshold' | 'ratio' | 'attack' | 'release'
export type BandKey = 'freq' | 'gain' | 'q'

export type Param = {
  label: string
  min: number
  max: number
  log?: boolean
  zero?: string // the lowest position means 0, shown as this text
  inf?: { value: number; text: string } // the highest position means this value, shown as this text
  step?: number
  fmt: (v: number) => string
  tip: string
}

// value formats as PRAYROOM's behavior.md writes them: '-18.0 dB', '47.5 Hz', '2.50 kHz', '4.20 ms'
export const fmtDb = (v: number) => {
  const t = Math.abs(v).toFixed(1)
  return `${t === '0.0' ? '' : v > 0 ? '+' : '-'}${t} dB`
}
// three significant digits
const sig3 = (v: number) => (v >= 99.95 ? String(Math.round(v)) : v >= 9.995 ? v.toFixed(1) : v.toFixed(2))
export const fmtHz = (v: number) => (v >= 999.5 ? `${sig3(v / 1000)} kHz` : `${sig3(v)} Hz`)
const fmtMs = (v: number) => (v >= 999.5 ? `${sig3(v / 1000)} s` : `${sig3(v)} ms`)

export const PARAMS: Record<NumKey, Param> = {
  volume: { label: 'Volume', min: -24, max: 12, step: 0.5, fmt: fmtDb, tip: 'トラックの音量。ダブルクリックで 0 dB に戻る。Shift を押しながらで細かく動く。' },
  threshold: { label: 'Threshold', min: -60, max: 0, step: 0.5, fmt: fmtDb, tip: 'ここより大きい音だけが圧縮（音量を抑えること）される。下げるほど多くの音にかかる。音量の流れの点線を上下にドラッグしても動かせる。' },
  ratio: { label: 'Ratio', min: 1, max: 20, log: true, inf: { value: RATIO_INF, text: '∞:1' }, fmt: (v) => `${v.toFixed(1)}:1`, tip: 'Threshold を超えた分をどれだけ縮めるか。4:1 なら、8 dB 超えた音が 2 dB 超えまで縮む（dB は音量の単位）。右端の ∞:1 は、Threshold より上に出ないよう抑えるリミッターになる。' },
  attack: { label: 'Attack', min: 0.1, max: 100, log: true, zero: '最速', fmt: fmtMs, tip: '音が Threshold を超えてから、圧縮が効ききるまでの時間。遅いほど、太鼓を叩いた瞬間や歌の子音の出だしが、抑えられずにそのまま残る。' },
  release: { label: 'Release', min: 5, max: 1000, log: true, zero: '最速', fmt: fmtMs, tip: '音が Threshold を下回ってから、圧縮がゆるんで元に戻るまでの時間。速いほど音量が小刻みに揺れやすく、遅いほど次の音まで下がったままになりやすい。' },
}

export const BAND_PARAMS: Record<BandKey, Param> = {
  freq: { label: 'Freq', min: 20, max: 20000, log: true, fmt: fmtHz, tip: 'このバンドが効く周波数（音の高さ）。グラフの点を左右にドラッグしても動かせる。' },
  gain: { label: 'Gain', min: -18, max: 18, step: 0.5, fmt: fmtDb, tip: 'Freq の周波数の音を、どれだけ上げ下げするか。どの周波数かを探すときは、大きく上げてから Freq を動かすと、変化が聞き取りやすい。' },
  q: { label: 'Q', min: 0.3, max: 12, log: true, fmt: (v) => v.toFixed(2), tip: '効く周波数の幅。大きいほど狭い範囲に、小さいほど広い範囲にかかる（グラフの山や谷の幅）。グラフの点の上でホイールを回しても変わる。' },
}

export const clampTo = (p: Param, v: number) => Math.max(p.min, Math.min(p.max, v))

// 0..1 along the control; `zero` and `inf` each keep a notch at their end
export function toNorm(p: Param, v: number) {
  const lo = p.zero ? 0.04 : 0
  const hi = p.inf ? 0.96 : 1
  if (p.zero && v <= 0) return 0
  if (p.inf && v >= p.inf.value) return 1
  const t = p.log ? Math.log(v / p.min) / Math.log(p.max / p.min) : (v - p.min) / (p.max - p.min)
  return lo + Math.max(0, Math.min(1, t)) * (hi - lo)
}

export function fromNorm(p: Param, n: number) {
  const lo = p.zero ? 0.04 : 0
  const hi = p.inf ? 0.96 : 1
  if (p.zero && n < lo / 2) return 0
  if (p.inf && n > (hi + 1) / 2) return p.inf.value
  const t = Math.max(0, Math.min(1, (n - lo) / (hi - lo)))
  let v = p.log ? p.min * (p.max / p.min) ** t : p.min + t * (p.max - p.min)
  if (p.step) v = Math.round(v / p.step) * p.step
  return v
}

export const showValue = (p: Param, v: number) => (p.zero && v <= 0 ? p.zero : p.inf && v >= p.inf.value ? p.inf.text : p.fmt(v))
