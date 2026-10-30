// Writes tests/golden/eq/: the web app's EqFilters (mix-course src/chain.ts) rendered by Edge's OfflineAudioContext, for
// core_tests "[golden-eq]" (AC-06). EqFilters.apply(bands, false) sets each case on a fresh chain, and the 13 biquads
// are wired in series as StemChain wires them.
// Usage: node tests/golden/gen-eq.mjs [path to mix-course] [path to msedge.exe]
//   (defaults D:/work/Claude/mix-course, read only, and C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe)
//
// esbuild bundles the page (the worklet's import is left empty: EqFilters does not use it); a local HTTP server on
// 127.0.0.1 serves it to a headless Edge, which posts each rendered case back.
// Each case's input is exact in float32 and the C++ test makes it again: channel c is an impulse of 1 - 0.5 c at sample
// 0, silence, then from the middle 0.25 times an LCG's noise (seed 99 + c). <name>.out.f32 is the output, float32 little
// endian, channel after channel. cases.tsv lists name, fs, channels, frames and the 7 bands (on, freq, gain, q, slope);
// qdb.tsv the Q AudioParam (dB, float32) of each HP and LP stage: slope, stage, value.
import { build } from 'esbuild'
import { execFileSync, spawn } from 'node:child_process'
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs'
import { createServer } from 'node:http'
import { tmpdir } from 'node:os'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'

const here = dirname(fileURLToPath(import.meta.url))
const mix = (process.argv[2] ?? 'D:/work/Claude/mix-course').replaceAll('\\', '/')
const edge = process.argv[3] ?? 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'
const FRAMES = 4096

const page = `
import { BANDS, EqFilters } from '${mix}/src/chain.ts'
import { butterworthQs } from '${mix}/src/dsp.ts'

function input(channels, frames) {
  return Array.from({ length: channels }, (_, c) => {
    const x = new Float32Array(frames)
    x[0] = 1 - 0.5 * c
    let s = (99 + c) >>> 0
    for (let i = frames / 2; i < frames; i++) {
      s = (Math.imul(s, 1664525) + 1013904223) >>> 0
      x[i] = 0.25 * (((s >>> 8) - 8388608) / 8388608)
    }
    return x
  })
}

async function render(c) {
  const ctx = new OfflineAudioContext({ numberOfChannels: c.channels, length: c.frames, sampleRate: c.fs })
  const buffer = ctx.createBuffer(c.channels, c.frames, c.fs)
  input(c.channels, c.frames).forEach((x, ch) => buffer.copyToChannel(x, ch))
  const source = ctx.createBufferSource()
  source.buffer = buffer
  const eq = new EqFilters(ctx)
  eq.apply(c.bands, false)
  let tail = source
  for (const f of eq.filters.flat()) tail = tail.connect(f)
  tail.connect(ctx.destination)
  source.start(0)
  const out = await ctx.startRendering()
  const all = new Float32Array(c.channels * c.frames)
  for (let ch = 0; ch < c.channels; ch++) all.set(out.getChannelData(ch), ch * c.frames)
  return all
}

const post = (path, body) => fetch(path, { method: 'POST', body })
try {
  if (BANDS.length !== 7) throw new Error('BANDS changed')
  const qdb = []
  for (const slope of [12, 24, 36, 48])
    butterworthQs(slope / 6).forEach((q, k) => qdb.push([slope, k, Math.fround(20 * Math.log10(q))].join('\\t')))
  await post('/qdb', qdb.join('\\n') + '\\n')
  const cases = await (await fetch('/cases')).json()
  for (const c of cases) await post('/result/' + c.name, await render(c))
  await post('/done', navigator.userAgent)
} catch (e) {
  await post('/error', String(e && e.stack || e))
}
`

const stub = {
  name: 'stub-worklet',
  setup(b) {
    b.onResolve({ filter: /\?worker&url$/ }, (args) => ({ path: args.path, namespace: 'stub' }))
    b.onLoad({ filter: /.*/, namespace: 'stub' }, () => ({ contents: 'export default ""', loader: 'js' }))
  },
}
const bundled = await build({
  stdin: { contents: page, resolveDir: here, loader: 'js' },
  bundle: true, format: 'esm', platform: 'browser', write: false, plugins: [stub],
})
const html = `<!doctype html><meta charset="utf-8"><script type="module">${bundled.outputFiles[0].text}</script>`

const band = (on, freq, gain = 0, q = 1, slope = 12) => ({ on, freq, gain, q, slope })
const DEFAULTS = [band(false, 80, 0, 1, 24), band(true, 120), band(true, 300), band(true, 1000), band(true, 4000), band(true, 8000),
  band(false, 18000)]
