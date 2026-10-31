import { useMemo, useRef } from 'preact/hooks'
import { player, resetBand, selectBand, setBand } from '../app'
import { BANDS, DEFAULTS, SLOPES, type BandKind } from '../chain'
import { BAND_PARAMS, clampTo, fmtDb, fmtHz, type BandKey } from '../params'
import { Glyph, KIND_NAMES } from './Glyph'
import { Knob, Segmented, Toggle } from './Knob'
import { graphGestures } from '../gestures'
import { hideHint, hint, pluginHints, showHint, type HintText } from './Hint'
import { AutoGain } from './PluginHeader'
import { css, fitCanvas, useApp, useFrame } from './hooks'

// what each kind of band does, in the Hint of its key, its point and its switch
const BAND_TIPS: Record<BandKind, string> = {
  hp: 'Freq より低い音を削り、高い音だけ通す。「ゴロつき」を消すときや、低い音が要らないトラックの低い音を削るときに使う。',
  ls: 'Freq より低い音をまとめて上げ下げする。低音全体の量を変えたいときに使える。',
  bell: 'Freq を中心に、山（上げる）か谷（下げる）を作る。山や谷の幅は Q で決める。',
  hs: 'Freq より高い音をまとめて上げ下げする。高音全体の量を変えたいときに使える。',
  lp: 'Freq より高い音を削り、低い音だけ通す。「シャー」のような高い音をまとめて減らしたいときに使う。',
}

// what each word of the two label rows means, in its Hint
const LABEL_TIPS: Record<string, string> = {
  重さ: 'キックやベースのいちばん低い音。ノートパソコンやスマホのスピーカーでは、ほとんど聞こえない。',
  厚み: 'ベースの音の多くと、男性の声やギター・ピアノの低い音がある。ここを下げると、曲の低い音全体の量が減る。',
  胴鳴り: 'ギターやピアノのボディ（木の箱）が共鳴して出る音がある。',
  存在感: 'ここを上げると、歌の言葉やスネアの音が、ほかの楽器より聞き取りやすくなる。',
  輪郭: '叩いた瞬間（アタック）や弦をはじいた瞬間の音、歌の「か」「た」などの子音がある。ここを上げると、音の出だしがはっきり聞こえる。',
  空気感: '息の音や、シンバルの高い響き、残響のいちばん高い成分がある。',
  ゴロつき: '出すぎると、マイクに入った振動や空調の音のような、音程の分からない低い音が鳴り続け、キックやベースが聞き取りにくくなる。',
  ブーミー: '出すぎると、低い音が鳴り終わったあとも長く響いて残り、キックやベースの一音ずつの区切りが聞き取りにくくなる。',
  こもり: '出すぎると、高い音が相対的に小さく聞こえて、歌の言葉が聞き取りにくくなる。楽器どうしの音も混ざって、1 つずつ聞き分けにくくなる。',
  鼻にかかる: '出すぎると、歌が、鼻をつまんで話したときのような鼻声に聞こえる。',
  耳に痛い: '出すぎると、歌の高い声やひずんだギターの音が大きく聞こえ、長く聴くと耳が疲れる。人の耳がとくに敏感な高さで、少し出すぎても目立つ。',
  シャー: '出すぎると、サ行の「スー」「シー」という息の音（歯擦音）や、シンバル・ハイハットの音が、ほかの音より大きく聞こえる。',
}

const SLOPE_TIP = 'Freq の先で、どれだけ急に削るか。大きいほど Freq のすぐ先で一気に小さくなり、小さいほどゆるやかに減っていく。'
const GRAPH_HINT: HintText = { title: 'EQ のグラフ', text: '横が周波数（音の高さ、左が低く右が高い）、縦が上げ下げの量。線がこの EQ のかかり方。再生中は、かける前（灰色）とかけた後（色の塗り）の音の大きさも、高さごとに出る。' }
// no range: the graph shades it (the user: 「範囲は要らない」)
const wordHint = ([, word]: Word): HintText => ({ title: word, text: LABEL_TIPS[word] })
const bandName = (k: number) => `${KIND_NAMES[BANDS[k].kind]} ${BANDS[k].name}`

