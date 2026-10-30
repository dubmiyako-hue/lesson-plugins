// Writes tests/golden/spectrum/: Edge's AnalyserNode (Chromium's RealtimeAnalyser) on fixed signals, for core_tests
// "[spectrum]" (AC-18). No mix-course source is used: the web app's analysers are fftSize 8192 (src/chain.ts, src/player.ts).
// Usage: node tests/golden/gen-spectrum.mjs [path to msedge.exe]
//   (default C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe)
//
// An OfflineAudioContext (mono) plays each signal's 8192 samples through an AnalyserNode (fftSize 8192,
// smoothingTimeConstant 0). It is suspended after exactly 8192 frames, where getFloatFrequencyData (4096 bins, dB) and
// getFloatTimeDomainData are read. The time-domain data must equal the input, which proves that the analysed window is
// the signal and nothing is shifted. A local HTTP server on 127.0.0.1 serves the page and the inputs to a headless Edge,
// which posts each result back.
// Files per signal <name>: <name>.in.f32 (the input, float32 little endian: Math.sin and libm may differ by a bit, so the
// C++ test reads the input instead of making it again) and <name>.db.f32 (the 4096 dB values). cases.tsv lists name, fs
// and the recipe's text. manifest.json: the Edge version, the date of the data and the recipes. Run again with nothing
// changed, the files are byte for byte the same (the date stays the one of the data they hold).
import { mkdirSync, mkdtempSync, existsSync, readFileSync, rmSync, writeFileSync } from 'node:fs'
import { spawn } from 'node:child_process'
import { createServer } from 'node:http'
import { tmpdir } from 'node:os'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'

const here = dirname(fileURLToPath(import.meta.url))
const edge = process.argv[2] ?? 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'
const N = 8192
const BINS = N / 2

// The LCG of gen-eq.mjs: uniform in [-1, 1), exact in float32.
function lcg(seed) {
  let s = seed >>> 0
  return () => {
    s = (Math.imul(s, 1664525) + 1013904223) >>> 0
    return ((s >>> 8) - 8388608) / 8388608
  }
}
// tones: [amplitude, frequency Hz]; noise: [amplitude, seed]; x[i] = fround(sum of amp sin(2 pi f i / fs) + amp noise)
function make(fs, tones, noise) {
  const x = new Float32Array(N)
  const next = noise ? lcg(noise[1]) : null
  for (let i = 0; i < N; i++) {
    let v = 0
    for (const [a, f] of tones) v += a * Math.sin((2 * Math.PI * f * i) / fs)
    if (next) v += noise[0] * next()
    x[i] = Math.fround(v)
  }
  return x
}
const recipe = (tones, noise) =>
  [...tones.map(([a, f]) => `${a} sin(2 pi ${f} i / fs)`), ...(noise ? [`${noise[0]} * LCG noise (seed ${noise[1]})`] : [])].join(' + ')
const cases = [
  { name: 'sine-noise-48000', fs: 48000, tones: [[0.5, 1234.567]], noise: [0.01, 7] },
  { name: 'sine-noise-44100', fs: 44100, tones: [[0.5, 3210.123]], noise: [0.01, 7] },
  { name: 'sine-noise-96000', fs: 96000, tones: [[0.5, 7000.77]], noise: [0.01, 7] },
  { name: 'noise-48000', fs: 48000, tones: [], noise: [0.25, 11] },
  { name: 'two-tones-44100', fs: 44100, tones: [[0.3, 440.7], [0.002, 9000.3]], noise: null },
].map((c) => ({ ...c, recipe: recipe(c.tones, c.noise), input: make(c.fs, c.tones, c.noise) }))

const page = `
async function render(c) {
  const input = new Float32Array(await (await fetch('/input/' + c.name)).arrayBuffer())
  const ctx = new OfflineAudioContext({ numberOfChannels: 1, length: ${N + 128}, sampleRate: c.fs })
  const buffer = ctx.createBuffer(1, ${N + 128}, ctx.sampleRate)
  buffer.copyToChannel(input, 0)
  const source = ctx.createBufferSource()
  source.buffer = buffer
  const an = ctx.createAnalyser()
  an.fftSize = ${N}
  an.smoothingTimeConstant = 0
  source.connect(an)
  an.connect(ctx.destination)
  source.start(0)
  const db = new Float32Array(an.frequencyBinCount)
  const td = new Float32Array(${N})
  const suspended = ctx.suspend(${N} / c.fs).then(() => {
    an.getFloatFrequencyData(db)
    an.getFloatTimeDomainData(td)
    return ctx.resume()
  })
  const rendering = ctx.startRendering()
  await suspended
  await rendering
  const all = new Float32Array(db.length + td.length)
  all.set(db, 0)
  all.set(td, db.length)
  return all
}
const post = (path, body) => fetch(path, { method: 'POST', body })
try {
  const cases = await (await fetch('/cases')).json()
  for (const c of cases) await post('/result/' + c.name, await render(c))
  await post('/done', navigator.userAgent)
} catch (e) {
  await post('/error', String((e && e.stack) || e))
}
`
const html = `<!doctype html><meta charset="utf-8"><script type="module">${page}</script>`

