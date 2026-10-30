import { useEffect, useRef } from 'preact/hooks'
import { fromNorm, showValue, toNorm, type Param } from '../params'
import type { BandKind } from '../chain'
import { idOf } from '../app'
import { closeGesture, closeWhenIdle, openGesture, within, type Gesture } from '../gestures'
import { host as plugin } from '../host'
import { hint, rehint } from './Hint'
import { useFrame } from './hooks'

// Preact wrappers around the PRAYROOM components (prayroom/components/bundle.js).
// Each one mounts the component once, and remounts it when an option it reads only
// at creation changes; values that change while it lives are pushed in silently.

// '-18.0 dB' → number and unit, which PRAYROOM draws apart
function split(s: string) {
  const m = s.match(/^(.*\d)\s(\S+)$/)
  return m ? { text: m[1], unit: m[2] } : { text: s }
}

type KnobProps = {
  param: Param
  value: number
  def: number // double-click, Alt+click or Delete returns here
  disabled?: boolean // shown, but this band type has no such control
  size?: 'sm' | 'md'
  head?: 'ink' | 'paper'
  more?: string // the Hint reads it on when the keyboard comes to the knob
  onChange: (v: number) => void
  onReset: () => void
}

// The knob runs on 0..1 along our own curve (toNorm), so log ranges, steps and the
// 「最速」 position behave as before. Its hint is its parameter's name and tip.
export function Knob({ param, value, def, disabled, size = 'sm', head = 'ink', more, onChange, onReset }: KnobProps) {
  const host = useRef<HTMLDivElement>(null)
  const ctl = useRef<ReturnType<typeof window.PRAYROOM.Knob> | null>(null)
  const cb = useRef({ onChange, onReset })
  cb.current = { onChange, onReset }
  useEffect(() => {
    const d = toNorm(param, def)
    // The host's gesture for what moves the knob (gestures.ts): a drag from PRAYROOM's own start and end; the wheel and
    // the arrow keys until 250 ms without a step, told apart by the event that is being handled when the change comes;
    // a reset (double-click, Alt+click, Delete) is complete.
    let drag: Gesture | null = null
    let steps: Gesture | null = null
    let source: 'steps' | 'reset' | null = null
    const el = host.current!
    const mark = (e: Event) => {
      source = e.type === 'keydown' && ['Delete', 'Backspace'].includes((e as KeyboardEvent).key) ? 'reset' : 'steps'
    }
    const unmark = () => (source = null)
    for (const t of ['wheel', 'keydown']) {
      el.addEventListener(t, mark, true)
      el.addEventListener(t, unmark)
    }
    const change = (n: number) => (n === d ? cb.current.onReset() : cb.current.onChange(fromNorm(param, n)))
    const k = window.PRAYROOM.Knob(el, {
      label: param.label,
      min: 0,
      max: 1,
      value: toNorm(param, value),
      default: d,
      size,
      head,
      bipolar: param.min < 0 && param.max > 0,
      disabled,
      format: (n) => (disabled ? { text: '—' } : split(showValue(param, fromNorm(param, n)))),
      onGestureStart: () => (drag = openGesture()),
      onGestureEnd: () => {
        closeGesture(drag)
        drag = null
      },
      onChange: (n) => {
        if (drag) return within(drag, () => change(n))
        if (source !== 'steps') return change(n)
        if (!steps || steps.closed) steps = openGesture()
        closeWhenIdle(steps)
        within(steps, () => change(n))
      },
    })
    ctl.current = k
    const typing = typeValue(el, param, (v) => cb.current.onChange(v))
    return () => {
      for (const t of ['wheel', 'keydown']) {
        el.removeEventListener(t, mark, true)
        el.removeEventListener(t, unmark)
      }
      typing()
      closeGesture(drag)
      closeGesture(steps)
      k.destroy()
    }
  }, [param, def, disabled, size, head])
  useEffect(() => ctl.current?.setValue(toNorm(param, value), true), [value])
  useEffect(() => rehint(host.current!), [more])
  return <div class="pr-host" ref={host} {...hint(param.label, param.tip, undefined, more)} />
}

// Typing a value (PRAYROOM behavior.md, 数値の入力): a click on the value under the knob makes it a field of the same size;
// Enter or a click elsewhere takes the value, Esc leaves it. The text is read by the plug-in (getValueForText, the same
// reading as the host's), so the field accepts what the host does; text that cannot be read, or that gives the value the
// knob has, changes nothing. The field has the keyboard only while it is open. Returns what removes the listener.
function typeValue(el: HTMLElement, param: Param, set: (v: number) => void) {
  const click = (e: MouseEvent) => {
    const shown = (e.target as Element).closest<HTMLElement>('.pr-ctl__value')
    const id = idOf(param)
    if (!shown || !id || shown.closest('.pr-ctl--disabled') || el.querySelector('.value-input')) return
    const field = document.createElement('input')
    field.className = 'value-input value'
    field.value = Array.from(shown.childNodes, (n) => n.textContent).join(' ') // the number and its unit, drawn apart
    field.style.width = `${shown.offsetWidth}px`
    field.setAttribute('aria-label', param.label)
    shown.style.display = 'none'
    shown.after(field)
    field.focus()
    field.select()
    let done = false
    const finish = async (take: boolean) => {
      if (done) return
      done = true
      const text = field.value
      field.remove()
      shown.style.display = ''
      if (!take) return
      const v = await plugin().parseValue(id, text)
      if (v !== null && Number.isFinite(v)) set(v)
    }
    field.addEventListener('keydown', (k) => {
      k.stopPropagation()
      if (k.key === 'Enter') void finish(true)
      else if (k.key === 'Escape') void finish(false)
    })
    field.addEventListener('blur', () => void finish(true))
  }
  el.addEventListener('click', click)
  return () => el.removeEventListener('click', click)
}