// The bottom row's ranges, where the sources put each fault (DECISIONS.md, 2026-10-30):
// 鼻にかかる is iZotope's nasal vocal (500 Hz-1.2 kHz), シャー the vocal sibilance of Sound On
// Sound (7.5-10 kHz) and Sound & Recording (8-10 kHz); the others sit within the sources' ranges.
export type Word = [at: number, word: string, lo: number, hi: number]
export const TOP_LABELS: Word[] = [[40, '重さ', 20, 60], [130, '厚み', 100, 200], [400, '胴鳴り', 300, 600], [2300, '存在感', 2000, 3000], [5000, '輪郭', 4000, 6000], [12000, '空気感', 10000, 20000]]
export const BOTTOM_LABELS: Word[] = [[35, 'ゴロつき', 20, 50], [110, 'ブーミー', 80, 200], [280, 'こもり', 200, 500], [1000, '鼻にかかる', 500, 1200], [3000, '耳に痛い', 2000, 5000], [9000, 'シャー', 7000, 10000]]

const F_LO = 20
const F_HI = 20000
const fx = (f: number, w: number) => (Math.log(f / F_LO) / Math.log(F_HI / F_LO)) * w
const xf = (x: number, w: number) => F_LO * (F_HI / F_LO) ** (x / w)
const RANGE = 18 // ±dB of the curve
const SCALE_W = 28 // the dB numbers at the graph's left edge
const ROW = 18 // height of each label row
// spectrum: dB shown from bottom to top, after a tilt of 4.5 dB/oct around 1 kHz
// (as Pro-Q shows it), so a typical mix reads roughly level
const SPEC_LO = -130
const SPEC_HI = -40
const TILT = 4.5
const GRID_F: [number, string][] = [[50, '50'], [100, '100'], [200, '200'], [500, '500'], [1000, '1k'], [2000, '2k'], [5000, '5k'], [10000, '10k']]

// Each frequency has a colour, from pink at 20 Hz to green at 20 kHz. The spectrum, the
// label rows, the frequency numbers, the band points and the band LEDs all take the colour
// of where they sit, so a point reads as part of its region. (An exception to PRAYROOM's
// indigo-and-amber visualisation and red LEDs, asked for by the user on 2026-10-28.)
// The colours are OKLCH at one lightness and chroma per use, so only the hue tells them
// apart; the hue takes equal steps from one label word to the next (both rows together),
// rather than equal steps per octave, so neighbouring words never come out alike.
const WORD_X = [...TOP_LABELS, ...BOTTOM_LABELS].map(([f]) => Math.log(f / F_LO)).sort((a, b) => a - b)
const HUE_0 = 10 // pink, at the lowest word and below
const HUE_SPAN = 260 // to green, at the highest word and above
function hue(f: number) {
  const x = Math.log(f / F_LO)
  const n = WORD_X.length - 1
  let k = 0
  if (x >= WORD_X[n]) k = n
  else if (x > WORD_X[0]) {
    const i = WORD_X.findIndex((v) => v > x) - 1
    k = i + (x - WORD_X[i]) / (WORD_X[i + 1] - WORD_X[i])
  }
  return (HUE_0 - (HUE_SPAN * k) / n + 360) % 360
}
// Each chroma is the most that stays inside sRGB at that lightness for every hue (blue
// limits the tint, teal the ink), so no colour is clipped brighter or duller than the rest.
// text and outlines, dark enough to read on the tints and the light surfaces
export const freqInk = (f: number) => `oklch(0.5 0.085 ${hue(f)})`
// filled marks: band points and lit LEDs
// (L 0.53 keeps the paper-coloured number on it at 4.8:1 or more)
export const freqMark = (f: number) => `oklch(0.53 0.085 ${hue(f)})`
// an LED lit in a colour of its own, as PRAYROOM's tint() sets it: the lens made lighter
// at the same hue, the light it spills a translucent copy
const ledTint = (c: string) => ({ '--active': c, '--led-lit': `oklch(from ${c} calc(l + 0.16) c h)`, '--led-glow': `oklch(from ${c} l c h / 0.65)` })
// the label rows' bands, opaque
const freqTint = (f: number) => `oklch(0.9 0.045 ${hue(f)})`
// washes and fills over the graph, at the given opacity
const freqWash = (f: number, a: number) => `oklch(0.6 0.095 ${hue(f)} / ${a})`
const isCut = (k: number) => BANDS[k].kind === 'hp' || BANDS[k].kind === 'lp'
// the pixel columns of a word's band on its label row: the bands tile the row edge to
// edge, meeting midway (on the log scale) between neighbouring ranges
const tile = (row: Word[], k: number, w: number) => [
  k ? Math.round(fx(Math.sqrt(row[k - 1][3] * row[k][2]), w)) : 0,
  k < row.length - 1 ? Math.round(fx(Math.sqrt(row[k][3] * row[k + 1][2]), w)) : w,
]

