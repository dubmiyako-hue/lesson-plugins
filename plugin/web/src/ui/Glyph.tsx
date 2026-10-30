import type { BandKind } from '../chain'

// The shape of each band's filter, drawn in the text colour: it names the kind of band
// on the band selector and the band switch, in place of a sentence.
export const GLYPHS: Record<BandKind, string> = {
  hp: 'M1 11 C5 11 6 3 9 3 L17 3',
  ls: 'M1 3 L5 3 C8 3 9 8 12 8 L17 8',
  bell: 'M1 9 L5 9 C7 9 8 2 9 2 C10 2 11 9 13 9 L17 9',
  hs: 'M1 8 L6 8 C9 8 10 3 13 3 L17 3',
  lp: 'M1 3 L9 3 C12 3 13 11 17 11',
}

export const KIND_NAMES: Record<BandKind, string> = { hp: 'ハイパス', ls: 'ローシェルフ', bell: 'ベル', hs: 'ハイシェルフ', lp: 'ローパス' }

export function Glyph({ kind }: { kind: BandKind }) {
  return (
    <svg class="pr-glyph" viewBox="0 0 18 12" aria-hidden="true">
      <path d={GLYPHS[kind]} />
    </svg>
  )
}
