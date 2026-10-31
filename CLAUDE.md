# mix-course-plugin

ミックス練習の Web アプリ（`D:\work\Claude\mix-course`）の EQ とコンプを、VST3 プラグイン 2 本（Lesson EQ、Lesson Comp）にしたもの。開発の記録は `docs/dev-loop/`（要件、設計、周の記録）にある。

## 守ること

- 音は Web アプリと同じにする。DSP を変えたら、ゴールデンとの一致の試験（`tests/golden/`）を通す。音が変わる近似や仕様の変更は、ユーザーの承認を取ってから入れる。
- `D:\work\Claude\mix-course` は読むだけ。何も書き込まない、コミットしない。ゴールデンを作り直すときも、出力はこのリポジトリに置く。
- オーディオスレッドでは、メモリの確保と解放、ロック、I/O、ログをしない。
- パラメータの ID は一度出したら変えない。

## UI のデザインルール

UI は PRAYROOM のデザインシステムに従う。

- 見た目の決まりは `~/.claude/skills/prayroom/README.md`、操作と状態の振る舞いは `~/.claude/skills/prayroom/behavior.md` を読んでから UI を作る。
- CSS では `tokens.css` のカスタムプロパティ（`var(--surface-000)` など）を使い、値を直接書かない。
- `plugin/web/design-system/` は PRAYROOM（デザインシステム）の写しで、`tools/sync-prayroom.py` で作る。中は手で書き換えない。
- 書体（Zen Kaku Gothic New、IBM Plex Mono）の woff2 は同梱する。プラグインはネットにつながなくても動く。

## ビルド

- `tools\build.cmd Release`（または `Debug`）。MSVC の環境は `tools\msvc.cmd` が用意する。`.bat`・`.cmd` は Bash から `cmd //c` で呼ぶ。
- JUCE は `D:/work/Claude/_sdk/JUCE/JUCE-subbass`（JUCE 9.0.2 に WebView2 の文字の拡大のパッチを当てた worktree。subbass-vst と共用）。
