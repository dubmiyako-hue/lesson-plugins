// The plug-in's window: PRAYROOM's PluginWindow (header, body, footer) without its preset list and A/B (the user left
// them out), and with a 設定 key before the power switch. PRAYROOM has no menu part, so the key opens a panel under it with
// the PC's settings (PRAYROOM behavior.md, 状態の保存: not in the session): the window scale, the theme and the Hint.
// The key again, Esc or a press outside closes it. The plug-in saves each choice and sizes the window; the page itself
// stays at its CSS size (the plug-in scales the web view).
import { host, type Ready, type Scales, type Theme } from './host'
import { setHintsOn } from './ui/Hint'

export const WIDTH = 960 // PRAYROOM's L window
export const HEIGHT = 560

const THEMES: { value: Theme; label: string }[] = [
  { value: 'light', label: 'Light' },
  { value: 'dark', label: 'Dark' },
  { value: 'studio', label: 'Studio' },
]

export function applyTheme(theme: Theme) {
  document.documentElement.dataset.theme = theme
}

type Options = { title: string; ready: Ready; onBypass: (bypassed: boolean) => void }

export function mountWindow({ title, ready, onBypass }: Options) {
  const P = window.PRAYROOM
  const w = P.PluginWindow(document.getElementById('app')!, { title, width: WIDTH, height: HEIGHT, version: ready.version, onBypass })
  const head = w.el.querySelector<HTMLElement>('.pr-window__header')!
  head.querySelector('.pr-window__preset')?.remove()
  head.querySelector(':scope > .pr-seg')?.remove() // A/B
  const keyHost = document.createElement('div')
  head.insertBefore(keyHost, head.querySelector('.pr-switch'))
  const key = P.Key(keyHost, { label: '設定', pressed: false, onClick: () => (panel.hidden ? open() : close()) })
  w.setReadout('')

  // ---- the settings panel ----
  const panel = document.createElement('div')
  panel.className = 'settings'
  panel.hidden = true
  w.el.append(panel)
  const box = P.Panel(panel, {})
  const scaleSeg = P.Segmented(box.body, {
    label: '拡大率',
    options: ready.scales.map((c) => ({ value: c.value, label: `${Math.round(c.value * 100)}%` })),
    value: ready.scale,
    onChange: (v) => void host().setScale(Number(v)).then(showScales),
  })
  const themeSeg = P.Segmented(box.body, {
    label: 'テーマ',
    options: THEMES,
    value: ready.theme,
    onChange: (v) => {
      applyTheme(v as Theme)
      host().setTheme(v as Theme)
    },
  })
  P.Segmented(box.body, {
    label: '説明の欄',
    options: [
      { value: 1, label: '出す' },
      { value: 0, label: '出さない' },
    ],
    value: ready.hints ? 1 : 0,
    onChange: (v) => {
      setHintsOn(v === 1)
      host().setHints(v === 1)
    },
  })
  // a scale the window's screen has no room for cannot be chosen
  function showScales(s: Scales) {
    scaleSeg.value = s.scale
    scaleSeg.el.querySelectorAll<HTMLButtonElement>('.pr-seg__opt').forEach((b, k) => (b.disabled = !s.scales[k]?.enabled))
  }
  showScales(ready)
  void themeSeg

  function open() {
    panel.hidden = false
    key.pressed = true
    void host().scales().then(showScales) // the window may have moved to another screen
  }
  function close() {
    panel.hidden = true
    key.pressed = false
  }
  addEventListener(
    'pointerdown',
    (e) => {
      if (!panel.hidden && !panel.contains(e.target as Node) && !keyHost.contains(e.target as Node)) close()
    },
    true,
  )
  addEventListener('keydown', (e) => {
    if (e.key === 'Escape' && !panel.hidden) close()
  })

  return {
    body: w.body,
    setReadout: w.setReadout,
    setBypassed: (bypassed: boolean) => (w.bypass.pressed = bypassed),
  }
}

// PRAYROOM behavior.md (キーボード): a press with the mouse does not take the keyboard (it stays with the host); only Tab
// brings it to a control, and Esc lets it go. A typed value's field is the one exception while it is open.
export function keepKeyboardWithHost() {
  addEventListener(
    'mousedown',
    (e) => {
      if (!(e.target as Element).closest('input, textarea')) e.preventDefault()
    },
    true,
  )
  addEventListener('keydown', (e) => {
    const f = document.activeElement as HTMLElement | null
    if (e.key === 'Escape' && f && f !== document.body && !f.matches('input')) f.blur()
  })
}
