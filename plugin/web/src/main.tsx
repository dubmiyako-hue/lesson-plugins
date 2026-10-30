// The page of both plug-ins: the plug-in says which it is (ready), and the window shows the web app's EQ or compressor,
// large. PRAYROOM first (tokens → component styles → components), then the page's own styles.
import '../prayroom/tokens.css'
import '../prayroom/bundle.css'
import '../prayroom/bundle.js'
import './style.css'
import { render } from 'preact'
import { onChange, readout, setBypass, start, state, subscribe } from './app'
import { Comp } from './ui/Comp'
import { Eq } from './ui/Eq'
import { setHintsOn } from './ui/Hint'
import { applyTheme, keepKeyboardWithHost, mountWindow, noContextMenu } from './window'

const TITLES = { eq: 'Lesson EQ', comp: 'Lesson Comp' }

async function main() {
  const ready = await start()
  applyTheme(ready.theme)
  setHintsOn(ready.hints)
  keepKeyboardWithHost()
  noContextMenu()
  const win = mountWindow({ title: TITLES[ready.plugin], ready, onBypass: setBypass })
  onChange((id, value) => win.setReadout(readout(ready.names, id, value)))
  const bypassed = () => (ready.plugin === 'eq' ? state.settings[0].eqBypass : state.settings[0].compBypass)
  win.setBypassed(bypassed())
  subscribe(() => win.setBypassed(bypassed()))
  render(ready.plugin === 'eq' ? <Eq big /> : <Comp big />, win.body)
}

void main()
