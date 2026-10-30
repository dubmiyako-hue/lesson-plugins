// PRAYROOM — framework-free components. Each factory mounts into `host` and returns a small controller.

export interface ContinuousOptions {
  label?: string;
  min: number;
  max: number;
  value?: number;
  /** Value restored by double-click, Alt+click or Delete. Defaults to `value`. */
  default?: number;
  /** 'log' for frequency/time (requires min > 0). */
  scale?: 'linear' | 'log';
  unit?: string;
  decimals?: number;
  /** Prefix positive values with "+". */
  signed?: boolean;
  step?: number;
  format?: (v: number) => { text: string; unit?: string };
  disabled?: boolean;
  onChange?: (v: number) => void;
  /** Wire to JUCE beginChangeGesture / endChangeGesture. */
  onGestureStart?: () => void;
  onGestureEnd?: () => void;
}
export interface ContinuousControl {
  el: HTMLElement;
  value: number;
  setValue(v: number, silent?: boolean): void;
  destroy(): void;
}

export interface KnobProps extends ContinuousOptions {
  /** Scale diameter: lg 112 (numbers printed), md 80, sm 56, xs 40 px (the sm drawing scaled down). */
  size?: 'lg' | 'md' | 'sm' | 'xs';
  /** Head colour = role. Default 'ink'. (`cap` is accepted as an older alias.) */
  head?: 'ink' | 'paper' | 'indigo' | 'ochre';
  /** lg only: text printed round the scale, spread evenly from end to end. Default 0..10; false hides it. */
  scaleLabels?: string[] | false;
  /** Prints a red centre mark at the top of the scale. */
  bipolar?: boolean;
  bipolarCenter?: number;
}
export declare function Knob(host: HTMLElement, props: KnobProps): ContinuousControl;

export interface DialProps extends ContinuousOptions {
  /** Diameter in px. Default 220. One per plugin. */
  size?: number;
  /** The 11 numbers printed on the disc. Default: the range split in 10, formatted. */
  scaleLabels?: string[];
  /** Red band printed on the disc, as [from, to] in 0..1. */
  zone?: [number, number];
}
export declare function Dial(host: HTMLElement, props: DialProps): ContinuousControl;

export interface DrumProps extends ContinuousOptions {
  /** Ticks printed on the drum. Numbers every 5, bold every 10. Default 100. */
  steps?: number;
  width?: number;
  height?: number;
}
export declare function Drum(host: HTMLElement, props: DrumProps): ContinuousControl;

export interface TapeProps extends ContinuousOptions {
  /** Ticks printed on the tape. Numbers every 10. Default 100. */
  steps?: number;
  /** Window width in px. Default 300. */
  width?: number;
  /** Px between ticks. Default 9. */
  px?: number;
  zone?: [number, number];
}
export declare function Tape(host: HTMLElement, props: TapeProps): ContinuousControl;

export interface EncoderProps extends ContinuousOptions {
  /** lg 160, md 120, sm 88 px. */
  size?: 'lg' | 'md' | 'sm';
  bipolar?: boolean;
  bipolarCenter?: number;
}
export declare function Encoder(host: HTMLElement, props: EncoderProps): ContinuousControl;

export interface FaderProps extends ContinuousOptions {
  /** Track length in px. Default 120. */
  height?: number;
  /** 'horizontal': laid on its side for a track's volume in a timeline head, as wide as its
   *  container, with the value at its right; the label is spoken only */
  orientation?: 'vertical' | 'horizontal';
  /** horizontal: a line across the groove at this value (0 dB) */
  mark?: number;
}
export declare function Fader(host: HTMLElement, props: FaderProps): ContinuousControl;
/** a mixing app's track colour: hue 70 to 330 in equal steps for n tracks (朱 left out), L 0.58, C 0.1 */
export declare function trackColor(index: number, count: number): string;

export type GlyphKind = 'hp' | 'ls' | 'bell' | 'hs' | 'lp';

