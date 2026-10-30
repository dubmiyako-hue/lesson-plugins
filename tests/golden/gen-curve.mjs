// Writes tests/golden/curve/: the EQ curve the plugin's screen draws, for core_tests "[curve]" (AC-17). The screen's
// own steps (plugin/web/src/app.ts class Curve, plugin/web/src/ui/Eq.tsx) run in a headless Edge: an
// OfflineAudioContext(1, 128, fs), EqFilters(ctx), apply(bands, false), then every one of the 13 biquads'
// getFrequencyResponse multiplied into a Float32Array that starts at 1. The log10 and the clamp are the test's.
// Usage: node tests/golden/gen-curve.mjs [path to msedge.exe]
//   (default C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe)
//
// esbuild bundles plugin/web/src/chain.ts (and dsp.ts); a local HTTP server on 127.0.0.1 serves the page and the cases to
// a headless Edge, which posts each case's product back.
// Files per case <name>: <name>.freq.f32 (the frequencies, float32 little endian, ascending: 512 points, logarithmic from
// 20 Hz to 20 kHz, and the 7 bands' own frequencies) and <name>.mag.f32 (the product `mag` at them, float32 little
// endian, as the screen holds it). cases.tsv lists name, fs and the 7 bands (on, freq, gain, q, slope);
// manifest.json: the Edge version, the date of the data, the hash of the two sources and the cases' descriptions.
// Run again with nothing changed, the files are byte for byte the same (the date stays the one of the data they hold).
import { build } from 'esbuild'
import { spawn } from 'node:child_process'
import { createHash } from 'node:crypto'
import { existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs'
import { createServer } from 'node:http'
import { tmpdir } from 'node:os'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'

const here = dirname(fileURLToPath(import.meta.url))
const web = join(here, '..', '..', 'plugin', 'web', 'src').replaceAll('\\', '/')
const edge = process.argv[2] ?? 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'
const POINTS = 512

const page = `
import { BANDS, EqFilters } from '${web}/chain.ts'

// The screen's curve: Curve.setRate / apply in app.ts, then the loop in ui/Eq.tsx.
function curve(c) {
  const ctx = new OfflineAudioContext(1, 128, c.fs)
  const eq = new EqFilters(ctx)
  eq.apply(c.bands, false)
  const freqs = new Float32Array(c.freqs)
  const n = freqs.length
  const mag = new Float32Array(n).fill(1)
  const tmp = new Float32Array(n)
  const ph = new Float32Array(n)
  for (const f of eq.filters.flat()) {
    f.getFrequencyResponse(freqs, tmp, ph)
    for (let k = 0; k < n; k++) mag[k] *= tmp[k]
  }
  return mag
}

const post = (path, body) => fetch(path, { method: 'POST', body })
try {
  if (BANDS.length !== 7) throw new Error('BANDS changed')
  const cases = await (await fetch('/cases')).json()
  for (const c of cases) await post('/result/' + c.name, curve(c))
  await post('/done', navigator.userAgent)
} catch (e) {
  await post('/error', String((e && e.stack) || e))
}
`
const bundled = await build({
  stdin: { contents: page, resolveDir: here, loader: 'js' },
  bundle: true, format: 'esm', platform: 'browser', write: false,
})
const html = `<!doctype html><meta charset="utf-8"><script type="module">${bundled.outputFiles[0].text}</script>`

// The cases. Values lie in core/src/Params.cpp's ranges: freq 20..20000 Hz, gain -18..18 dB, q 0.3..12, slope 12/24/36/48.
const band = (on, freq, gain = 0, q = 1, slope = 12) => ({ on, freq, gain, q, slope })
const DEFAULTS = [band(false, 80, 0, 1, 24), band(true, 120), band(true, 300), band(true, 1000), band(true, 4000), band(true, 8000),
  band(false, 18000)]
const HP = 0, LS = 1, B1 = 2, B2 = 3, B3 = 4, HS = 5, LP = 6
const RATES = [44100, 48000, 96000, 192000]
const settings = []
const add = (label, changes, fs) => {
  const bands = DEFAULTS.map((b) => ({ ...b }))
  for (const [i, patch] of Object.entries(changes)) Object.assign(bands[i], patch)
  settings.push({ label, bands, fs })
}
add('flat', {})
add('hp 80 12', { [HP]: { on: true, freq: 80, slope: 12 } })
add('hp 200 24', { [HP]: { on: true, freq: 200, slope: 24 } })
add('hp 1k 36', { [HP]: { on: true, freq: 1000, slope: 36 } })
add('hp 20 48', { [HP]: { on: true, freq: 20, slope: 48 } })
add('hp 20k 48', { [HP]: { on: true, freq: 20000, slope: 48 } })
add('lp 18k 12', { [LP]: { on: true, freq: 18000, slope: 12 } })
add('lp 5k 24', { [LP]: { on: true, freq: 5000, slope: 24 } })
add('lp 1k 36', { [LP]: { on: true, freq: 1000, slope: 36 } })
add('lp 100 48', { [LP]: { on: true, freq: 100, slope: 48 } })
add('lp 20k 12', { [LP]: { on: true, freq: 20000, slope: 12 } }, 44100)
add('lp 20k 48', { [LP]: { on: true, freq: 20000, slope: 48 } }, 44100)
add('ls 120 +18', { [LS]: { gain: 18 } })
add('ls 20 -18', { [LS]: { freq: 20, gain: -18 } })
add('hs 8k +18', { [HS]: { gain: 18 } })
add('hs 20k -18', { [HS]: { freq: 20000, gain: -18 } })
add('bell1 300 q0.3 +18', { [B1]: { gain: 18, q: 0.3 } })
add('bell1 300 q12 -18', { [B1]: { gain: -18, q: 12 } })
add('bell2 1k q0.3 -18', { [B2]: { gain: -18, q: 0.3 } })
add('bell2 1k q12 +18', { [B2]: { gain: 18, q: 12 } })
add('bell3 20k q0.3 +18', { [B3]: { freq: 20000, gain: 18, q: 0.3 } })
add('bell3 20 q12 -18', { [B3]: { freq: 20, gain: -18, q: 12 } })
add('all on', {
  [HP]: { on: true, freq: 30, slope: 48 }, [LS]: { freq: 150, gain: 4.5 }, [B1]: { freq: 250, gain: -3, q: 2 },
  [B2]: { freq: 1200, gain: 6, q: 0.7 }, [B3]: { freq: 5000, gain: -9, q: 4 }, [HS]: { freq: 10000, gain: 2.5 },
  [LP]: { on: true, freq: 15000, slope: 36 },
})
add('all on extremes', {
  [HP]: { on: true, freq: 20, slope: 48 }, [LS]: { freq: 20000, gain: 18 }, [B1]: { freq: 20, gain: 18, q: 12 },
  [B2]: { freq: 20000, gain: -18, q: 12 }, [B3]: { freq: 1000, gain: 18, q: 0.3 }, [HS]: { freq: 20, gain: -18 },
  [LP]: { on: true, freq: 20000, slope: 48 },
})
add('off with gains', {
  [HP]: { slope: 48 }, [LS]: { on: false, gain: 12 }, [B1]: { on: false, gain: -9, q: 4 }, [HS]: { on: false, gain: 6 },
  [LP]: { slope: 36 },
})

// 512 points from 20 Hz to 20 kHz, logarithmic, and the bands' own frequencies; ascending.
const grid = Array.from({ length: POINTS }, (_, k) => 20 * 1000 ** (k / (POINTS - 1)))
const cases = settings.map((s, k) => {
  const freqs = Float32Array.from([...grid, ...s.bands.map((b) => b.freq)])
  freqs.sort()
  return { name: `case-${String(k).padStart(2, '0')}`, label: s.label, fs: s.fs ?? RATES[k % RATES.length], bands: s.bands, freqs }
})
for (const fs of RATES) if (!cases.some((c) => c.fs === fs)) throw new Error(`no case at ${fs}`)

const dir = join(here, 'curve')
mkdirSync(dir, { recursive: true })

const profile = mkdtempSync(join(tmpdir(), 'lesson-gen-curve-'))
let browser
let agent = ''
const results = new Map()
const finished = new Promise((resolve, reject) => {
  const server = createServer((req, res) => {
    const chunks = []
    req.on('data', (d) => chunks.push(d))
    req.on('end', () => {
      const body = Buffer.concat(chunks)
      const url = req.url ?? ''
      if (req.method === 'GET' && url === '/') res.writeHead(200, { 'content-type': 'text/html' }).end(html)
      else if (req.method === 'GET' && url === '/cases')
        res.writeHead(200, { 'content-type': 'application/json' }).end(
          JSON.stringify(cases.map((c) => ({ name: c.name, fs: c.fs, bands: c.bands, freqs: Array.from(c.freqs) }))))
      else if (req.method === 'POST' && url.startsWith('/result/')) {
        const name = url.slice('/result/'.length)
        const c = cases.find((x) => x.name === name)
        if (!c || body.length !== 4 * c.freqs.length) {
          res.end()
          server.close()
          reject(new Error(`bad result ${name}`))
          return
        }
        results.set(name, Buffer.from(body))
        res.end()
      } else if (req.method === 'POST' && url === '/done') {
        agent = body.toString()
        res.end()
        server.close()
        resolve()
      } else if (req.method === 'POST' && url === '/error') {
        res.end()
        server.close()
        reject(new Error(`page: ${body}`))
      } else res.writeHead(404).end()
    })
  })
  server.listen(0, '127.0.0.1', () => {
    const { port } = server.address()
    browser = spawn(edge, ['--headless=new', '--disable-gpu', '--no-first-run', '--no-default-browser-check',
      '--disable-extensions', `--user-data-dir=${profile}`, `http://127.0.0.1:${port}/`], { stdio: 'ignore' })
    browser.on('error', reject)
  })
  setTimeout(() => reject(new Error('timed out')), 120000).unref()
})

try {
  await finished
} finally {
  browser?.kill()
  await new Promise((r) => setTimeout(r, 500))
  rmSync(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 })
}
if (results.size !== cases.length) throw new Error(`got ${results.size} of ${cases.length} cases`)

