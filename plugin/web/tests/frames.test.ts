import { describe, expect, it } from 'vitest'
import { GRACE_MS, SAFETY_MS, createFramePolicy } from '../src/frames'

const make = (playing = false, hidden = false) => {
  const st = { playing, hidden }
  return { st, p: createFramePolicy({ playing: () => st.playing, hidden: () => st.hidden }) }
}

describe('frame policy', () => {
  it('draws every frame while audio plays', () => {
    const { p } = make(true)
    for (let t = 10_000; t < 10_100; t += 16) expect(p.shouldDraw(t)).toBe(true)
  })
  it('draws only the safety frames when idle', () => {
    const { p } = make()
    let drawn = 0
    for (let t = 10_000; t < 12_000; t += 16) if (p.shouldDraw(t)) drawn++
    expect(drawn).toBeLessThanOrEqual(Math.ceil(2000 / SAFETY_MS) + 1)
    expect(drawn).toBeGreaterThanOrEqual(2000 / SAFETY_MS - 1)
  })
  it('draws every frame for GRACE_MS after a change, then stops', () => {
    const { p } = make()
    p.shouldDraw(10_000)
    p.invalidate(10_100)
    for (let t = 10_116; t < 10_100 + GRACE_MS; t += 16) expect(p.shouldDraw(t)).toBe(true)
    expect(p.shouldDraw(10_100 + GRACE_MS + 16)).toBe(false)
  })
  it('gives every caller of one frame the same answer', () => {
    const { p } = make()
    p.shouldDraw(10_000)
    const first = p.shouldDraw(10_016)
    expect(p.shouldDraw(10_016)).toBe(first)
    expect(p.shouldDraw(10_016)).toBe(first)
  })
  it('does not draw while hidden, even when playing or changed', () => {
    const { st, p } = make(true, true)
    p.invalidate(10_000)
    expect(p.shouldDraw(10_016)).toBe(false)
    st.hidden = false
    expect(p.shouldDraw(10_032)).toBe(true)
  })
})