// `color` lights the LED, the rim and the face in place of 朱, and `glyph` puts a band's
// shape before the label; `name` is the spoken name, when the label alone does not say
// what it switches (the EQ band toggle)
type ToggleProps = { label: string; pressed: boolean; color?: string; glyph?: BandKind; name?: string; onChange: () => void }

export function Toggle({ label, pressed, color, glyph, name, onChange }: ToggleProps) {
  const host = useRef<HTMLSpanElement>(null)
  const ctl = useRef<ReturnType<typeof window.PRAYROOM.Toggle> | null>(null)
  const cb = useRef(onChange)
  cb.current = onChange
  useEffect(() => {
    const t = window.PRAYROOM.Toggle(host.current!, { label, name, glyph, color, pressed, onChange: () => cb.current() })
    ctl.current = t
    return () => t.destroy()
  }, [label, glyph, name])
  useEffect(() => {
    if (ctl.current) ctl.current.pressed = pressed
  }, [pressed])
  useEffect(() => {
    if (ctl.current && color) ctl.current.setColor(color)
  }, [color])
  return <span class="pr-host" ref={host} />
}

// PRAYROOM's square key lit from inside, for a setting in a row of knobs. It draws no
// name; `label` is the spoken one
export function Lamp({ label, pressed, big, onChange }: { label: string; pressed: boolean; big?: boolean; onChange: () => void }) {
  const host = useRef<HTMLSpanElement>(null)
  const ctl = useRef<ReturnType<typeof window.PRAYROOM.Lamp> | null>(null)
  const cb = useRef(onChange)
  cb.current = onChange
  useEffect(() => {
    const t = window.PRAYROOM.Lamp(host.current!, { label, pressed, size: big ? 'lg' : 'md', onChange: () => cb.current() })
    ctl.current = t
    return () => t.destroy()
  }, [label, big])
  useEffect(() => {
    if (ctl.current) ctl.current.pressed = pressed
  }, [pressed])
  return <span class="pr-host" ref={host} />
}

// PRAYROOM's power switch: on = the device runs, off = bypassed. It draws no name;
// `label` is the spoken one; `tip` what the Hint says of it
export function Switch({ label, on, tip, onChange }: { label: string; on: boolean; tip: string; onChange: () => void }) {
  const host = useRef<HTMLSpanElement>(null)
  const ctl = useRef<ReturnType<typeof window.PRAYROOM.Switch> | null>(null)
  const cb = useRef(onChange)
  cb.current = onChange
  useEffect(() => {
    const t = window.PRAYROOM.Switch(host.current!, { label, on, onChange: () => cb.current() })
    ctl.current = t
    return () => t.destroy()
  }, [label])
  useEffect(() => {
    if (ctl.current) ctl.current.on = on
  }, [on])
  return <span class="pr-host" ref={host} {...hint('Bypass', tip)} />
}

type SegProps = { label: string; unit?: string; options: number[]; value: number | null; disabled?: boolean; onChange: (v: number) => void }

// `value: null` shows no choice, for a band that has no such control
export function Segmented({ label, unit, options, value, disabled, onChange }: SegProps) {
  const host = useRef<HTMLDivElement>(null)
  const ctl = useRef<ReturnType<typeof window.PRAYROOM.Segmented> | null>(null)
  const cb = useRef(onChange)
  cb.current = onChange
  useEffect(() => {
    const sg = window.PRAYROOM.Segmented(host.current!, { label, options, value: value ?? undefined, onChange: (v) => cb.current(Number(v)) })
    ctl.current = sg
    if (value === null) sg.value = null as unknown as number
    // the unit is printed once, beside the name, rather than on every option
    if (unit) sg.el.firstElementChild?.append(Object.assign(document.createElement('span'), { className: 'unit', textContent: unit }))
    return () => sg.el.remove()
  }, [label, unit, options])
  useEffect(() => {
    if (ctl.current) ctl.current.value = value as number
  }, [value])
  // dimmed like a disabled knob; the class goes on the component's own column, as the host has no box
  useEffect(() => {
    ctl.current?.el.classList.toggle('pr-ctl--disabled', !!disabled)
  }, [disabled, label, options])
  return <div class="pr-host" ref={host} />
}

// PRAYROOM's gain reduction meter, as tall as its box; `read` gives the reduction in dB each frame
export function GrMeter({ read, tip }: { read: () => number; tip: string }) {
  const host = useRef<HTMLDivElement>(null)
  const ctl = useRef<ReturnType<typeof window.PRAYROOM.Meter> | null>(null)
  useEffect(() => {
    const m = window.PRAYROOM.Meter(host.current!, { label: 'GR', mode: 'gr', channels: 1, grRange: 24 })
    m.el.style.height = '100%'
    ctl.current = m
    return () => m.destroy()
  }, [])
  useFrame(() => ctl.current?.set(read()))
  return <div class="grmeter" ref={host} {...hint('GR', tip)} />
}