const files = new Map()
for (const c of cases) {
  const body = results.get(c.name)
  const mag = new Float32Array(body.buffer.slice(body.byteOffset, body.byteOffset + body.length))
  // A response outside Web Audio's range comes back NaN; the screen would not draw it, and the test could not compare it.
  if (!mag.every((v) => Number.isFinite(v) && v >= 0)) throw new Error(`${c.name}: a magnitude is NaN, infinite or negative`)
  files.set(`${c.name}.freq.f32`, Buffer.from(c.freqs.buffer, c.freqs.byteOffset, c.freqs.byteLength))
  files.set(`${c.name}.mag.f32`, Buffer.from(mag.buffer, mag.byteOffset, mag.byteLength))
}
const row = (c) => [c.name, c.fs, ...c.bands.flatMap((b) => [b.on ? 1 : 0, b.freq, b.gain, b.q, b.slope])].join('\t')
files.set('cases.tsv', Buffer.from(cases.map(row).join('\n') + '\n'))

const hash = createHash('sha256')
for (const f of ['chain.ts', 'dsp.ts']) hash.update(readFileSync(join(web, f)).toString('utf8').replaceAll('\r\n', '\n'))
const sourceSha256 = hash.digest('hex')
const manifestOf = (date) => ({
  generator: 'tests/golden/gen-curve.mjs',
  source: 'plugin/web/src: Curve (app.ts) and the curve loop (ui/Eq.tsx) on EqFilters (chain.ts): the product of the 13 biquads\' getFrequencyResponse in a Float32Array, in Edge',
  sourceSha256,
  browser: agent,
  date,
  layout: `float32 little endian; per case ascending frequencies (${POINTS} logarithmic points from 20 Hz to 20 kHz and the 7 bands' own) and the product of the stages' magnitudes at them`,
  cases: cases.map(({ name, label, fs, bands }) => ({ name, label, fs, bands })),
})
// The date of the data: kept while the data is the same, so that a second run writes the same bytes.
const today = new Date()
let date = `${today.getFullYear()}-${String(today.getMonth() + 1).padStart(2, '0')}-${String(today.getDate()).padStart(2, '0')}`
try {
  const old = JSON.parse(readFileSync(join(dir, 'manifest.json'), 'utf8'))
  const same = [...files].every(([n, b]) => existsSync(join(dir, n)) && readFileSync(join(dir, n)).equals(b)) &&
    readdirSync(dir).length === files.size + 1
  if (same && JSON.stringify({ ...old, date: '' }) === JSON.stringify(manifestOf(''))) date = old.date
} catch {
  // no earlier run
}
for (const n of readdirSync(dir)) if (!files.has(n) && n !== 'manifest.json') rmSync(join(dir, n))
for (const [n, b] of files) writeFileSync(join(dir, n), b)
writeFileSync(join(dir, 'manifest.json'), JSON.stringify(manifestOf(date), null, 2) + '\n')
const total = [...files.values()].reduce((s, b) => s + b.length, 0)
console.log(`curve: ${cases.length} cases, ${(total / 1e3).toFixed(0)} kB, ${agent}, ${date}`)
