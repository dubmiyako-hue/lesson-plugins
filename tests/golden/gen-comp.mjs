// Writes tests/golden/comp/: the web app's CompressorCore (mix-course src/dsp.ts) run as its AudioWorklet runs it (blocks of
// 128 samples, the parameters as float32), for core_tests "[golden-comp]" (AC-07).
// Usage: node tests/golden/gen-comp.mjs [path to mix-course]   (default D:/work/Claude/mix-course; read only)
//
// Each case is <name>.in.f32 and <name>.out.f32: float32 little endian, channel after channel (all of channel 0, then
// channel 1). manifest.json lists the cases with their sample rate, channels, frames and parameters, and cases.tsv the
// same for the C++ test (tab separated: name, fs, channels, frames, threshold, ratio, attack, release).
// The same input also runs in blocks of 1 and 1000 samples, which must give the same output: the C++ side may then be
// compared with the 128-sample golden at any block length of the host.
// Node runs without flush-to-zero, so the inputs hold no denormals and the cases are shorter than 10 s (the GR does not
// decay to a double denormal).
import { build } from 'esbuild'
import { execFileSync } from 'node:child_process'
import { mkdirSync, rmSync, writeFileSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'

const here = dirname(fileURLToPath(import.meta.url))
const mix = (process.argv[2] ?? 'D:/work/Claude/mix-course').replaceAll('\\', '/')
const bundled = await build({ entryPoints: [`${mix}/src/dsp.ts`], bundle: true, format: 'esm', platform: 'node', write: false })
const { CompressorCore, RATIO_INF } = await import(
  'data:text/javascript;base64,' + Buffer.from(bundled.outputFiles[0].text).toString('base64'))

const f = Math.fround

// Deterministic noise: an LCG's top 24 bits, exact in float32.
function noise(seed) {
  let s = seed >>> 0
  return () => {
    s = (Math.imul(s, 1664525) + 1013904223) >>> 0
    return ((s >>> 8) - 8388608) / 8388608
  }
}

const signals = {
  // A 1 kHz sine whose level steps -30, -6, -18, 0 dBFS every quarter.
  steps: (fs, frames, ch) => (i) => {
    const db = [-30, -6, -18, 0][Math.min(3, Math.floor((4 * i) / frames))]
    return 10 ** (db / 20) * Math.sin((2 * Math.PI * 1000 * i) / fs + ch * 0.7)
  },
  noise: (fs, frames, ch) => {
    const next = noise(1234 + ch)
    return (i) => 0.5 * next() * (i < frames / 2 ? 1 : 0.1)
  },
  // Decaying hits every 150 ms (a drum's shape: a 200 Hz tone with a 40 ms decay).
  hits: (fs) => (i) => {
    const t = (i % Math.round(0.15 * fs)) / fs
    return 0.9 * Math.exp(-t / 0.04) * Math.sin(2 * Math.PI * 200 * t)
  },
  // Silence, then one hit: the GR starts from rest.
  silenceHit: (fs, frames) => (i) => {
    const start = Math.floor(frames / 2)
    if (i < start) return 0
    const t = (i - start) / fs
    return 0.8 * Math.exp(-t / 0.05) * Math.sin(2 * Math.PI * 120 * t)
  },
}

const cases = []
const add = (signal, fs, channels, threshold, ratio, attack, release, seconds = 0.25) =>
  cases.push({ signal, fs, channels, threshold, ratio, attack, release, frames: Math.round(seconds * fs) })

for (const fs of [44100, 48000, 96000]) {
  add('steps', fs, 1, -20, 4, 10, 100)
  add('steps', fs, 2, -12, RATIO_INF, 0, 0)
  add('noise', fs, 2, -18, 2, 1, 50)
  add('hits', fs, 1, -24, 8, 0.1, 5)
  add('silenceHit', fs, 2, -30, 20, 100, 1000)
}
add('steps', 48000, 2, 0, 4, 10, 100)          // the defaults: nothing goes over 0 dBFS
add('steps', 48000, 1, -60, 1, 10, 100)        // 1:1 takes nothing away
add('noise', 48000, 1, -40, RATIO_INF, 0.1, 0) // a limiter with the fastest release
add('hits', 48000, 2, -6, 3, 30, 300)
add('hits', 44100, 2, -36, 12, 0, 1000)
add('steps', 96000, 2, -48, 1.5, 100, 5)
add('silenceHit', 44100, 1, -10, 6, 0, 0)

function render(c, input, block) {
  const core = new CompressorCore(c.fs)
  const out = input.map((ch) => Float32Array.from(ch))
  for (let start = 0; start < c.frames; start += block) {
    const end = Math.min(c.frames, start + block)
    core.process(out.map((ch) => ch.subarray(start, end)), f(c.threshold), f(c.ratio), f(c.attack), f(c.release))
  }
  return out
}

const dir = join(here, 'comp')
rmSync(dir, { recursive: true, force: true })
mkdirSync(dir, { recursive: true })
const planar = (chs) => {
  const all = new Float32Array(chs.length * chs[0].length)
  chs.forEach((ch, k) => all.set(ch, k * ch.length))
  return Buffer.from(all.buffer)
}
const listed = []
let bytes = 0
cases.forEach((c, k) => {
  const name = `case-${String(k).padStart(2, '0')}`
  const input = Array.from({ length: c.channels }, (_, ch) => {
    const gen = signals[c.signal](c.fs, c.frames, ch)
    return Float32Array.from({ length: c.frames }, (_, i) => gen(i))
  })
  const out = render(c, input, 128)
  for (const block of [1, 1000]) {
    const other = render(c, input, block)
    other.forEach((ch, n) => {
      for (let i = 0; i < c.frames; i++)
        if (!Object.is(ch[i], out[n][i])) throw new Error(`${name}: block ${block} differs at ${n}:${i}`)
    })
  }
  const inBuf = planar(input)
  const outBuf = planar(out)
  writeFileSync(join(dir, `${name}.in.f32`), inBuf)
  writeFileSync(join(dir, `${name}.out.f32`), outBuf)
  bytes += inBuf.length + outBuf.length
  listed.push({ name, ...c, threshold: f(c.threshold), ratio: f(c.ratio), attack: f(c.attack), release: f(c.release) })
})

const git = (...args) => execFileSync('git', ['-C', mix, ...args], { encoding: 'utf8' }).trim()
const manifest = {
  generator: 'tests/golden/gen-comp.mjs',
  source: 'mix-course src/dsp.ts CompressorCore, blocks of 128 samples as src/compressor.worklet.ts',
  commit: git('rev-parse', 'HEAD'),
  sourceChanged: git('status', '--porcelain', '--', 'src/dsp.ts', 'src/compressor.worklet.ts') !== '',
  node: process.version,
  layout: 'float32 little endian, planar (channel after channel)',
  cases: listed,
}
writeFileSync(join(dir, 'manifest.json'), JSON.stringify(manifest, null, 2) + '\n')
const row = (c) => [c.name, c.fs, c.channels, c.frames, c.threshold, c.ratio, c.attack, c.release].join('\t')
writeFileSync(join(dir, 'cases.tsv'), listed.map(row).join('\n') + '\n')
console.log(`comp: ${listed.length} cases, ${(bytes / 1e6).toFixed(1)} MB, from mix-course ${manifest.commit}`)
