// The host's gestures (PRAYROOM behavior.md, 操作 > ジェスチャー). The host treats what happens between a begin and an end
// as one edit (an automation pass, one undo step):
//   a drag:           from the press to the release
//   the wheel, keys:  from the first step until 250 ms without another
//   a reset, a typed value, a toggle, a segment:  one change, begun and ended at once ("complete")
// A control opens a gesture, makes its changes inside it (`within`), and closes it. The changes it makes go to the host
// as begin (the first change of a parameter in the gesture), then move; closing sends end for each parameter it began.
// A change made outside any gesture is complete.
import type { Phase } from './host'

export const IDLE_MS = 250

export type Gesture = { ids: Set<string>; timer: number; closed: boolean }

let sendEnd: (id: string) => void = () => {}
let released: (ids: string[]) => void = () => {}
let current: Gesture | null = null
const open_ = new Set<Gesture>()

// `end` sends a parameter's end to the host; `release` is told the parameters a gesture let go of, so the screen can
// take the host's value for them again.
export function connectGestures(end: (id: string) => void, release: (ids: string[]) => void) {
  sendEnd = end
  released = release
}

export function openGesture(): Gesture {
  const g = { ids: new Set<string>(), timer: 0, closed: false }
  open_.add(g)
  return g
}

export function closeGesture(g: Gesture | null) {
  if (!g || g.closed) return
  g.closed = true
  clearTimeout(g.timer)
  open_.delete(g)
  for (const id of g.ids) sendEnd(id)
  released([...g.ids])
}

// closes `g` when `ms` pass without another call (the wheel's and the keys' gestures)
export function closeWhenIdle(g: Gesture, ms = IDLE_MS) {
  clearTimeout(g.timer)
  g.timer = setTimeout(() => closeGesture(g), ms)
}

// A graph's two gestures, kept apart: a drag from the press to the release (or a cancel), and the wheel's, which closes
// when IDLE_MS pass without a step. A step while dragging does not close the drag.
export function graphGestures() {
  let drag: Gesture | null = null
  let wheel: Gesture | null = null
  return {
    press() {
      closeGesture(drag)
      drag = openGesture()
    },
    release() {
      closeGesture(drag)
      drag = null
    },
    dragged<T>(fn: () => T): T {
      return within(drag, fn)
    },
    stepped<T>(fn: () => T): T {
      if (!wheel || wheel.closed) wheel = openGesture()
      closeWhenIdle(wheel)
      return within(wheel, fn)
    },
  }
}

// runs `fn` with its parameter changes belonging to `g`
export function within<T>(g: Gesture | null, fn: () => T): T {
  const outer = current
  current = g && !g.closed ? g : null
  try {
    return fn()
  } finally {
    current = outer
  }
}

// the phase of a change to `id` made now. A parameter is in one gesture at a time: another open gesture that holds it
// (the wheel's, waiting out its 250 ms, when a drag or a reset comes) ends it first, at the value it left it at.
export function phaseOf(id: string): Phase {
  for (const g of open_) {
    if (g === current || !g.ids.has(id)) continue
    g.ids.delete(id)
    sendEnd(id)
    released([id])
  }
  if (!current) return 'complete'
  if (current.ids.has(id)) return 'move'
  current.ids.add(id)
  return 'begin'
}

// whether some open gesture holds `id`: the host's value for it waits until the gesture ends
export function held(id: string) {
  for (const g of open_) if (g.ids.has(id)) return true
  return false
}

// every open gesture, closed (the window is going away)
export function closeAll() {
  for (const g of [...open_]) closeGesture(g)
}
