import { useRef } from 'preact/hooks'
import { player, resetParam, setParam } from '../app'
import { DEFAULTS } from '../chain'
import { characterWords, type Word, type Words } from '../character'
import { RATIO_INF } from '../dsp'
import { PARAMS, clampTo, fmtDb, type NumKey } from '../params'
import { GrMeter, Knob } from './Knob'
import { closeGesture, openGesture, within, type Gesture } from '../gestures'
import { hint, pluginHints } from './Hint'
import { AutoGain } from './PluginHeader'
import { TICK_HZ } from '../app'
import { css, fitCanvas, useApp, useFrame } from './hooks'

// the words under each knob (Words below), which its Hint reads on when the keyboard comes
// to it: the words cannot be reached with Tab
const UNDER: Partial<Record<NumKey, (keyof Words)[]>> = { ratio: ['ratio'], attack: ['attack', 'whole'], release: ['release', 'whole'] }

function K({ k, big }: { k: NumKey; big?: boolean }) {
  const s = useApp()
  const i = s.selected
  const w = characterWords(s.settings[i])
  const more = (w && UNDER[k]?.map((u) => (w[u] ? `「${w[u]!.word}」：${w[u]!.tip}` : '')).join('')) || undefined
  return (
    <Knob
      param={PARAMS[k]}
      value={s.settings[i][k]}
      def={DEFAULTS[k]}
      size={big ? 'md' : 'sm'}
      head={k === 'threshold' ? 'paper' : 'ink'}
      more={more}
      onChange={(v) => setParam(i, k, v)}
      onReset={() => resetParam(i, k)}
    />
  )
}

const DB_LO = -60 // bottom of the display; 0 dB at the top
// The web app's 40 points (about 0.2 s at 48 kHz) and 4 points (the worklet's batch) were counted in points; the
// plug-in runs at the host's rate and sends its points 30 times a second, so both are kept in time.
const HOLD_S = (40 * 256) / 48000 // the GR readout holds its peak
const holdOf = (rate: number) => Math.max(1, Math.round(HOLD_S * rate))
// points the picture may slide between batches: one batch
const lagMaxOf = (rate: number) => Math.max(4, Math.ceil(rate / TICK_HZ))

// The gain reduction of the selected track now, held briefly so the number can be read.
const grNow = (i: number) => {
  const hist = player.tracks[i]?.chain.history
  return player.playing && hist ? hist.recentGr(holdOf(hist.rate)) : 0
}