// One dB value per pixel column: bins averaged by power over 1/12 octave (or the
// column, if wider), interpolated where that spans less than one bin.
const SMOOTH = 2 ** (1 / 24)
function spectrum(an: AnalyserNode, data: Float32Array<ArrayBuffer>, w: number, out: Float32Array) {
  an.getFloatFrequencyData(data)
  const bin = an.context.sampleRate / an.fftSize
  for (let x = 0; x < w; x++) {
    const f = xf(x, w)
    const a = Math.min(xf(x - 0.5, w), f / SMOOTH) / bin
    const b = Math.max(xf(x + 0.5, w), f * SMOOTH) / bin
    let db: number
    if (b - a < 1) {
      const pos = (a + b) / 2
      const k = Math.min(data.length - 2, Math.floor(pos))
      db = data[k] + (data[k + 1] - data[k]) * (pos - k)
    } else {
      let p = 0
      let n = 0
      for (let k = Math.ceil(a); k < b && k < data.length; k++, n++) p += 10 ** (data[k] / 10)
      db = 10 * Math.log10(p / Math.max(1, n) || 1e-20)
    }
    out[x] = db + TILT * Math.log2(f / 1000)
  }
}

function EqGraph() {
  const s = useApp()
  const c = useRef<HTMLCanvasElement>(null)
  const drag = useRef<number | null>(null)
  const gestures = useMemo(graphGestures, []) // the host's gestures of a drag and of the wheel, apart
  const word = useRef<{ row: Word[]; k: number } | null>(null) // the label-row word under the pointer
  const buf = useRef<{ data: Float32Array<ArrayBuffer>; pre: Float32Array; post: Float32Array } | null>(null)
  const i = s.selected
  const set = s.settings[i]

  const geo = () => {
    const h = c.current!.clientHeight
    const gh = h - 2 * ROW
    return { w: c.current!.clientWidth, gh, y: (db: number) => ROW + gh / 2 - (db / RANGE) * (gh / 2) }
  }
  const nodeY = (k: number, y: (db: number) => number) => y(isCut(k) ? 0 : set.bands[k].gain)

  useFrame(() => {
    const t = player.tracks[i]
    if (!c.current || !t || !set) return
    const { g, w, h } = fitCanvas(c.current)
    const gh = h - 2 * ROW
    const y = (db: number) => ROW + gh / 2 - (db / RANGE) * (gh / 2)
    const sy = (db: number) => ROW + gh - ((Math.max(SPEC_LO, Math.min(SPEC_HI, db)) - SPEC_LO) / (SPEC_HI - SPEC_LO)) * gh
    g.clearRect(0, 0, w, h)
    const sans = css('--font-sans')
    const mono = css('--font-mono')

    // a faint wash of each frequency's colour, then the grid
    const wash = g.createLinearGradient(0, 0, w, 0)
    const fill = g.createLinearGradient(0, 0, w, 0)
    for (let k = 0; k <= 40; k++) {
      const f = xf((k / 40) * w, w)
      wash.addColorStop(k / 40, freqWash(f, 0.06))
      fill.addColorStop(k / 40, freqWash(f, 0.42))
    }
    g.fillStyle = wash
    g.fillRect(0, ROW, w, gh)
    g.fillStyle = css('--line')
    for (const [f] of GRID_F) g.fillRect(Math.round(fx(f, w)), ROW, 1, gh)
    for (const d of [-12, -6, 6, 12]) g.fillRect(0, Math.round(y(d)), w, 1)
    g.fillStyle = css('--line-strong')
    g.fillRect(0, Math.round(y(0)), w, 1)

    // spectra: after the EQ filled with the frequency colours, before it as a faint fill and a line
    if (player.playing) {
      const n = t.chain.pre.frequencyBinCount
      if (!buf.current || buf.current.data.length !== n || buf.current.pre.length < w) {
        buf.current = { data: new Float32Array(n), pre: new Float32Array(Math.ceil(w)), post: new Float32Array(Math.ceil(w)) }
      }
      const { data, pre, post } = buf.current
      spectrum(t.chain.pre, data, w, pre)
      g.beginPath()
      g.moveTo(0, ROW + gh)
      for (let x = 0; x < w; x++) g.lineTo(x, sy(pre[x]))
      g.lineTo(w, ROW + gh)
      g.closePath()
      g.globalAlpha = 0.1
      g.fillStyle = css('--ink')
      g.fill()
      g.globalAlpha = 1
      if (!set.eqBypass) {
        spectrum(t.chain.post, data, w, post)
        g.beginPath()
        g.moveTo(0, ROW + gh)
        for (let x = 0; x < w; x++) g.lineTo(x, sy(post[x]))
        g.lineTo(w, ROW + gh)
        g.closePath()
        g.fillStyle = fill
        g.fill()
      }
      g.beginPath()
      for (let x = 0; x < w; x++) (x ? g.lineTo : g.moveTo).call(g, x, sy(pre[x]))
      g.strokeStyle = css('--ink-muted')
      g.lineWidth = 1
      g.stroke()
    }

    // curve: the product of every biquad's response, exactly what is heard
    const n = Math.ceil(w / 2)
    const freqs = new Float32Array(n)
    for (let k = 0; k < n; k++) freqs[k] = xf((k / (n - 1)) * w, w)
    const mag = new Float32Array(n).fill(1)
    const tmp = new Float32Array(n)
    const ph = new Float32Array(n)
    for (const f of t.chain.eq.filters.flat()) {
      f.getFrequencyResponse(freqs, tmp, ph)
      for (let k = 0; k < n; k++) mag[k] *= tmp[k]
    }
    // the band of the word under the pointer, as wide as the word's band on its row
    if (word.current) {
      const { row, k } = word.current
      const [x0, x1] = tile(row, k, w)
      g.fillStyle = freqWash(row[k][0], 0.16)
      g.fillRect(x0, ROW, x1 - x0, gh)
    }

    g.beginPath()
    for (let k = 0; k < n; k++) {
      const yy = y(Math.max(-RANGE - 2, Math.min(RANGE + 2, 20 * Math.log10(mag[k]))))
      ;(k ? g.lineTo : g.moveTo).call(g, fx(freqs[k], w), yy)
    }
    g.strokeStyle = css(set.eqBypass ? '--line-strong' : '--viz-primary')
    g.lineWidth = 1.5
    g.stroke()

    // band points in their frequency's colour, numbered: filled when on, hollow when off;
    // the selected one ringed
    g.font = `700 10px ${mono}`
    g.textAlign = 'center'
    g.textBaseline = 'middle'
    const paper = css('--surface-000')
    set.bands.forEach((b, k) => {
      const x = fx(b.freq, w)
      const yy = nodeY(k, y)
      const sel = k === s.band
      g.beginPath()
      g.arc(x, yy, sel ? 9 : 8, 0, Math.PI * 2)
      g.fillStyle = b.on ? freqMark(b.freq) : paper
      g.fill()
      g.strokeStyle = b.on ? freqMark(b.freq) : freqInk(b.freq)
      g.lineWidth = 1
      g.stroke()
      if (sel) {
        g.beginPath()
        g.arc(x, yy, 11, 0, Math.PI * 2)
        g.strokeStyle = css('--ink')
        g.lineWidth = 1.5
        g.stroke()
      }
      g.fillStyle = b.on ? paper : freqInk(b.freq)
      g.fillText(BANDS[k].name, x, yy + 0.5)
    })

    // the selected band's values on a chip beside its point: above it when the chip clears
    // the top label row with room to spare, else below; never over the dB scale at the left
    {
      const b = set.bands[s.band]
      const text = readBand(s.band)
      g.font = `500 12px ${mono}`
      const tw = g.measureText(text).width + 10
      const x = fx(b.freq, w)
      const yy = nodeY(s.band, y)
      const bx = Math.max(SCALE_W, Math.min(w - tw - 2, x - tw / 2))
      const by = yy - 34 >= ROW + 14 ? yy - 34 : yy + 16
      g.globalAlpha = 0.92
      g.fillStyle = paper
      g.beginPath()
      g.roundRect(bx, by, tw, 18, 3)
      g.fill()
      g.globalAlpha = 1
      g.strokeStyle = css('--line')
      g.lineWidth = 1
      g.stroke()
      g.fillStyle = css('--ink')
      g.textAlign = 'left'
      g.fillText(text, bx + 5, by + 9.5)
      g.textAlign = 'center'
    }

    // label rows and scales
    g.fillStyle = css('--surface-100')
    g.fillRect(0, 0, w, ROW)
    g.fillRect(0, ROW + gh, w, ROW)
    // each word on a tint of its own frequency's colour across its whole band
    const words = (row: Word[], weight: number, top: number) => {
      g.font = `${weight} 11px ${sans}`
      row.forEach(([f, txt], k) => {
        const [x0, x1] = tile(row, k, w)
        g.fillStyle = freqTint(f)
        g.fillRect(x0, top, x1 - x0, ROW)
        // the word in the middle of its band
        g.fillStyle = freqInk(f)
        g.fillText(txt, (x0 + x1) / 2, top + ROW / 2 + 1)
      })
    }
    words(TOP_LABELS, 700, 0)
    words(BOTTOM_LABELS, 500, ROW + gh)
    g.font = `400 10px ${mono}`
    g.textAlign = 'left'
    for (const [f, txt] of GRID_F) {
      g.fillStyle = freqInk(f)
      g.fillText(txt, fx(f, w) + 3, ROW + 9)
    }
    g.fillStyle = css('--ink-muted')
    // dB scale at the left: the right edge holds 10k, 空気感 and the LP node
    for (const d of [12, 6, -6, -12]) g.fillText(`${d > 0 ? '+' : ''}${d}`, 4, y(d) - 6)
  })

  const at = (e: PointerEvent | WheelEvent) => {
    const r = c.current!.getBoundingClientRect()
    return { px: e.clientX - r.left, py: e.clientY - r.top }
  }
  // the nearest node within reach, the selected band first when nodes overlap
  const hit = (e: PointerEvent | WheelEvent) => {
    const { w, y } = geo()
    const { px, py } = at(e)
    let best: number | null = null
    let bestD = 12
    for (const k of [s.band, ...BANDS.keys()]) {
      const d = Math.hypot(px - fx(set.bands[k].freq, w), py - nodeY(k, y))
      if (d < bestD) [best, bestD] = [k, d]
    }
    return best
  }
  const down = (e: PointerEvent) => {
    if (!set) return
    const k = hit(e)
    if (k === null) return
    selectBand(k)
    gestures.press()
    // grabbing an off band's point turns it on, but not HP's or LP's: they sit at the graph's
    // edges, off by default, where a stray click would cut the lows or highs (the user, 2026-10-31)
    if (!set.bands[k].on && !isCut(k)) gestures.dragged(() => setBand(i, k, { on: true }))
    drag.current = k
    ;(e.currentTarget as Element).setPointerCapture(e.pointerId)
  }
  // the word whose band on either label row holds the pointer, the band the graph shades
  const wordAt = (e: PointerEvent) => {
    const { w, gh } = geo()
    const { px, py } = at(e)
    const row = py < ROW ? TOP_LABELS : py > ROW + gh ? BOTTOM_LABELS : null
    if (!row) return null
    const k = row.findIndex((_, j) => px < tile(row, j, w)[1])
    return { row, k: k < 0 ? row.length - 1 : k }
  }
  // the Hint of the word, the point or else the graph under the pointer, level with it, on
  // the side away from the pointer
  const hintAt = (e: PointerEvent) => {
    const el = c.current!
    const top = el.getBoundingClientRect().top
    const { gh, y } = geo()
    const px = e.clientX
    const wd = word.current
    if (wd) return showHint(el, wordHint(wd.row[wd.k]), top + (wd.row === TOP_LABELS ? ROW / 2 : ROW + gh + ROW / 2), false, px)
    const k = drag.current ?? hit(e)
    if (k === null) return showHint(el, GRAPH_HINT, top + ROW + gh / 2, false, px)
    showHint(el, { title: bandName(k), text: `${BAND_TIPS[BANDS[k].kind]} 点をドラッグしても動かせる。` }, top + nodeY(k, y), false, px)
  }
  const move = (e: PointerEvent) => {
    word.current = wordAt(e)
    hintAt(e)
    const k = drag.current
    c.current!.style.cursor = k !== null ? 'grabbing' : hit(e) !== null ? 'grab' : ''
    if (k === null) return
    const { w, gh } = geo()
    const { px, py } = at(e)
    const freq = clampTo(BAND_PARAMS.freq, xf(Math.max(0, Math.min(w, px)), w))
    if (isCut(k)) return gestures.dragged(() => setBand(i, k, { freq }))
    const db = ((ROW + gh / 2 - py) / (gh / 2)) * RANGE
    gestures.dragged(() => setBand(i, k, { freq, gain: Math.round(clampTo(BAND_PARAMS.gain, db) * 2) / 2 }))
  }
  // wheel over a bell narrows or widens it, as in most EQ plugins
  const wheel = (e: WheelEvent) => {
    const k = hit(e)
    if (k === null || BANDS[k].kind !== 'bell') return
    e.preventDefault()
    selectBand(k)
    gestures.stepped(() => setBand(i, k, { q: clampTo(BAND_PARAMS.q, set.bands[k].q * (e.deltaY < 0 ? 1.1 : 1 / 1.1)) }))
  }

  const leave = () => {
    word.current = null
    hideHint()
  }
  function readBand(k: number) {
    const b = set.bands[k]
    return isCut(k) ? `${fmtHz(b.freq)}  ${b.slope} dB/oct` : `${fmtHz(b.freq)}  ${fmtDb(b.gain)}`
  }

  // a release, or a cancel (the browser took the pointer), ends the drag
  const end = () => {
    drag.current = null
    gestures.release()
  }
  const up = (e: PointerEvent) => {
    end()
    move(e)
  }

  return (
    <canvas
      class="eqgraph"
      ref={c}
      data-hint-own
      onPointerDown={down}
      onPointerMove={move}
      onPointerLeave={leave}
      onPointerUp={up}
      onPointerCancel={end}
      onWheel={wheel}
    />
  )
}

