// Writes tests/golden/params.tsv: the web app's toNorm, fromNorm and value texts (mix-course src/params.ts), and
// JavaScript's toFixed and Math.round, for core_tests "[norm]" and "[text]".
// Usage: node tests/golden/gen-params.mjs [path to mix-course]   (default D:/work/Claude/mix-course; read only)
import { build } from 'esbuild'
import { execFileSync } from 'node:child_process'
import { writeFileSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'

const here = dirname(fileURLToPath(import.meta.url))
const mix = (process.argv[2] ?? 'D:/work/Claude/mix-course').replaceAll('\\', '/')
const bundled = await build({ entryPoints: [`${mix}/src/params.ts`], bundle: true, format: 'esm', platform: 'node', write: false })
const { PARAMS, BAND_PARAMS, toNorm, fromNorm, showValue } = await import(
  'data:text/javascript;base64,' + Buffer.from(bundled.outputFiles[0].text).toString('base64'))

const params = {
  threshold: PARAMS.threshold, ratio: PARAMS.ratio, attack: PARAMS.attack, release: PARAMS.release,
  freq: BAND_PARAMS.freq, gain: BAND_PARAMS.gain, q: BAND_PARAMS.q,
}

// A fixed pseudo-random sequence (the file must not change between runs).
let seed = 12345
const random = () => ((seed = (seed * 1103515245 + 12345) % 2147483648) / 2147483648)

const lines = []
const norms = []
for (let k = 0; k <= 2000; k++) norms.push(k / 2000)
for (const edge of [0.02, 0.04, 0.96, 0.98, 0.5])
  for (const d of [-1e-6, -1e-9, 0, 1e-9, 1e-6]) norms.push(edge + d)
for (let k = 0; k < 500; k++) norms.push(random())
norms.push(-0.1, 1.1)

for (const [key, p] of Object.entries(params)) {
  const values = new Set()
  for (const n of norms) {
    const v = fromNorm(p, n)
    lines.push(`from\t${key}\t${n}\t${v}`)
    values.add(v)
  }
  for (const v of [p.min, p.max, 0, -1, p.min / 2, p.max * 2, p.min * 1.0001, p.max * 0.9999, p.inf?.value ?? p.max, 1e6])
    values.add(v)
  for (let k = 0; k < 300; k++) values.add(p.log ? p.min * (p.max / p.min) ** (random() * 1.2 - 0.1) : p.min + (p.max - p.min) * (random() * 1.2 - 0.1))
  for (const v of values) lines.push(`to\t${key}\t${v}\t${toNorm(p, v)}`)
  // The texts around the format's breaks (sig3 at 9.995, 99.95, 999.5; toFixed's ties) as well as the knob's values.
  const texts = new Set(values)
  for (const base of [9.995, 99.95, 999.5, 9995, 99950])
    for (const d of [-1e-9, 0, 1e-9]) texts.add(base + d)
  for (let k = -400; k <= 400; k++) texts.add(k / 20)
  for (let k = 0; k <= 400; k++) texts.add(k / 8)
  for (const v of [0.125, 0.375, 1.005, 2.675, 0.05, 0.25, -0.05, -0.04, -0.25, 1.25, 12.25, 1234.5, 20000, 19999.5])
    texts.add(v)
  for (const v of texts) lines.push(`text\t${key}\t${v}\t${showValue(p, v)}`)
}

const fixedValues = [0, -0, 0.5, 1.5, 2.5, -2.5, 0.125, 0.375, 1.005, 2.675, 1.45, 8.345, 1e-7, 123456789.125, 0.05, 0.15, 0.25, 0.35]
for (let k = 0; k < 400; k++) fixedValues.push((random() - 0.5) * 2000, random() * 10)
for (let k = 0; k <= 200; k++) fixedValues.push(k / 200, k / 40)
for (const v of fixedValues)
  for (const digits of [0, 1, 2]) lines.push(`fixed\t${digits}\t${v}\t${v.toFixed(digits)}`)
for (const v of [0.5, 1.5, 2.5, -0.5, -1.5, -2.5, 0.49999999999999994, -0.49999999999999994, 4503599627370495.5, -3.7, 3.7, 1e300, -1e300])
  lines.push(`round\t${v}\t${Math.round(v)}`)
for (let k = -100; k <= 100; k++) lines.push(`round\t${k / 4}\t${Math.round(k / 4)}`)

writeFileSync(join(here, 'params.tsv'), lines.join('\n') + '\n')
const git = (...args) => execFileSync('git', ['-C', mix, ...args], { encoding: 'utf8' }).trim()
const manifest = {
  generator: 'tests/golden/gen-params.mjs',
  source: 'mix-course src/params.ts (with src/dsp.ts)',
  commit: git('rev-parse', 'HEAD'),
  sourceChanged: git('status', '--porcelain', '--', 'src/params.ts', 'src/dsp.ts') !== '',
  node: process.version,
  lines: lines.length,
}
writeFileSync(join(here, 'params.manifest.json'), JSON.stringify(manifest, null, 2) + '\n')
console.log(`params.tsv: ${lines.length} lines from mix-course ${manifest.commit}`)
