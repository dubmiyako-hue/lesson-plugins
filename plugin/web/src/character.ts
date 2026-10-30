import type { Settings } from './chain'
import { RATIO_INF } from './dsp'

// The words under the compressor's knobs: for Ratio, Attack and Release, what their value
// does to the sound, from the purposes mixing guides set it for ("to keep the punch, slow
// the attack"); under Attack and Release together, where that combination is recommended.
// The tip says why the value does it. Sources and the splits are in DECISIONS.md, 2026-10-30.

export type Word = { word: string; tip: string }
// [from, word, tip], from the lowest value up; a value on a split belongs to the upper one
type Range = [number, string, string]

// Ratio splits are the Waves "7 Tips" table (light up to 2:1, medium to 8:1, heavy beyond).
// What is over the Threshold comes out over by 1 / ratio of it.
export const RATIO_WORDS: Range[] = [
  [1, '軽く抑える', 'Ratio が 2:1 より小さいので、Threshold を超えた分を半分も縮めない。音量の差が少し縮まるだけで、かけたことが目立ちにくい。'],
  [2, '音量を揃える', 'Ratio が中くらいなので、Threshold を超えた分を、半分から 8 分の 1 までに縮める（4:1 なら 4 分の 1 になる）。大きいところと小さいところの差が縮まって、音量がそろいやすい。'],
  [8, '強く抑える', 'Ratio が大きいので、Threshold を超えた分を 8 分の 1 以下に縮める。超えたところがほとんど同じ大きさになり、強弱のない音になる。かけすぎると、叩いた瞬間や歌い出しの強さも消えやすい。'],
  [RATIO_INF, 'Limiter', 'Ratio が ∞:1 なので、Threshold を超えた分は出てこない。出る音は Threshold より大きくならない。'],
]
// Attack and Release splits are provisional, from CompressorCore run over the demo stems
// (Ratio 4:1, about 7.5 dB wanted at the peaks). Below 1 ms the head is held as hard as
// the rest; from 30 ms the whole is held more lightly. Below 50 ms the gain comes back
// within a 16th note; from 300 ms it hardly lets go while the stem plays.
export const ATTACK_WORDS: Range[] = [
  [0, 'ピークを削る', 'Attack がとても速いので、音の出だしから抑える。瞬間的に大きい音（ピーク）が小さくなって、いちばん大きいところの大きさがそろいやすい。そのぶん、叩いた瞬間の音は弱く聞こえやすい。'],
  [1, '出だしを残す', 'Attack が少し遅れて効くので、音の出だしは抑えられずに通り、そのあとが抑えられる。叩いた瞬間が、そのあとより大きく聞こえる。Release が次の音までに戻る長さで、GR がはっきり出ているときに効く。'],
  [30, '強弱を残す', 'Attack が遅いので、音の出だしも、そのあとも少しだけ抑える。かけたことが分かりにくく、元の強弱が残りやすい。'],
]
export const RELEASE_WORDS: Range[] = [
  [0, '余韻を上げる', 'Release が速いので、抑えたあとすぐ戻る。音と音のあいだの小さいところ（余韻）が相対的に大きくなり、全体が大きく聞こえやすい。速すぎると、音量が細かく上下するのが聞こえる（ポンピング）ことがある。'],
  [50, '拍に合わせる', 'Release が中くらいなので、抑えたあと次の拍に向けて戻っていく。音量の上がり下がりが、曲の拍に合いやすい。合う長さは曲のテンポで変わる。'],
  [300, '揺れを抑える', 'Release が遅いので、抑えたまま、ゆっくり戻る。音量が細かく上下せず、変化がゆるやかに聞こえやすい。'],
]

// Where the Attack and Release words together are recommended, from the per-instrument
// settings in mixing guides and manuals: rows go by ATTACK_WORDS, columns by RELEASE_WORDS.
// null where no source names a use. A Release under 50 ms is past the end of the 1176, SSL
// and TK BC1-THD release knobs, so the guides only use it for distortion or in place of a
// limiter.
export const USES: ([string, string] | null)[][] = [
  [null, ['ボーカルにおすすめ', 'ボーカルに 1176 型のコンプをかけるときによく使われる組み合わせ（スネアにも）。出だしからすぐ抑えて一音ずつの大きさをそろえ、次の音までに戻す。子音がはっきり聞こえる声になりやすい。'], ['ボーカルにおすすめ', 'ボーカルで、シャウトのように声量の差が大きいところを抑えたいときや、Fairchild 型のコンプで少しだけかけるときに使われる組み合わせ。出だしからすぐ抑えて、ゆっくり戻す。']],
  [null, ['ドラムにおすすめ', 'キック・スネア・ドラムバスでよく使われる組み合わせ。叩いた瞬間は抑えずに通して目立たせ、次の音までに戻す。'], ['ボーカルにおすすめ', 'ボーカルでよく使われる組み合わせ（LA-2A などのオプト型もここに入る）。出だしを少し通して言葉を聞き取りやすくし、ゆっくり戻して音量の変化をゆるやかにする。']],
  [null, ['ミックス全体におすすめ', 'ミックス全体（2mix）にかけるときによく使われる組み合わせ。出だしは通して少しだけ抑え、拍に合わせて戻す。'], ['マスターにおすすめ', 'マスタリングでよく使われる組み合わせ。出だしは通して少しだけ抑え、ゆっくり戻して全体の音量の変化をゆるやかにする。']],
]

// the last range whose start the value has reached
const at = (ranges: Range[], v: number) => ranges.reduce((k, [from], j) => (v >= from ? j : k), 0)
const word = ([, w, tip]: Range): Word => ({ word: w, tip })

export type Words = { ratio: Word; attack: Word; release: Word; whole: Word | null }

// None at 1:1, where the compressor does nothing at any Threshold. At the Threshold's top
// (0 dB, where the page starts) the words still show, so they can be read before it is
// lowered.
export function characterWords(set: Pick<Settings, 'ratio' | 'attack' | 'release'>): Words | null {
  if (set.ratio <= 1) return null
  const ai = at(ATTACK_WORDS, set.attack)
  const ri = at(RELEASE_WORDS, set.release)
  const attack = word(ATTACK_WORDS[ai])
  const release = word(RELEASE_WORDS[ri])
  const use = USES[ai][ri]
  return {
    ratio: word(RATIO_WORDS[at(RATIO_WORDS, set.ratio)]),
    attack,
    release,
    whole: use && { word: use[0], tip: `Attack「${attack.word}」と Release「${release.word}」は、${use[1]}` },
  }
}
