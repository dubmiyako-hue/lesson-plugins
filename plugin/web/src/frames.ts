// Which animation frames the screen draws. The copied screens redraw their canvases every frame; with nothing moving
// that is a core's worth of GPU and renderer time in a window that is only open (measured 2026-11-01: 26 to 28 % of a
// core, no audio). A frame is drawn while audio plays, for a moment after anything may have changed (a parameter, the
// plug-in's data, the pointer, the keys, the size, the theme, a font), and once in a while as a safety net.
export const GRACE_MS = 600
export const SAFETY_MS = 500

export type FramePolicy = { playing: () => boolean; hidden: () => boolean }

export function createFramePolicy(policy: FramePolicy) {
  let lastChange = -Infinity
  let lastDraw = -Infinity
  let decidedAt = NaN
  let decision = false
  return {
    // Something may have changed on the screen: draw from now on for GRACE_MS.
    invalidate(now: number) {
      lastChange = now
    },
    // Whether to draw the frame at `now`. Every caller in one frame gets the same answer.
    shouldDraw(now: number): boolean {
      if (now === decidedAt) return decision
      decidedAt = now
      decision = !policy.hidden() && (policy.playing() || now - lastChange < GRACE_MS || now - lastDraw >= SAFETY_MS)
      if (decision) lastDraw = now
      return decision
    },
  }
}