export interface ToggleProps {
  /** the text on the key; may be empty when a glyph says what it is */
  label: string;
  /** the spoken name, when the label alone does not say it (an empty label, a glyph) */
  name?: string;
  /** the shape of a filter, drawn before the label */
  glyph?: GlyphKind;
  /** a colour of its own for the LED, the rim and the face, in place of 朱: only where the
   *  colour itself means something (an EQ band's frequency colour) */
  color?: string;
  pressed?: boolean;
  disabled?: boolean;
  onChange?: (pressed: boolean) => void;
}
export declare function Toggle(host: HTMLElement, props: ToggleProps): { el: HTMLButtonElement; pressed: boolean; /** relight in another colour, as `color` does */ setColor(color: string): void; destroy(): void };

export type IconName = 'play' | 'stop' | 'expand' | 'close';

export interface KeyProps {
  /** the text on the key; leave it out for a key that is only an icon */
  label?: string;
  icon?: IconName;
  /** the spoken name; needed when the key is only an icon */
  name?: string;
  /** the dialog's main action, in bold */
  primary?: boolean;
  /** a state the caller holds (mute, solo, playing); leave it out for a plain action */
  pressed?: boolean;
  disabled?: boolean;
  onClick?: () => void;
}
export declare function Key(host: HTMLElement, props: KeyProps): { el: HTMLButtonElement; pressed: boolean | undefined; disabled: boolean; setIcon(icon: IconName | null): void; destroy(): void };

export declare function Badge(host: HTMLElement, props: { label: string; hidden?: boolean }): { el: HTMLSpanElement; hidden: boolean; destroy(): void };

/** what a knob's value is set for, framed under the knob; `off` keeps its room */
export declare function Tag(host: HTMLElement, props: { label: string; off?: boolean }): { el: HTMLSpanElement; label: string; off: boolean; destroy(): void };
/** a bracket under neighbouring knobs' tags; put the tag for them together in `el` */
export declare function Bracket(host: HTMLElement, props?: { off?: boolean }): { el: HTMLDivElement; off: boolean; destroy(): void };

export interface DeviceProps {
  /** the device's name, beside its power switch; the spoken name of the switch is `<name> Bypass` */
  name: string;
  /** on = the device runs; off = bypassed. Default true */
  on?: boolean;
  onPower?: (on: boolean) => void;
  /** opened large over a Scrim: the key at the right closes it */
  big?: boolean;
  /** the key at the right opens the device large; leave out both for a device that does not open */
  onExpand?: () => void;
  onClose?: () => void;
}
export declare function Device(host: HTMLElement, props: DeviceProps): { el: HTMLElement; head: HTMLElement; body: HTMLElement; on: boolean; destroy(): void };

export declare function Scrim(host: HTMLElement | null, props: { onClose?: () => void }): { el: HTMLElement; destroy(): void };

export interface LampProps {
  /** the spoken name; the lamp draws none, so put the name above it as text */
  label: string;
  pressed?: boolean;
  /** md (lamp-md) beside knob-sm knobs; lg (lamp-lg) beside knob-md knobs. Default md */
  size?: "md" | "lg";
  disabled?: boolean;
  onChange?: (pressed: boolean) => void;
}
export declare function Lamp(host: HTMLElement, props: LampProps): { el: HTMLButtonElement; pressed: boolean; destroy(): void };

export interface SwitchProps {
  /** the spoken name; the switch draws none, so put the name beside it as text */
  label: string;
  /** on = the device runs; off = bypassed. Default true */
  on?: boolean;
  disabled?: boolean;
  onChange?: (on: boolean) => void;
}
export declare function Switch(host: HTMLElement, props: SwitchProps): { el: HTMLButtonElement; on: boolean; destroy(): void };

export interface HintContent {
  /** the control's name, as its label reads */
  title: string;
  /** a few lines on what it does */
  text: string;
  /** a figure that goes with it (a range, a unit), set in the value face */
  aside?: string;
}
export interface HintPlace {
  /** the device the hint floats beside: an element or its rect */
  beside: Element | DOMRect;
  /** the height to centre on, in viewport pixels: the control's middle. Default the device's middle */
  y?: number;
  /** inside the device's left edge, for a device with no room to its left (a plugin window, a device opened large) */
  inside?: boolean;
}
/** one per page; position: fixed, so any host will do */
export declare function Hint(host: HTMLElement, props?: {}): { el: HTMLDivElement; show(content: HintContent, place: HintPlace): void; hide(): void; readonly shown: boolean; destroy(): void };

