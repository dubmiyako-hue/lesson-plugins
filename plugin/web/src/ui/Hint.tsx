// PRAYROOM's Hint, one for the page: what the control under the pointer (or reached with
// Tab) is, floating beside its plugin, level with the control; inside the plugin's left
// edge when it is open large, or its right edge while the pointer (or the control the
// keyboard reached) is in the left half, so the Hint never covers it (asked for by the
// user on 2026-10-31; PRAYROOM keeps it at the left edge). A control says what it is with
// `hint(title, text)` spread on its element; a graph that tells its parts apart marks
// itself `data-hint-own` and calls showHint itself.
export type HintText = { title: string; text: string; aside?: string }

let one: ReturnType<typeof window.PRAYROOM.Hint> | null = null
let now = '' // what is shown and where, so moving within one control does not redraw it
const get = () => {
  if (!one) {
    one = window.PRAYROOM.Hint(document.body)
    addEventListener('scroll', hideHint, true)
    addEventListener('resize', hideHint)
  }
  return one
}

let keyed = false // the Hint shown is the one the keyboard brought up

// the plug-in's settings can turn the Hint off
let enabled = true
export function setHintsOn(on: boolean) {
  enabled = on
  if (!on) hideHint()
}

// `keys`: brought up by the keyboard, not the pointer. `px`: the pointer's x, which picks the
// side; without it, the control's middle does
export function showHint(from: Element, h: HintText, y?: number, keys = false, px?: number) {
  const device = from.closest('.pr-device')
  if (!device || !enabled) return
  keyed = keys
  // a host with no box of its own (display: contents) is placed by what it holds
  const box = from.getBoundingClientRect().height ? from : (from.firstElementChild ?? from)
  const r = box.getBoundingClientRect()
  const at = Math.round(y ?? r.top + r.height / 2)
  const d = device.getBoundingClientRect()
  const right = (px ?? r.left + r.width / 2) < d.left + d.width / 2
  const key = `${h.title}\n${h.text}\n${at}\n${right}`
  if (key === now) return
  now = key
  const one = get()
  one.show(h, right ? { beside: rightEdge(d, one.el), y: at, inside: true } : { beside: device, y: at, inside: !!from.closest('.pr-scrim') })
}

// a box PRAYROOM places the Hint inside the left edge of, as far in as puts it inside the
// plugin's right edge (the same gap, space-2)
function rightEdge(d: DOMRect, el: HTMLElement) {
  const gap = parseFloat(getComputedStyle(el).getPropertyValue('--space-2')) || 8
  return new DOMRect(d.right - el.offsetWidth - 2 * gap, d.top, el.offsetWidth + 2 * gap, d.height)
}

export function hideHint() {
  now = ''
  keyed = false
  one?.hide()
}

// `more` is read on after the text only when the keyboard comes to the control: what the
// words under a knob say, which the mouse reads on the words themselves
export const hint = (title: string, text: string, aside?: string, more?: string) => ({ 'data-hint-title': title, 'data-hint': text, 'data-hint-aside': aside, 'data-hint-more': more })

const withMore = (el: HTMLElement, h: HintText): HintText => (el.dataset.hintMore ? { ...h, text: h.text + el.dataset.hintMore } : h)

// after what a control says has changed while the keyboard is on it (a word under the knob
// changed as its value was stepped); not while the pointer has brought up a hint since, as
// when the focused knob is dragged
export function rehint(host: Element) {
  const f = document.activeElement
  if (!keyed || !f || !host.contains(f) || !f.matches(':focus-visible')) return
  const x = hinted(f)
  if (x) showHint(x.el, withMore(x.el, x.h), undefined, true)
}

function hinted(t: EventTarget | null) {
  const el = (t as Element | null)?.closest<HTMLElement>('[data-hint]')
  return el ? { el, h: { title: el.dataset.hintTitle!, text: el.dataset.hint!, aside: el.dataset.hintAside } } : null
}

// on a plugin's root: the hint of whatever the pointer comes onto, none where nothing has
// one; and of a control the keyboard focus comes to (not one a click focused)
export const pluginHints = {
  onPointerOver: (e: PointerEvent) => {
    if ((e.target as Element).closest('[data-hint-own]')) return
    const x = hinted(e.target)
    if (x) showHint(x.el, x.h, undefined, false, e.clientX)
    else hideHint()
  },
  // across the middle of a wide control (the level history), the shown Hint changes sides
  onPointerMove: (e: PointerEvent) => {
    if (!now || keyed || (e.target as Element).closest('[data-hint-own]')) return
    const x = hinted(e.target)
    if (x) showHint(x.el, x.h, undefined, false, e.clientX)
  },
  onPointerLeave: hideHint,
  onFocusIn: (e: FocusEvent) => {
    const t = e.target as Element
    const x = hinted(t)
    if (!x || !t.matches(':focus-visible')) return
    showHint(x.el, withMore(x.el, x.h), undefined, true)
    t.setAttribute('aria-describedby', get().el.id)
  },
  onFocusOut: (e: FocusEvent) => {
    ;(e.target as Element).removeAttribute('aria-describedby')
    hideHint()
  },
}
