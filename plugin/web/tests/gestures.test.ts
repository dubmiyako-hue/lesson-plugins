// A parameter is in one gesture at a time (src/gestures.ts phaseOf): a change to one that another open gesture holds
// ends it there first, so the host gets an end before the new begin or complete.
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { IDLE_MS, closeAll, closeGesture, connectGestures, graphGestures, held, openGesture, phaseOf, within } from '../src/gestures'

const log: string[] = []

beforeEach(() => {
  closeAll()
  connectGestures(
    (id) => log.push(`end ${id}`),
    (ids) => log.push(`released ${ids.join(',')}`),
  )
  log.length = 0
})

afterEach(() => {
  vi.useRealTimers()
})

it('a gesture taking a parameter another one holds ends it there first, and the other keeps the rest', () => {
  const wheel = openGesture()
  expect(within(wheel, () => phaseOf('b1_gain'))).toBe('begin')
  expect(within(wheel, () => phaseOf('b1_freq'))).toBe('begin')
  expect(within(wheel, () => phaseOf('b1_gain'))).toBe('move')
  expect(log).toEqual([])

  const drag = openGesture()
  expect(within(drag, () => phaseOf('b1_gain'))).toBe('begin')
  expect(log).toEqual(['end b1_gain', 'released b1_gain'])
  expect(within(drag, () => phaseOf('b1_gain'))).toBe('move')
  expect(held('b1_gain')).toBe(true)

  // the wheel's 250 ms pass: it ends only what it still holds
  closeGesture(wheel)
  expect(log).toEqual(['end b1_gain', 'released b1_gain', 'end b1_freq', 'released b1_freq'])
  expect(held('b1_gain')).toBe(true)
  closeGesture(drag)
  expect(log.slice(4)).toEqual(['end b1_gain', 'released b1_gain'])
  expect(held('b1_gain')).toBe(false)
})

it('a complete change to a parameter an open gesture holds ends it there first', () => {
  const wheel = openGesture()
  within(wheel, () => phaseOf('comp_ratio'))
  expect(phaseOf('comp_ratio')).toBe('complete')
  expect(log).toEqual(['end comp_ratio', 'released comp_ratio'])
  expect(held('comp_ratio')).toBe(false)
  closeGesture(wheel)
  expect(log).toEqual(['end comp_ratio', 'released comp_ratio', 'released '])
})

it('a change to a parameter no other gesture holds ends nothing', () => {
  const drag = openGesture()
  const other = openGesture()
  within(other, () => phaseOf('comp_attack'))
  expect(within(drag, () => phaseOf('comp_ratio'))).toBe('begin')
  expect(phaseOf('comp_release')).toBe('complete')
  expect(log).toEqual([])
})

// A graph (Eq.tsx's curve, Comp.tsx's Threshold line): the wheel's gesture is not the drag's.
it("a graph's wheel step while dragging leaves the drag open when the wheel's 250 ms pass", () => {
  vi.useFakeTimers()
  const g = graphGestures()
  g.press()
  expect(g.dragged(() => phaseOf('b1_freq'))).toBe('begin')
  expect(g.stepped(() => phaseOf('b1_q'))).toBe('begin')
  expect(g.stepped(() => phaseOf('b1_q'))).toBe('move')
  vi.advanceTimersByTime(IDLE_MS + 1)
  expect(log).toEqual(['end b1_q', 'released b1_q'])
  expect(g.dragged(() => phaseOf('b1_freq'))).toBe('move')
  g.release()
  expect(log.slice(2)).toEqual(['end b1_freq', 'released b1_freq'])
  expect(g.dragged(() => phaseOf('b1_freq'))).toBe('complete') // no drag open
})

it("a graph's press while the wheel's gesture waits leaves that to its own end", () => {
  vi.useFakeTimers()
  const g = graphGestures()
  g.stepped(() => phaseOf('b2_q'))
  g.press()
  expect(g.dragged(() => phaseOf('b2_gain'))).toBe('begin')
  expect(log).toEqual([])
  vi.advanceTimersByTime(IDLE_MS + 1)
  expect(log).toEqual(['end b2_q', 'released b2_q'])
  expect(held('b2_gain')).toBe(true)
  // a second press ends the first drag (a lost release) before it opens its own
  g.press()
  expect(log.slice(2)).toEqual(['end b2_gain', 'released b2_gain'])
  g.release()
})
