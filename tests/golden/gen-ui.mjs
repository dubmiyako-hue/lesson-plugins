// Writes tests/golden/ui.json: the web app's words (mix-course), for plugin/web/tests/copied.test.ts (AC-16, "the screen's
// words are the web app's"): the word tables, every string with a non-ASCII character in the screens' sources, and
// characterWords over a grid that covers every word boundary.
// Usage: node tests/golden/gen-ui.mjs [path to mix-course]   (default D:/work/Claude/mix-course; read only)
//
// ui.json holds
//   webCommit  the web app's commit
//   tables     TOP_LABELS, BOTTOM_LABELS (src/ui/Eq.tsx), RATIO_WORDS, ATTACK_WORDS, RELEASE_WORDS, USES (src/character.ts),
//              read from the source's literal arrays (RATIO_INF is the number it names in src/dsp.ts)
//   texts      per source file (path under src/): the strings with a non-ASCII character, in source order, without
//              repeats. A string is a string literal, the static part of a template literal, or the text in JSX.
//   grid       characterWords over ratio x attack x release (ratio.length * attack.length * release.length points). A
//              result is an index into combos; combos[k] is null (characterWords gave null) or [ratio, attack, release,
//              whole] as indexes into words (whole -1 for null). rows[i * attack.length + j] is the results for ratio i,
//              attack j, along release, run-length coded as "combo:count,...".
// The grid's values: the knob's legal values (fromNorm of the parameter at k / 64, k = 0..64) and, for each start of a range
// in the *_WORDS tables, the start itself and the values 1e-6 (relative) above and below it. Ratio has 1 and RATIO_INF in
// its ranges; at 1 characterWords gives null.
// The extraction below is the same as in plugin/web/tests/copied.test.ts. Keep them so.
import ts from 'typescript'
import { build } from 'esbuild'
import { execFileSync } from 'node:child_process'
import { readFileSync, writeFileSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'

const here = dirname(fileURLToPath(import.meta.url))
const mix = (process.argv[2] ?? 'D:/work/Claude/mix-course').replaceAll('\\', '/')

// ---- the web app's functions, bundled ----
const bundled = await build({
  stdin: {
    contents: [
      "export { characterWords, RATIO_WORDS, ATTACK_WORDS, RELEASE_WORDS, USES } from './src/character.ts'",
      "export { PARAMS, fromNorm } from './src/params.ts'",
      "export { RATIO_INF } from './src/dsp.ts'",
    ].join('\n'),
    resolveDir: mix,
    loader: 'ts',
  },
  bundle: true,
  format: 'esm',
  platform: 'node',
  write: false,
})
const web = await import('data:text/javascript;base64,' + Buffer.from(bundled.outputFiles[0].text).toString('base64'))

// ---- reading the sources ----
const NON_ASCII = /[^\x00-\x7f]/
const parse = (path) => ts.createSourceFile(path, readFileSync(path, 'utf8'), ts.ScriptTarget.ES2022, true, ts.ScriptKind.TSX)

function strings(sf) {
  const out = []
  const visit = (n) => {
    let t
    if (ts.isStringLiteral(n) || ts.isNoSubstitutionTemplateLiteral(n)) t = n.text
    else if (ts.isTemplateHead(n) || ts.isTemplateMiddle(n) || ts.isTemplateTail(n)) t = n.text
    else if (ts.isJsxText(n)) t = n.text.replace(/\s+/g, ' ').trim()
    if (t !== undefined && NON_ASCII.test(t) && !out.includes(t)) out.push(t)
    ts.forEachChild(n, visit)
  }
  visit(sf)
  return out
}

function evaluate(n, env) {
  if (ts.isArrayLiteralExpression(n)) return n.elements.map((e) => evaluate(e, env))
  if (ts.isStringLiteral(n) || ts.isNoSubstitutionTemplateLiteral(n)) return n.text
  if (ts.isNumericLiteral(n)) return Number(n.text)
  if (n.kind === ts.SyntaxKind.NullKeyword) return null
  if (ts.isIdentifier(n) && n.text in env) return env[n.text]
  if (ts.isPrefixUnaryExpression(n) && n.operator === ts.SyntaxKind.MinusToken) return -evaluate(n.operand, env)
  throw new Error(`cannot evaluate ${ts.SyntaxKind[n.kind]}: ${n.getText()}`)
}

function table(sf, name, env) {
  let init
  const visit = (n) => {
    if (ts.isVariableDeclaration(n) && ts.isIdentifier(n.name) && n.name.text === name && n.initializer) init = n.initializer
    ts.forEachChild(n, visit)
  }
  visit(sf)
  if (!init) throw new Error(`${name} not found in ${sf.fileName}`)
  return evaluate(init, env)
}

const env = { RATIO_INF: web.RATIO_INF }
const eqSource = parse(`${mix}/src/ui/Eq.tsx`)
const charSource = parse(`${mix}/src/character.ts`)
const tables = {
  TOP_LABELS: table(eqSource, 'TOP_LABELS', env),
  BOTTOM_LABELS: table(eqSource, 'BOTTOM_LABELS', env),
  RATIO_WORDS: table(charSource, 'RATIO_WORDS', env),
  ATTACK_WORDS: table(charSource, 'ATTACK_WORDS', env),
  RELEASE_WORDS: table(charSource, 'RELEASE_WORDS', env),
  USES: table(charSource, 'USES', env),
}
// what is read from the source is what the bundle runs on
for (const k of ['RATIO_WORDS', 'ATTACK_WORDS', 'RELEASE_WORDS', 'USES']) {
  if (JSON.stringify(tables[k]) !== JSON.stringify(web[k])) throw new Error(`${k} read from the source differs from the bundle's`)
}

const TEXT_FILES = ['Eq', 'Comp', 'Knob', 'Hint', 'PluginHeader', 'Glyph'].map((n) => `ui/${n}.tsx`).concat(['character.ts', 'params.ts'])
const texts = Object.fromEntries(TEXT_FILES.map((f) => [f, strings(parse(`${mix}/src/${f}`))]))

// ---- the grid ----
const knobValues = (p) => Array.from({ length: 65 }, (_, k) => web.fromNorm(p, k / 64))
const around = (starts) => starts.flatMap((b) => [b === 0 ? -1e-6 : b * (1 - 1e-6), b, b === 0 ? 1e-6 : b * (1 + 1e-6)])
const axis = (p, words) => [...new Set([...knobValues(p), ...around(words.map(([from]) => from))])].sort((a, b) => a - b)
const ratio = axis(web.PARAMS.ratio, tables.RATIO_WORDS)
const attack = axis(web.PARAMS.attack, tables.ATTACK_WORDS)
const release = axis(web.PARAMS.release, tables.RELEASE_WORDS)

const words = []
const wordIndex = (w) => {
  const k = words.findIndex((x) => x.word === w.word && x.tip === w.tip)
  if (k >= 0) return k
  words.push({ word: w.word, tip: w.tip })
  return words.length - 1
}
const combos = [null]
const comboIndex = (r) => {
  if (r === null) return 0
  const c = [wordIndex(r.ratio), wordIndex(r.attack), wordIndex(r.release), r.whole ? wordIndex(r.whole) : -1]
  const k = combos.findIndex((x) => x && x.every((v, i) => v === c[i]))
  if (k >= 0) return k
  combos.push(c)
  return combos.length - 1
}
const rows = []
for (const rt of ratio) {
  for (const at of attack) {
    const runs = []
    for (const rl of release) {
      const c = comboIndex(web.characterWords({ ratio: rt, attack: at, release: rl }))
      if (runs.length && runs[runs.length - 1][0] === c) runs[runs.length - 1][1]++
      else runs.push([c, 1])
    }
    rows.push(runs.map(([c, n]) => `${c}:${n}`).join(','))
  }
}

const webCommit = execFileSync('git', ['-C', mix, 'rev-parse', '--short', 'HEAD'], { encoding: 'utf8' }).trim()
const out = { webCommit, tables, texts, grid: { ratio, attack, release, words, combos, rows } }
writeFileSync(join(here, 'ui.json'), JSON.stringify(out, null, 1) + '\n')
const points = ratio.length * attack.length * release.length
const strs = Object.values(texts).reduce((n, a) => n + a.length, 0)
console.log(`ui.json: ${Object.keys(tables).length} tables, ${strs} texts in ${TEXT_FILES.length} files, ${points} grid points (${ratio.length} x ${attack.length} x ${release.length}), ${words.length} words, ${combos.length} combos, web ${webCommit}`)