// The last seconds of the selected track, newest at the right: the input level
// (a thin line), the output level before makeup (filled), and the gain reduction as a
// line down from the top, on the same dB scale as the threshold.
function CompDisplay() {
  const s = useApp()
  const c = useRef<HTMLCanvasElement>(null)
  const drag = useRef(false)
  const gesture = useRef<Gesture | null>(null) // the host's gesture of the drag
  const i = s.selected
  const set = s.settings[i]
  const yOf = (db: number, h: number) => (Math.min(0, Math.max(DB_LO, db)) / DB_LO) * h

  useFrame(() => {
    const hist = player.tracks[i]?.chain.history
    if (!c.current || !set) return
    const { g, w, h } = fitCanvas(c.current)
    g.clearRect(0, 0, w, h)
    g.fillStyle = css('--line')
    for (const d of [-12, -24, -36, -48]) g.fillRect(0, Math.round(yOf(d, h)), w, 1)

    if (hist) {
      // Every point at its own x, oldest at the left. The points come in batches; between
      // them the picture slides left by the time since the last one, so it scrolls evenly.
      // (Taking the loudest point per pixel column instead re-split the columns on every
      // batch, and the same peak jumped by up to 17 dB from frame to frame.)
      const LAG_MAX = lagMaxOf(hist.rate)
      const lag = player.playing ? Math.min(LAG_MAX, ((performance.now() - hist.at) / 1000) * hist.rate) : 0
      const dx = w / (hist.size - 1 - LAG_MAX)
      const xAt = (j: number) => (j - lag - LAG_MAX) * dx
      const at = (arr: Float32Array, j: number) => arr[(hist.pos + j) % hist.size]
      const db = (v: number) => 20 * Math.log10(v || 1e-9)
      const path = (arr: Float32Array, y: (v: number) => number) => {
        g.beginPath()
        for (let j = 0; j < hist.size; j++) (j ? g.lineTo : g.moveTo).call(g, xAt(j), y(at(arr, j)))
      }
      const area = (arr: Float32Array, fill: string) => {
        path(arr, (v) => yOf(db(v), h))
        g.lineTo(xAt(hist.size - 1), h)
        g.lineTo(xAt(0), h)
        g.closePath()
        g.fillStyle = fill
        g.fill()
      }
      const line = (arr: Float32Array, y: (v: number) => number, color: string, width: number) => {
        path(arr, y)
        g.strokeStyle = color
        g.lineWidth = width
        g.stroke()
      }
      g.globalAlpha = 0.11
      area(hist.inp, css('--ink'))
      g.globalAlpha = 1
      area(hist.out, css('--viz-primary-fill'))
      line(hist.inp, (v) => yOf(db(v), h), css('--line-strong'), 1)
      line(hist.out, (v) => yOf(db(v), h), css('--viz-primary'), 1.5)
      line(hist.gr, (v) => yOf(-v, h), css('--viz-gr'), 1.5)
    }

    // scale, at the right: the left edge holds the threshold's handle and chip
    const mono = css('--font-mono')
    g.font = `400 10px ${mono}`
    g.textAlign = 'right'
    g.textBaseline = 'middle'
    g.fillStyle = css('--ink-muted')
    for (const d of [-12, -24, -36, -48]) g.fillText(`${d}`, w - 4, yOf(d, h) + 7)

    // threshold: a solid line over the waves, its handle, and its value on a chip
    const ty = Math.round(yOf(set.threshold, h)) + 0.5
    const ink = css('--ink')
    g.strokeStyle = css('--surface-000')
    g.lineWidth = 3
    g.beginPath()
    g.moveTo(0, ty)
    g.lineTo(w, ty)
    g.stroke()
    g.strokeStyle = ink
    g.lineWidth = 1.5
    g.setLineDash([6, 3])
    g.stroke()
    g.setLineDash([])
    g.fillStyle = ink
    g.beginPath()
    g.moveTo(0, ty - 6)
    g.lineTo(9, ty)
    g.lineTo(0, ty + 6)
    g.fill()
    g.font = `500 12px ${mono}`
    const chip = (text: string, x: number, y: number, right: boolean, color: string) => {
      const tw = g.measureText(text).width
      const left = right ? x - tw - 8 : x
      g.fillStyle = css('--surface-000')
      g.globalAlpha = 0.9
      g.beginPath()
      g.roundRect(left, y, tw + 8, 18, 3)
      g.fill()
      g.globalAlpha = 1
      g.fillStyle = color
      g.textAlign = 'left'
      g.textBaseline = 'middle'
      g.fillText(text, left + 4, y + 9.5)
    }
    const above = ty - 22 >= 0
    chip(fmtDb(set.threshold), 14, above ? ty - 22 : ty + 4, false, ink)
    chip(`GR ${fmtDb(-grNow(i))}`, w - 4, 4, true, ink)
  })

  // the threshold line drags up and down, like the knob
  const dbAt = (e: PointerEvent) => {
    const r = c.current!.getBoundingClientRect()
    return ((e.clientY - r.top) / r.height) * DB_LO
  }
  const down = (e: PointerEvent) => {
    if (!onLine(e)) return
    drag.current = true
    closeGesture(gesture.current)
    gesture.current = openGesture()
    ;(e.currentTarget as Element).setPointerCapture(e.pointerId)
  }
  const onLine = (e: PointerEvent) => {
    const r = c.current!.getBoundingClientRect()
    return Math.abs(e.clientY - r.top - yOf(set.threshold, r.height)) <= 8
  }
  const move = (e: PointerEvent) => {
    if (drag.current) within(gesture.current, () => setParam(i, 'threshold', Math.round(clampTo(PARAMS.threshold, dbAt(e)) * 2) / 2))
    c.current!.style.cursor = drag.current || onLine(e) ? 'ns-resize' : ''
  }
  // a release, or a cancel (the browser took the pointer), ends the drag
  const end = () => {
    drag.current = false
    closeGesture(gesture.current)
    gesture.current = null
  }
  return <canvas class="compgraph" ref={c} onPointerDown={down} onPointerMove={move} onPointerUp={end} onPointerCancel={end} />
}