const HP = 0, LS = 1, B1 = 2, B2 = 3, B3 = 4, HS = 5, LP = 6
const settings = []
const add = (label, changes) => {
  const bands = DEFAULTS.map((b) => ({ ...b }))
  for (const [i, patch] of Object.entries(changes)) Object.assign(bands[i], patch)
  settings.push({ label, bands })
}
add('flat', {})
for (const slope of [12, 24, 36, 48]) add(`hp ${slope}`, { [HP]: { on: true, slope } })
add('lp 18k 12', { [LP]: { on: true, freq: 18000, slope: 12 } })
add('lp 5k 24', { [LP]: { on: true, freq: 5000, slope: 24 } })
add('lp 1k 36', { [LP]: { on: true, freq: 1000, slope: 36 } })
add('lp 20k 48', { [LP]: { on: true, freq: 20000, slope: 48 } })
add('hp 20k 12', { [HP]: { on: true, freq: 20000, slope: 12 } })
add('hp 20 48', { [HP]: { on: true, freq: 20, slope: 48 } })
add('lp 20 48', { [LP]: { on: true, freq: 20, slope: 48 } })
add('ls +12', { [LS]: { gain: 12 } })
add('ls 20 -18', { [LS]: { freq: 20, gain: -18 } })
add('hs 20k +18', { [HS]: { freq: 20000, gain: 18 } })
add('hs -6', { [HS]: { gain: -6 } })
add('bell 20 q0.3 +18', { [B1]: { freq: 20, gain: 18, q: 0.3 } })
add('bell 20k q12 -18', { [B2]: { freq: 20000, gain: -18, q: 12 } })
add('bell 1k q12 +18', { [B3]: { freq: 1000, gain: 18, q: 12 } })
add('bell 4k q0.3 -18', { [B1]: { freq: 4000, gain: -18, q: 0.3 } })
add('all', {
  [HP]: { on: true, freq: 30, slope: 48 }, [LS]: { freq: 150, gain: 4.5 }, [B1]: { freq: 250, gain: -3, q: 2 },
  [B2]: { freq: 1200, gain: 6, q: 0.7 }, [B3]: { freq: 5000, gain: -9, q: 4 }, [HS]: { freq: 10000, gain: 2.5 },
  [LP]: { on: true, freq: 15000, slope: 36 },
})
add('all 2', {
  [HP]: { on: true, freq: 120, slope: 24 }, [LS]: { freq: 200, gain: -3 }, [B1]: { freq: 300, gain: 6, q: 0.5 },
  [B2]: { freq: 2500, gain: -4.5, q: 8 }, [HS]: { freq: 6000, gain: 4 }, [LP]: { on: true, freq: 9000, slope: 12 },
})
add('off with gains', {
  [HP]: { slope: 48 }, [LS]: { on: false, gain: 12 }, [B1]: { on: false, gain: -9, q: 4 }, [HS]: { on: false, gain: 6 },
  [LP]: { slope: 36 },
})

const cases = []
;[44100, 48000, 96000].forEach((fs, r) =>
  settings.forEach((s, k) => {
    const name = `case-${String(cases.length).padStart(2, '0')}`
    cases.push({ name, label: s.label, fs, channels: ((k + r) % 2) + 1, frames: FRAMES, bands: s.bands })
  }),
)

const dir = join(here, 'eq')
rmSync(dir, { recursive: true, force: true })
mkdirSync(dir, { recursive: true })

const profile = mkdtempSync(join(tmpdir(), 'lesson-gen-eq-'))
let browser
let agent = ''
let bytes = 0
const received = new Set()
const finished = new Promise((resolve, reject) => {
  const server = createServer((req, res) => {
    const chunks = []
    req.on('data', (d) => chunks.push(d))
    req.on('end', () => {
      const body = Buffer.concat(chunks)
      const url = req.url ?? ''
      if (req.method === 'GET' && url === '/') res.writeHead(200, { 'content-type': 'text/html' }).end(html)
      else if (req.method === 'GET' && url === '/cases') res.writeHead(200, { 'content-type': 'application/json' }).end(JSON.stringify(cases))
      else if (req.method === 'POST' && url === '/qdb') {
        writeFileSync(join(dir, 'qdb.tsv'), body)
        res.end()
      } else if (req.method === 'POST' && url.startsWith('/result/')) {
        const name = url.slice('/result/'.length)
        const c = cases.find((x) => x.name === name)
        if (!c || body.length !== 4 * c.channels * c.frames) reject(new Error(`bad result ${name}`))
        writeFileSync(join(dir, `${name}.out.f32`), body)
        bytes += body.length
        received.add(name)
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
if (received.size !== cases.length) throw new Error(`got ${received.size} of ${cases.length} cases`)

const row = (c) => [c.name, c.fs, c.channels, c.frames, ...c.bands.flatMap((b) => [b.on ? 1 : 0, b.freq, b.gain, b.q, b.slope])].join('\t')
writeFileSync(join(dir, 'cases.tsv'), cases.map(row).join('\n') + '\n')
const git = (...args) => execFileSync('git', ['-C', mix, ...args], { encoding: 'utf8' }).trim()
const manifest = {
  generator: 'tests/golden/gen-eq.mjs',
  source: 'mix-course src/chain.ts EqFilters.apply(bands, false), rendered by OfflineAudioContext',
  commit: git('rev-parse', 'HEAD'),
  sourceChanged: git('status', '--porcelain', '--', 'src/chain.ts', 'src/dsp.ts') !== '',
  browser: agent,
  input: 'channel c: impulse 1 - 0.5c at 0, then 0.25 * LCG noise (seed 99 + c) from frames / 2',
  layout: 'float32 little endian, planar (channel after channel)',
  cases: cases.map(({ name, label, fs, channels, frames, bands }) => ({ name, label, fs, channels, frames, bands })),
}
writeFileSync(join(dir, 'manifest.json'), JSON.stringify(manifest, null, 2) + '\n')
console.log(`eq: ${cases.length} cases, ${(bytes / 1e6).toFixed(1)} MB, ${agent}, from mix-course ${manifest.commit}`)
