// A parameter is in one gesture at a time (src/gestures.ts phaseOf): a change to one that another open gesture holds
// ends it there first, so the host gets an end before the new begin or complete.
import { beforeEach, expect, it } from 'vitest'
import { closeAll, closeGesture, connectGestures, held, openGesture, phaseOf, within } from '../src/gestures'

const log: string[] = []

beforeEach(() => {
  closeAll()
  connectGestures(
    (id) => log.push(`end ${id}`),
    (ids) => log.push(`released ${ids.join(',')}`),
  )
  log.length = 0
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
