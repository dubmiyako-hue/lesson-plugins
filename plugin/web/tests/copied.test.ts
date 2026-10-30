// AC-16: the page's words are the web app's. The web app's tables, texts and characterWords are in
// tests/golden/ui.json (node tests/golden/gen-ui.mjs); this reads the plug-in's copies (src/COPIED.md) against it.
// The screens (src/ui/*.tsx) need a browser, so they are read as text and parsed, not imported. The reading of strings
// is the same as in gen-ui.mjs. Keep them so.
import { describe, expect, it } from 'vitest'
import ts from 'typescript'
import { characterWords, type Words } from '../src/character'
import { RATIO_INF } from '../src/dsp'
import * as copiedCharacter from '../src/character'
import uiRaw from '../../../tests/golden/ui.json?raw'
import charsRaw from '../fonts/chars.txt?raw'

type Table = unknown[]
type Pair = { word: string; tip: string }
type Golden = {
  webCommit: string
  tables: Record<string, Table>
  texts: Record<string, string[]>
  grid: { ratio: number[]; attack: number[]; release: number[]; words: Pair[]; combos: (number[] | null)[]; rows: string[] }
}
const golden: Golden = JSON.parse(uiRaw)

// the plug-in's sources by path under src/ ("ui/Eq.tsx")
const sources = Object.fromEntries(
  Object.entries(import.meta.glob('../src/**/*.{ts,tsx}', { query: '?raw', import: 'default', eager: true }) as Record<string, string>).map(
    ([path, text]) => [path.replace('../src/', ''), text],
  ),
)
const source = (file: string): string => {
  const text = sources[file]
  if (text === undefined) throw new Error(`plugin/web/src/${file} is missing`)
  return text
}
const parse = (file: string) => ts.createSourceFile(file, source(file), ts.ScriptTarget.ES2022, true, ts.ScriptKind.TSX)

const NON_ASCII = /[^\x00-\x7f]/
function strings(sf: ts.SourceFile): string[] {
  const out: string[] = []
  const visit = (n: ts.Node) => {
    let t: string | undefined
    if (ts.isStringLiteral(n) || ts.isNoSubstitutionTemplateLiteral(n)) t = n.text
    else if (ts.isTemplateHead(n) || ts.isTemplateMiddle(n) || ts.isTemplateTail(n)) t = n.text
    else if (ts.isJsxText(n)) t = n.text.replace(/\s+/g, ' ').trim()
    if (t !== undefined && NON_ASCII.test(t) && !out.includes(t)) out.push(t)
    ts.forEachChild(n, visit)
  }
  visit(sf)
  return out
}

function evaluate(n: ts.Node, env: Record<string, number>): unknown {
  if (ts.isArrayLiteralExpression(n)) return n.elements.map((e) => evaluate(e, env))
  if (ts.isStringLiteral(n) || ts.isNoSubstitutionTemplateLiteral(n)) return n.text
  if (ts.isNumericLiteral(n)) return Number(n.text)
  if (n.kind === ts.SyntaxKind.NullKeyword) return null
  if (ts.isIdentifier(n) && n.text in env) return env[n.text]
  if (ts.isPrefixUnaryExpression(n) && n.operator === ts.SyntaxKind.MinusToken) return -(evaluate(n.operand, env) as number)
  throw new Error(`cannot evaluate ${ts.SyntaxKind[n.kind]}: ${n.getText()}`)
}

function table(file: string, name: string): Table {
  let init: ts.Expression | undefined
  const visit = (n: ts.Node) => {
    if (ts.isVariableDeclaration(n) && ts.isIdentifier(n.name) && n.name.text === name && n.initializer) init = n.initializer
    ts.forEachChild(n, visit)
  }
  visit(parse(file))
  if (!init) throw new Error(`${name} is not in plugin/web/src/${file}`)
  return evaluate(init, { RATIO_INF }) as Table
}

// where each table is in the sources
const TABLE_FILES: Record<string, string> = {
  TOP_LABELS: 'ui/Eq.tsx',
  BOTTOM_LABELS: 'ui/Eq.tsx',
  RATIO_WORDS: 'character.ts',
  ATTACK_WORDS: 'character.ts',
  RELEASE_WORDS: 'character.ts',
  USES: 'character.ts',
}

// A text of the web app that a copy has intentionally dropped is listed here, per file, with the reason. None so far.
const EXPECTED_OMISSIONS: Record<string, string[]> = {}

describe('AC-16 (1): the word tables are the web app\'s', () => {
  it('the golden holds every table, and the web app\'s commit', () => {
    expect(Object.keys(golden.tables).sort()).toEqual(Object.keys(TABLE_FILES).sort())
    expect(golden.webCommit).toMatch(/^[0-9a-f]{7,}$/)
  })
  it.each(Object.keys(TABLE_FILES))('%s equals the web app\'s', (name) => {
    expect(table(TABLE_FILES[name]!, name)).toEqual(golden.tables[name])
  })
  it('the tables character.ts exports are the ones read from its source', () => {
    const exported: Record<string, unknown> = copiedCharacter
    for (const name of ['RATIO_WORDS', 'ATTACK_WORDS', 'RELEASE_WORDS', 'USES']) expect(exported[name]).toEqual(table('character.ts', name))
  })
})

