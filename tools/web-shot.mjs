// Shoots plugin/web/dev/ (the page with the fake plug-in) to PNG with a headless Edge, one file per state, for looking at
// every variant of the screen without a host (AC-20). Vite serves the page on 127.0.0.1 for the run only.
// Usage: node tools/web-shot.mjs <output folder> [name=query ...]
//   With no states, the standard set: both plug-ins in the three themes, the Hint off, no audio, and the EQ's bands and
//   the compressor's words in their kinds. A state is a file name and the query of dev/index.html (fake-juce.ts).
// EDGE overrides the browser's path; SCALE the device scale factor (default 1).
import { spawn } from 'node:child_process'
import { mkdirSync, mkdtempSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { createServer } from 'vite'

const repo = resolve(fileURLToPath(new URL('..', import.meta.url)))
const edge = process.env.EDGE ?? 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'
const scale = process.env.SCALE ?? '1'
const [outArg, ...given] = process.argv.slice(2)
if (!outArg) {
  console.error('usage: node tools/web-shot.mjs <output folder> [name=query ...]')
  process.exit(2)
}
const out = resolve(outArg)
mkdirSync(out, { recursive: true })

const standard = []
for (const theme of ['light', 'dark', 'studio']) {
  standard.push([`eq-${theme}`, `theme=${theme}&set=b1_gain:6,b2_gain:-4,hs_gain:3`])
  standard.push([`comp-${theme}`, `plugin=comp&theme=${theme}&set=comp_threshold:-24,comp_ratio:4,comp_attack:3,comp_release:200`])
}
for (const [band, set] of [[0, 'hp_on:1,hp_slope:3'], [1, 'ls_gain:-6'], [2, 'b1_gain:9,b1_q:4'], [5, 'hs_gain:-12'], [6, 'lp_on:1,lp_freq:6000']])
  standard.push([`eq-band${band}`, `band=${band}&set=${set}`])
standard.push(['eq-silent', 'playing=0'], ['eq-bypass', 'set=bypass:1,b2_gain:6'], ['eq-96k', 'rate=96000&set=b2_gain:6'])
standard.push(['comp-words-none', 'plugin=comp&set=comp_ratio:1.2,comp_attack:50,comp_release:40'])
standard.push(['comp-bracket', 'plugin=comp&set=comp_threshold:-20,comp_ratio:4,comp_attack:30,comp_release:100'])
standard.push(['comp-inf', 'plugin=comp&set=comp_threshold:-30,comp_ratio:1000,comp_attack:0,comp_release:0'])
standard.push(['comp-silent', 'plugin=comp&playing=0'], ['comp-hints-off', 'plugin=comp&hints=0'])
const states = given.length ? given.map((g) => [g.slice(0, g.indexOf('=')), g.slice(g.indexOf('=') + 1)]) : standard

const server = await createServer({ configFile: join(repo, 'plugin/web/vite.config.ts'), server: { host: '127.0.0.1', port: 0 }, logLevel: 'error' })
await server.listen()
const port = server.httpServer.address().port
const profile = mkdtempSync(join(tmpdir(), 'lesson-shot-'))
try {
  for (const [name, query] of states) {
    const url = `http://127.0.0.1:${port}/dev/index.html?${query}`
    const png = join(out, `${name}.png`)
    // not spawnSync: Vite serves the page from this process's event loop
    const status = await new Promise((done) => {
      const child = spawn(edge, ['--headless=new', '--disable-gpu', '--hide-scrollbars', '--no-first-run', '--no-default-browser-check',
        `--user-data-dir=${profile}`, `--force-device-scale-factor=${scale}`, '--window-size=960,560', '--virtual-time-budget=5000',
        `--screenshot=${png}`, url], { stdio: 'ignore' })
      const timer = setTimeout(() => child.kill(), 60000)
      child.on('exit', (code) => (clearTimeout(timer), done(code)))
    })
    console.log(`${name}: ${status === 0 ? png : `edge exited ${status}`}`)
  }
} finally {
  await server.close()
  rmSync(profile, { recursive: true, force: true })
}
