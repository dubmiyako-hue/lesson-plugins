import type { RefObject } from 'preact'
import { useEffect, useRef, useState } from 'preact/hooks'
import { data, player, state, subscribe } from '../app'
import { createFramePolicy } from '../frames'

// Re-renders the component whenever the app state changes.
export function useApp() {
  const [, set] = useState(0)
  useEffect(() => subscribe(() => set((n) => n + 1)), [])
  return state
}

// Runs `fn` on the animation frames the screen needs while mounted (src/frames.ts).
const frames = createFramePolicy({ playing: () => player.playing, hidden: () => document.hidden })
const invalidate = () => frames.invalidate(performance.now())
subscribe(invalidate)
for (const type of ['pointermove', 'pointerdown', 'pointerup', 'pointercancel', 'wheel', 'keydown', 'keyup', 'focusin', 'focusout', 'resize', 'visibilitychange'])
  addEventListener(type, invalidate, { capture: true, passive: true })
new MutationObserver(invalidate).observe(document.documentElement, { attributes: true })
document.fonts?.ready.then(invalidate)
export function useFrame(fn: () => void) {
  useEffect(() => {
    let id = 0
    let seen = data.stamp
    const tick = () => {
      if (data.stamp !== seen) {
        seen = data.stamp
        invalidate()
      }
      if (frames.shouldDraw(performance.now())) fn()
      id = requestAnimationFrame(tick)
    }
    id = requestAnimationFrame(tick)
    return () => cancelAnimationFrame(id)
  })
}

// While a box is being scrolled with the wheel, the wheel keeps scrolling it when a knob or
// fader comes under the pointer: a turn only starts 0.4 s after the box last moved, as in
// a browser page or a DAW's mixer.
const LATCH_MS = 400
export function useScrollLatch(ref: RefObject<HTMLElement>) {
  const last = useRef(-Infinity)
  useEffect(() => {
    const el = ref.current
    if (!el) return
    const scrolled = () => (last.current = performance.now())
    const wheel = (e: WheelEvent) => {
      if (performance.now() - last.current < LATCH_MS) e.stopPropagation()
    }
    el.addEventListener('scroll', scrolled)
    el.addEventListener('wheel', wheel, { capture: true, passive: true })
    return () => {
      el.removeEventListener('scroll', scrolled)
      el.removeEventListener('wheel', wheel, { capture: true })
    }
  })
}

export const css = (name: string) => getComputedStyle(document.documentElement).getPropertyValue(name).trim()

export function fitCanvas(c: HTMLCanvasElement) {
  const dpr = window.devicePixelRatio || 1
  const w = c.clientWidth
  const h = c.clientHeight
  if (c.width !== Math.round(w * dpr) || c.height !== Math.round(h * dpr)) {
    c.width = Math.round(w * dpr)
    c.height = Math.round(h * dpr)
  }
  const g = c.getContext('2d')!
  g.setTransform(dpr, 0, 0, dpr, 0, 0)
  return { g, w, h }
}