// Input level against output level: the line bends at the threshold and rises at
// 1 / ratio above it (flat at ∞:1). The dot is where the latest peak sits on it.
function Transfer() {
  const s = useApp()
  const c = useRef<HTMLCanvasElement>(null)
  const set = s.settings[s.selected]
  useFrame(() => {
    if (!c.current || !set) return
    const { g, w, h } = fitCanvas(c.current)
    g.clearRect(0, 0, w, h)
    const X = (db: number) => ((db - DB_LO) / -DB_LO) * w
    const Y = (db: number) => h - ((db - DB_LO) / -DB_LO) * h
    const t = set.threshold
    const out = (x: number) => (x <= t ? x : set.ratio >= RATIO_INF ? t : t + (x - t) / set.ratio)
    g.fillStyle = css('--line')
    for (const d of [-12, -24, -36, -48]) {
      g.fillRect(Math.round(X(d)), 0, 1, h)
      g.fillRect(0, Math.round(Y(d)), w, 1)
    }
    // unity, for comparison
    g.strokeStyle = css('--line-strong')
    g.lineWidth = 1
    g.setLineDash([2, 3])
    g.beginPath()
    g.moveTo(X(DB_LO), Y(DB_LO))
    g.lineTo(X(0), Y(0))
    g.stroke()
    g.strokeStyle = css('--ink')
    g.setLineDash([6, 3])
    g.beginPath()
    g.moveTo(Math.round(X(t)) + 0.5, 0)
    g.lineTo(Math.round(X(t)) + 0.5, h)
    g.stroke()
    g.setLineDash([])
    g.strokeStyle = css(set.compBypass ? '--line-strong' : '--viz-primary')
    g.lineWidth = 2
    g.beginPath()
    g.moveTo(X(DB_LO), Y(DB_LO))
    g.lineTo(X(t), Y(t))
    g.lineTo(X(0), Y(out(0)))
    g.stroke()
    const hist = player.tracks[s.selected]?.chain.history
    if (player.playing && hist) {
      let peak = 0
      for (let k = 1, n = holdOf(hist.rate); k <= n; k++) peak = Math.max(peak, hist.inp[(hist.pos - k + hist.size) % hist.size])
      const x = Math.min(0, 20 * Math.log10(peak || 1e-9))
      // in silence no dot, rather than one parked in the corner
      if (x >= DB_LO) {
        g.fillStyle = css('--viz-gr')
        g.beginPath()
        g.arc(X(x), Y(out(x)), 3.5, 0, Math.PI * 2)
        g.fill()
      }
    }
    g.font = `500 11px ${css('--font-sans')}`
    g.fillStyle = css('--ink-muted')
    g.textBaseline = 'alphabetic'
    g.textAlign = 'right'
    g.fillText('入力 →', w - 4, h - 4)
    g.textAlign = 'left'
    g.fillText('↑ 出力', 4, 12)
  })
  return (
    <div class="screen transfer" {...hint('入力 → 出力', '横が入ってくる音の大きさ、縦が出ていく音の大きさ。縦の点線が Threshold で、そこから右は Ratio の分だけ線が寝て、出る音が抑えられる。再生中の点が今の音。')}>
      <canvas ref={c} />
    </div>
  )
}

// PRAYROOM's Tag, drawn with its classes as it keeps no state of its own: a framed word, or
// its room kept with nothing in it, so the knobs and the graphs above do not move as the
// words come and go.
function Tag({ w, col }: { w: Word | null | undefined; col?: number }) {
  return (
    <span class={`pr-tag${w ? '' : ' is-off'}`} style={col ? { gridColumn: String(col) } : undefined} {...(w ? hint(w.word, w.tip) : {})}>
      {w ? w.word : ' '}
    </span>
  )
}

// Under the knobs: what the Ratio, Attack and Release values are set for, each under its
// own knob, and under PRAYROOM's Bracket from Attack to Release where the two together are
// recommended (character.ts).
function Words() {
  const s = useApp()
  const w = characterWords(s.settings[s.selected])
  return (
    <>
      <Tag w={w?.ratio} col={2} />
      <Tag w={w?.attack} col={3} />
      <Tag w={w?.release} col={4} />
      <div class={`pr-bracket${w?.whole ? '' : ' is-off'}`}>
        <Tag w={w?.whole} />
      </div>
    </>
  )
}

export function Comp({ big }: { big?: boolean }) {
  const s = useApp()
  return (
    <div class={`pr-device comp ${big ? 'big' : ''}`} {...pluginHints}>
      <div class="graph">
        <div class="screen-row">
          <Transfer />
          <div class="screen" {...hint('音量の流れ', '直近の数秒の音量が右から左へ流れる。灰色が入力、塗りが出力（Auto Gain の前）、上から下りる線が下げている量（GR）。横の点線が Threshold で、左端の三角を上下にドラッグして動かせる。')}>
            <CompDisplay />
          </div>
          <GrMeter read={() => grNow(s.selected)} tip="いま圧縮（音量を抑えること）で下げている量。上から下へ長く伸びるほど、強く抑えている。" />
        </div>
      </div>
      <div class="cknobs">
        <K k="threshold" big={big} />
        <K k="ratio" big={big} />
        <K k="attack" big={big} />
        <K k="release" big={big} />
        <AutoGain name="Compressor" plugin="comp" big={big} />
        <Words />
      </div>
    </div>
  )
}