describe('AC-16 (2): every Japanese text of the web app is in the copy', () => {
  it.each(Object.keys(golden.texts))('%s', (file) => {
    const have = new Set(strings(parse(file)))
    const omitted = new Set(EXPECTED_OMISSIONS[file] ?? [])
    const missing = golden.texts[file]!.filter((t) => !have.has(t) && !omitted.has(t))
    expect(missing).toEqual([])
  })
  it('an expected omission is a text the web app has, and the copy lacks', () => {
    for (const [file, list] of Object.entries(EXPECTED_OMISSIONS)) {
      const have = new Set(strings(parse(file)))
      for (const t of list) {
        expect(golden.texts[file]).toContain(t)
        expect(have.has(t)).toBe(false)
      }
    }
  })
  it('the golden has texts (the extraction found something)', () => {
    expect(Object.values(golden.texts).flat().length).toBeGreaterThan(50)
  })
})

describe('AC-16 (3): characterWords gives the web app\'s words over every word boundary', () => {
  const { ratio, attack, release, words, combos, rows } = golden.grid
  const expected = combos.map((c): Words | null =>
    c && {
      ratio: words[c[0]!]!,
      attack: words[c[1]!]!,
      release: words[c[2]!]!,
      whole: c[3]! < 0 ? null : words[c[3]!]!,
    },
  )
  const same = (a: Pair | null, b: Pair | null) => (a === null || b === null ? a === b : a.word === b.word && a.tip === b.tip)
  const sameWords = (a: Words | null, b: Words | null) =>
    a === null || b === null ? a === b : same(a.ratio, b.ratio) && same(a.attack, b.attack) && same(a.release, b.release) && same(a.whole, b.whole)

  it('the grid has the knob\'s legal values and each range start with the values beside it', () => {
    const starts = (name: string) => (golden.tables[name] as [number, ...unknown[]][]).map(([from]) => from)
    const check = (axis: number[], name: string) => {
      for (const b of starts(name)) {
        expect(axis).toContain(b)
        expect(axis.some((v) => v < b && v > b - Math.max(1e-3, Math.abs(b) * 1e-3))).toBe(true)
        expect(axis.some((v) => v > b && v < b + Math.max(1e-3, Math.abs(b) * 1e-3))).toBe(true)
      }
    }
    check(ratio, 'RATIO_WORDS')
    check(attack, 'ATTACK_WORDS')
    check(release, 'RELEASE_WORDS')
    expect(rows.length).toBe(ratio.length * attack.length)
  })

  it('every point of the grid gives the web app\'s words', () => {
    let points = 0
    const wrong: string[] = []
    let nWrong = 0
    rows.forEach((row, k) => {
      const rt = ratio[Math.floor(k / attack.length)]!
      const at = attack[k % attack.length]!
      let l = 0
      for (const run of row.split(',')) {
        const [c, n] = run.split(':').map(Number) as [number, number]
        for (let m = 0; m < n; m++, l++) {
          points++
          const got = characterWords({ ratio: rt, attack: at, release: release[l]! })
          if (!sameWords(got, expected[c]!)) {
            nWrong++
            if (wrong.length < 5) wrong.push(`ratio ${rt} attack ${at} release ${release[l]}: ${JSON.stringify(got)} (web: ${JSON.stringify(expected[c])})`)
          }
        }
      }
      expect(l).toBe(release.length)
    })
    expect(points).toBe(ratio.length * attack.length * release.length)
    expect({ nWrong, wrong }).toEqual({ nWrong: 0, wrong: [] })
  })
})

describe('AC-16 (4): every character of the texts is in the subset fonts (fonts/chars.txt)', () => {
  const lines = charsRaw.split(/\r?\n/).filter((l) => l !== '')
  const listed = new Set(lines)
  // the subset keeps what is drawn: a blank (U+0020 and below) is no glyph
  const missingIn = (texts: string[]) => [...new Set(texts.flatMap((t) => [...t]))].filter((c) => c.codePointAt(0)! > 0x20 && !listed.has(c)).map((c) => `${c} U+${c.codePointAt(0)!.toString(16).toUpperCase().padStart(4, '0')}`)

  it('chars.txt is one character per line, without repeats', () => {
    expect(lines.filter((l) => [...l].length !== 1)).toEqual([])
    expect(listed.size).toBe(lines.length)
  })
  it('the web app\'s texts (the golden)', () => {
    expect(missingIn(Object.values(golden.texts).flat())).toEqual([])
  })
  it('the texts of the copies of the same files', () => {
    expect(missingIn(Object.keys(golden.texts).flatMap((f) => strings(parse(f))))).toEqual([])
  })
  it.each(['window.ts', 'main.tsx'])('the plug-in\'s own page text in %s', (file) => {
    expect(missingIn(strings(parse(file)))).toEqual([])
  })
})