export type SegmentOption = string | number | {
  value: string | number;
  /** the text on the option; may be left out when a glyph says it */
  label?: string;
  /** the spoken name, when the label alone does not say it (a glyph, a number) */
  name?: string;
  /** the shape of a filter beside the label */
  glyph?: GlyphKind;
  /** an LED at the left, in this colour: what the option stands for is on or off (`lit`) */
  led?: string;
  lit?: boolean;
};
export interface SegmentedProps {
  label?: string;
  /** 5 or fewer: segments. 6 or more: a select, unless mode is 'segments'. */
  options: SegmentOption[];
  value?: string | number;
  mode?: 'segments' | 'select';
  /** across the whole width, every option the same width */
  fill?: boolean;
  onChange?: (value: string | number) => void;
}
export declare function Segmented(host: HTMLElement, props: SegmentedProps): { el: HTMLElement; value: string | number; setLed(index: number, color: string | null, lit: boolean): void };

/** a step of a VSlide or a Stepper: the value, the name in the window, and for a Stepper the full name its list shows */
export interface StepOption { value: number; label: string; full?: string }
export interface StepControl {
  el: HTMLElement;
  value: number;
  setValue(v: number, silent?: boolean): void;
  destroy(): void;
}

export interface VSlideProps {
  label?: string;
  /** 2 to 4 steps, in any order: the largest value sits on top */
  options: StepOption[];
  value?: number;
  disabled?: boolean;
  onChange?: (value: number) => void;
}
/** a vertical slide switch for 2 to 4 steps in a row of knobs; the whole body is the drag surface */
export declare function VSlide(host: HTMLElement, props: VSlideProps): StepControl;

export interface StepperProps {
  label?: string;
  /** in order: the one before the current sits above it, the one after below, as in the list */
  options: StepOption[];
  value?: number;
  disabled?: boolean;
  onChange?: (value: number) => void;
}
/** a chooser one knob column wide, for any number of options: a click opens its list; a drag, the wheel and the arrow keys step */
export declare function Stepper(host: HTMLElement, props: StepperProps): StepControl & { open(): void; close(): void; readonly isOpen: boolean };

export interface OverviewTab {
  value: string | number;
  /** the section's name, set in the section style (upper case) */
  label: string;
  /** px: a tab whose body stays empty keeps this width. Default one knob-sm column (72) */
  width?: number;
}
export interface OverviewTabsProps {
  /** the spoken name of the strip */
  label?: string;
  tabs: OverviewTab[];
  value?: string | number;
  onChange?: (value: string | number) => void;
}
/** the sections' names over their main controls; a press anywhere in a section shows it in the panel below */
export declare function OverviewTabs(host: HTMLElement, props: OverviewTabsProps): {
  el: HTMLElement; value: string | number; setValue(v: string | number, silent?: boolean): void;
  /** the column under a tab's name, for the section's main controls */
  body(value: string | number): HTMLElement; destroy(): void;
};

export interface MeterProps {
  label?: string;
  mode?: 'level' | 'gr';
  channels?: number;
  height?: number;
  /** level mode: bottom of the scale in dBFS. Default -48. */
  min?: number;
  /** level mode: start of the hot zone in dBFS. Default -6. */
  hot?: number;
  /** gr mode: full-scale reduction in dB. Default 24. */
  grRange?: number;
  ticks?: number[];
}
export declare function Meter(host: HTMLElement, props: MeterProps): {
  el: HTMLElement;
  /** dBFS per channel (level) or reduction in dB (gr). Call every frame. */
  set(values: number | number[]): void;
  resetClip(): void;
  destroy(): void;
};