function BandKnob({ k, big }: { k: BandKey; big?: boolean }) {
  const s = useApp()
  const i = s.selected
  return (
    <Knob
      param={BAND_PARAMS[k]}
      value={s.settings[i].bands[s.band][k]}
      def={DEFAULTS.bands[s.band][k]}
      size={big ? 'md' : 'sm'}
      onChange={(v) => setBand(i, s.band, { [k]: v })}
      onReset={() => resetBand(i, s.band, k)}
    />
  )
}

export function Eq({ big }: { big?: boolean }) {
  const s = useApp()
  const set = s.settings[s.selected]
  const k = s.band
  const b = set.bands[k]
  const kind = BANDS[k].kind
  const cut = kind === 'hp' || kind === 'lp'
  return (
    <div class={`pr-device eq ${big ? 'big' : ''}`} {...pluginHints}>
      <div class="graph">
        <div class="screen">
          <EqGraph />
        </div>
      </div>
      {/* one stop in the Tab order, the chosen band; the arrow keys choose among them */}
      <div
        class="pr-seg pr-seg--fill"
        role="radiogroup"
        aria-label="バンド"
        onKeyDown={(e) => {
          const to = { ArrowRight: k + 1, ArrowDown: k + 1, ArrowLeft: k - 1, ArrowUp: k - 1, Home: 0, End: BANDS.length - 1 }[e.key]
          if (to === undefined) return
          e.preventDefault()
          const j = (to + BANDS.length) % BANDS.length
          selectBand(j)
          ;(e.currentTarget.children[j] as HTMLElement).focus()
        }}
      >
        {BANDS.map((x, j) => (
          <button
            class="pr-seg__opt pr-focusable"
            role="radio"
            aria-checked={j === k}
            tabIndex={j === k ? 0 : -1}
            aria-label={bandName(j)}
            onClick={() => selectBand(j)}
            {...hint(bandName(j), BAND_TIPS[x.kind])}
            key={j}
          >
            <i class={`pr-seg__led ${set.bands[j].on ? 'is-lit' : ''}`} style={ledTint(freqMark(set.bands[j].freq))} />
            <Glyph kind={x.kind} />
            {x.kind === 'bell' && x.name}
          </button>
        ))}
      </div>
      {/* only the band's own controls: HP and LP have Freq and Slope, shelves Freq and Gain, bells Freq, Gain and Q */}
      <div class="eqknobs">
        <div
          class="eqlead"
          {...hint(
            bandName(k),
            cut
              ? 'このバンド（EQ で動かす 1 か所分）のオン・オフ。オフのあいだは、このバンドだけ効かない。'
              : 'このバンド（EQ で動かす 1 か所分）のオン・オフ。オフのあいだは、このバンドだけ効かない。グラフの点をつかんでもオンになる。',
          )}
        >
          <Toggle
            label={kind === 'bell' ? BANDS[k].name : ''}
            glyph={kind}
            name={`EQ ${BANDS[k].name}`}
            pressed={b.on}
            color={freqMark(b.freq)}
            onChange={() => setBand(s.selected, k, { on: !b.on })}
          />
        </div>
        <div class="eqslots">
          <BandKnob k="freq" big={big} key="freq" />
          {cut ? (
            <div class="slope" key="slope" {...hint('Slope', SLOPE_TIP)}>
              <Segmented
                label="Slope"
                unit="dB/oct"
                options={SLOPES}
                value={b.slope}
                onChange={(v) => setBand(s.selected, k, { slope: v as (typeof SLOPES)[number] })}
              />
            </div>
          ) : (
            <BandKnob k="gain" big={big} key="gain" />
          )}
          {kind === 'bell' && <BandKnob k="q" big={big} key="q" />}
          <AutoGain name="EQ" plugin="eq" big={big} key="ag" />
        </div>
      </div>
    </div>
  )
}