const dir = join(here, 'spectrum')
mkdirSync(dir, { recursive: true })

const profile = mkdtempSync(join(tmpdir(), 'lesson-gen-spectrum-'))
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
        res.writeHead(200, { 'content-type': 'application/json' }).end(JSON.stringify(cases.map(({ name, fs }) => ({ name, fs }))))
      else if (req.method === 'GET' && url.startsWith('/input/')) {
        const c = cases.find((x) => x.name === url.slice('/input/'.length))
        if (!c) res.writeHead(404).end()
        else res.writeHead(200, { 'content-type': 'application/octet-stream' }).end(Buffer.from(c.input.buffer))
      } else if (req.method === 'POST' && url.startsWith('/result/')) {
        const name = url.slice('/result/'.length)
        if (!cases.some((x) => x.name === name) || body.length !== 4 * (BINS + N)) reject(new Error(`bad result ${name}`))
        results.set(name, new Float32Array(body.buffer.slice(body.byteOffset, body.byteOffset + body.length)))
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
if (results.size !== cases.length) throw new Error(`got ${results.size} of ${cases.length} signals`)

const files = new Map()
for (const c of cases) {
  const r = results.get(c.name)
  const td = r.subarray(BINS)
  for (let i = 0; i < N; i++)
    if (!Object.is(td[i], c.input[i])) throw new Error(`${c.name}: the analysed window is not the input (sample ${i}: ${td[i]} vs ${c.input[i]})`)
  const db = r.subarray(0, BINS)
  // A bin whose magnitude is exactly 0 reads -Infinity (the test takes it as the floor); anything else must be a number.
  if (!db.every((v) => Number.isFinite(v) || v === -Infinity)) throw new Error(`${c.name}: a bin is NaN or +Infinity`)
  files.set(`${c.name}.in.f32`, Buffer.from(c.input.buffer))
  files.set(`${c.name}.db.f32`, Buffer.from(db.buffer, db.byteOffset, db.byteLength))
}
files.set('cases.tsv', Buffer.from(cases.map((c) => [c.name, c.fs, c.recipe].join('\t')).join('\n') + '\n'))

const manifestOf = (date) => ({
  generator: 'tests/golden/gen-spectrum.mjs',
  source: 'AnalyserNode.getFloatFrequencyData of an OfflineAudioContext (mono), fftSize 8192, smoothingTimeConstant 0, read after exactly 8192 frames',
  browser: agent,
  date,
  layout: 'float32 little endian; .in.f32 8192 samples, .db.f32 4096 bins (dB)',
  signals: cases.map(({ name, fs, recipe: r }) => ({ name, fs, recipe: `x[i] = fround(${r})` })),
})
// The date of the data: kept while the data is the same, so that a second run writes the same bytes.
const today = new Date()
let date = `${today.getFullYear()}-${String(today.getMonth() + 1).padStart(2, '0')}-${String(today.getDate()).padStart(2, '0')}`
try {
  const old = JSON.parse(readFileSync(join(dir, 'manifest.json'), 'utf8'))
  const same = [...files].every(([n, b]) => existsSync(join(dir, n)) && readFileSync(join(dir, n)).equals(b))
  if (same && JSON.stringify({ ...old, date: '' }) === JSON.stringify(manifestOf(''))) date = old.date
} catch {
  // no earlier run
}
for (const [n, b] of files) writeFileSync(join(dir, n), b)
writeFileSync(join(dir, 'manifest.json'), JSON.stringify(manifestOf(date), null, 2) + '\n')
const total = [...files.values()].reduce((s, b) => s + b.length, 0)
console.log(`spectrum: ${cases.length} signals, ${(total / 1e3).toFixed(0)} kB, ${agent}, ${date}`)
