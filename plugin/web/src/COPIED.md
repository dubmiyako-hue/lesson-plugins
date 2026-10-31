# Files copied from the web app

The screens are the web app's own code, copied from `D:\work\Claude\mix-course` at commit `090a107`. The copies keep
the web app's line endings (CRLF). When the web app changes one of these files, copy it again and redo the edits below.
`plugin/web/tests/copied.test.ts` (AC-16) fails when a copied constant no longer matches the web app.

| File | Edits |
| --- | --- |
| `params.ts` | none |
| `character.ts` | none |
| `dsp.ts` | none |
| `ui/Glyph.tsx` | none |
| `ui/hooks.ts` | none |
| `ui/PluginHeader.tsx` | none (only `AutoGain` is used; the plug-in's window has its own header) |
| `chain.ts` | `butterworthQs` imported from `./dsp` alone; the worklet loading, `StemChain` and `HISTORY_S` removed (the plug-in has no Web Audio graph) |
| `ui/Knob.tsx` | gestures (`../gestures`): a drag from press to release, wheel and keys until 250 ms without a step, a reset or a typed value complete; `typeValue`: a click on the value opens a field read by the plug-in's `parseValue` |
| `ui/Eq.tsx` | the curve's handle drag and the wheel run in gestures of their own (`graphGestures`), and a pointer cancel ends the drag; `PluginHeader` left out |
| `ui/Comp.tsx` | the Threshold drag on the level history runs in a gesture, and a pointer cancel ends it; the GR hold and the history's lag in time (`holdOf`, `lagMaxOf`) rather than in points at 48 kHz; `PluginHeader` and an unused `set` left out |
| `ui/Hint.tsx` | `setHintsOn`: the Hint can be turned off from the settings |

`app.ts` is not a copy: it gives the copied screens the web app's `app.ts` API (`state`, `subscribe`, the setters,
`player`) over the plug-in's bridge (`host.ts`). `style.css` holds the web app's base and plug-in rules verbatim, then the
plug-in's own.