export interface EqBand { type: 'bell' | 'lowshelf' | 'highshelf'; freq: number; gain: number; q?: number; bypass?: boolean }
export interface GraphProps {
  label?: string;
  width?: number | string;
  height?: number;
  /** ± dB shown. Default 18. */
  range?: number;
  fMin?: number;
  fMax?: number;
  sampleRate?: number;
  bands: EqBand[];
  editable?: boolean;
  /** the word rows above and below the plot: what each range sounds like (top), and what too much of it does (bottom) */
  words?: { top: GraphWord[]; bottom: GraphWord[] };
  onChange?: (bands: EqBand[]) => void;
}
/** a word of the word rows: where it sits, the word, and the range it names (Hz) */
export type GraphWord = [at: number, word: string, lo: number, hi: number];
/** the colour of each frequency, its hue stepped evenly from word to word */
export declare function freqColors(words: GraphWord[], fMin?: number): {
  hue(f: number): number; ink(f: number): string; mark(f: number): string; tint(f: number): string; wash(f: number, alpha: number): string;
};
export declare function Graph(host: HTMLElement, props: GraphProps): { el: HTMLElement; bands: EqBand[]; setBands(b: EqBand[]): void; redraw(): void; destroy(): void };

/** a display over the knobs it shows: as wide as the place it is given, knob-sm + space-1 + 16 px tall; its handles drag */
export interface DisplayProps<K extends string> {
  label?: string;
  /** a handle moved: the key and its new value (a position 0..1 on the knob's scale, unless the key says otherwise) */
  onChange?: (key: K, value: number) => void;
  /** Wire to JUCE beginChangeGesture / endChangeGesture, for the parameter the handle stands for. */
  onGestureStart?: (key: K) => void;
  onGestureEnd?: (key: K) => void;
  /** the pointer came onto a handle (for the footer's readout) */
  onHover?: (key: K) => void;
}
export interface DisplayControl<V> {
  el: HTMLElement;
  /** redraw with these values (any subset) */
  set(values: Partial<V>): void;
  values(): V;
  destroy(): void;
}

export interface EnvelopeValues {
  /** attack, decay and release: the knobs' positions on their scales, 0..1 */
  a: number; d: number; r: number;
  /** the sustain level's position on its scale, 0..1 */
  s: number;
  /** velocity sensitivity 0..1: the shape at the softest velocity is drawn faint */
  vel: number;
}
export type EnvelopeKey = 'a' | 'd' | 's' | 'r';
export interface EnvelopeProps extends DisplayProps<EnvelopeKey>, EnvelopeValues {}
/** an ADSR shape with handles: the attack's peak (a), the decay's end (d and s), the sustain's end (s), the release's end (r) */
export declare function Envelope(host: HTMLElement, props: EnvelopeProps): DisplayControl<EnvelopeValues>;

export interface PitchPathValues {
  /** the glide time's position on its scale, 0..1; 0 when glide is off */
  glide: number;
  /** the pitch envelope's amount in semitones, 0..amtMax */
  amt: number;
  /** the pitch envelope's time's position on its scale, 0..1 */
  time: number;
  /** cents */
  tune: number;
}
export type PitchPathKey = 'glide' | 'amt' | 'time';
export interface PitchPathProps extends DisplayProps<PitchPathKey>, PitchPathValues {
  /** the amount handle's ceiling in semitones. Default 48 */
  amtMax?: number;
}
/** one note's pitch over time, with handles: the start of the note (amt, in whole semitones), the glide's end (glide), the envelope's end (time) */
export declare function PitchPath(host: HTMLElement, props: PitchPathProps): DisplayControl<PitchPathValues>;

export interface PanelProps { title?: string; grow?: number; /** Colour zone: warm = input/drive side, cool = EQ/output side. */ tone?: 'warm' | 'cool' }
export declare function Panel(host: HTMLElement, props?: PanelProps): { el: HTMLElement; body: HTMLElement };

export interface PluginWindowProps {
  title: string;
  width?: number;
  height?: number;
  presets?: SegmentOption[];
  version?: string;
  hint?: string;
  onPreset?: (v: string | number) => void;
  onAB?: (v: string | number) => void;
  /** true when bypassed: the header's power switch turned off */
  onBypass?: (bypassed: boolean) => void;
}
export declare function PluginWindow(host: HTMLElement, props: PluginWindowProps): {
  el: HTMLElement; body: HTMLElement;
  preset: { value: string | number }; ab: { value: string | number }; bypass: { pressed: boolean };
  setReadout(text: string): void; destroy(): void;
};

