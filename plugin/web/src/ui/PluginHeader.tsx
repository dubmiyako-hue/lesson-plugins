import { setPop, toggleAuto, togglePlugin, type Plugin } from '../app'
import { fmtDb } from '../params'
import { hint } from './Hint'
import { useApp } from './hooks'
import { Lamp, Switch } from './Knob'

export const AUTO_GAIN_TIP = 'このプラグインで変わった音量（平均の大きさ）を自動で元に戻す。戻した量が下の数字。人は大きい音のほうが良い音に聞こえやすいので、音色の変化だけを比べられる。'
const BYPASS_TIP = 'このプラグインを通すかどうか。オフにすると、かける前の元の音になる。切り替えて、かける前と後を聞き比べられる。'

// The power switch, then the plugin's name; at the right the button that opens the plugin
// large. The track it works on is the one selected in the timeline.
export function PluginHeader({ name, on, plugin, big }: { name: string; on: boolean; plugin: Plugin; big?: boolean }) {
  return (
    <div class="pr-device__head">
      <Switch label={`${name} Bypass`} on={on} tip={BYPASS_TIP} onChange={() => togglePlugin(plugin)} />
      <span class="pr-device__name">{name}</span>
      <button
        class="pr-key pr-key--icon pr-focusable"
        aria-label={big ? '閉じる' : '大きく開く'}
        onClick={() => setPop(big ? null : plugin)}
        {...(big ? hint('閉じる', '元の大きさに戻す。Esc キーでも閉じる。') : hint('大きく開く', 'このプラグインを大きく開く。細かく触りたいときに開く。'))}
      >
        <i class={`pr-icon pr-icon--${big ? 'close' : 'expand'}`} />
      </button>
    </div>
  )
}

// Auto Gain as one more column of the plugin's controls: its name, its switch where a
// knob would be, and the gain it puts back where a knob shows its value
export function AutoGain({ name, plugin, big }: { name: string; plugin: Plugin; big?: boolean }) {
  const s = useApp()
  const i = s.selected
  const auto = plugin === 'eq' ? s.settings[i].eqAuto : s.settings[i].compAuto
  const db = s.makeup[i]?.[plugin] ?? 0
  return (
    <div class={`pr-ctl ag ${auto ? 'on' : ''} ${s.measuring[i] ? 'busy' : ''}`} {...hint('Auto Gain', AUTO_GAIN_TIP)}>
      <div class="pr-ctl__label">Auto Gain</div>
      <div class="agkey">
        <Lamp label={`${name} Auto Gain`} pressed={auto} big={big} onChange={() => toggleAuto(plugin)} />
      </div>
      <div class="pr-ctl__value">{fmtDb(db)}</div>
    </div>
  )
}