export interface ViewerProps {
  width?: number | string;
  height?: number | string;
  /** Width / height of the video. Default 16/9. */
  aspect?: number;
  /** Strings shown left-to-right in the band under the frame (timecode, resolution). */
  info?: string[];
}
export declare function Viewer(host: HTMLElement, props: ViewerProps): {
  el: HTMLElement;
  /** Put a <canvas>, <video> or <img> here. */
  frame: HTMLElement;
  setInfo(items: string[]): void;
  setAspect(aspect: number): void;
  destroy(): void;
};

export interface TimelineClip { start: number; end: number; label: string; opacity?: number; lane?: 'A' | 'B' }
export interface TimelineTrack { name: string; tag?: 'A' | 'B'; clips?: TimelineClip[] }
export interface TimelineProps {
  /** Number of bars shown. */
  bars?: number;
  beatsPerBar?: number;
  /** Width of one bar in px. Default 64. */
  pxPerBar?: number;
  tracks: TimelineTrack[];
  audio?: { name?: string; peaks?: number[] } | null;
  /** Playhead position in bars, 1-based. */
  position?: number;
  /** Text in the top-left corner. Default "BPM". */
  label?: string;
  onSeek?: (position: number) => void;
  onSelect?: (trackIndex: number, clip: TimelineClip) => void;
}
export declare function Timeline(host: HTMLElement, props: TimelineProps): { el: HTMLElement; position: number; setPosition(p: number): void; destroy(): void };

/** a track of the mix variant */
export interface MixTrack {
  name: string;
  /** 0..1 amplitudes, thinned to the region's width */
  peaks?: number[];
  /** dB. Default 0 */
  volume?: number;
  mute?: boolean;
  solo?: boolean;
  /** Default trackColor(index, count) */
  color?: string;
}
export interface MixTimelineProps {
  variant: 'mix';
  /** Number of bars shown, fitted to the width. Default 16 */
  bars?: number;
  /** The number written on the first bar. Default 1 */
  firstBar?: number;
  beatsPerBar?: number;
  tracks: MixTrack[];
  /** The selected track's index. Default 0 */
  selected?: number;
  /** Playhead position in bars, 1-based */
  position?: number;
  /** In bars, 1-based; null for none */
  loop?: { start: number; end: number } | null;
  /** The faders' range in dB. Default -24..12, step 0.5 */
  volume?: { min?: number; max?: number; step?: number };
  onSeek?: (position: number) => void;
  onSelectTrack?: (index: number) => void;
  onLoop?: (loop: { start: number; end: number }) => void;
  onMute?: (index: number, mute: boolean) => void;
  onSolo?: (index: number, solo: boolean) => void;
  onVolume?: (index: number, db: number) => void;
}
export declare function Timeline(host: HTMLElement, props: MixTimelineProps): {
  el: HTMLElement; position: number; setPosition(p: number): void;
  selected: number; select(index: number): void; setLoop(loop: { start: number; end: number } | null): void; destroy(): void;
};

declare global {
  interface Window {
    PRAYROOM: { Knob: typeof Knob; Dial: typeof Dial; Drum: typeof Drum; Tape: typeof Tape; Encoder: typeof Encoder; Fader: typeof Fader; trackColor: typeof trackColor; Toggle: typeof Toggle; Key: typeof Key; Badge: typeof Badge; Tag: typeof Tag; Bracket: typeof Bracket; Device: typeof Device; Scrim: typeof Scrim; Lamp: typeof Lamp; Switch: typeof Switch; Hint: typeof Hint; Segmented: typeof Segmented; VSlide: typeof VSlide; Stepper: typeof Stepper; OverviewTabs: typeof OverviewTabs; Meter: typeof Meter; Graph: typeof Graph; freqColors: typeof freqColors; Envelope: typeof Envelope; PitchPath: typeof PitchPath; Panel: typeof Panel; PluginWindow: typeof PluginWindow; Viewer: typeof Viewer; Timeline: typeof Timeline };
  }
}
