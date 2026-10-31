# Lesson EQ / Lesson Comp

ミックス練習の Web アプリ（`D:\work\Claude\mix-course`）の EQ とコンプを、Windows の VST3 プラグイン 2 本にしたもの。音は Web アプリと同じ（ゴールデンとの一致を試験で確かめている）。画面は Web アプリの画面を WebView2 で開き、PRAYROOM のデザインシステムで描く。

| プラグイン | 中身 |
| --- | --- |
| Lesson EQ | HP・ローシェルフ・ベル 3 本・ハイシェルフ・LP の 7 バンド。Auto Gain、入力と出力のスペクトル、周波数の帯の言葉 |
| Lesson Comp | Threshold・Ratio・Attack・Release。Auto Gain、入出力のグラフ、音量の流れ、札と 2 段の言葉 |

自分用で、配布はしない。開発の記録は `docs/dev-loop/`（要件、設計、周の記録、レビュー、DAW での確認表）にある。

## 要るもの

- Windows 11（x64）と WebView2 ランタイム（Windows 11 には入っている。無いときは、プラグインは音を通し、窓には案内の 1 行だけが出る）
- Visual Studio 18 Insiders の C++（MSVC）。`tools\msvc.cmd` が `vcvars64.bat` を呼ぶ。ほかの版なら `tools\msvc.cmd` と `tools\build.cmd` の `VCVARS` を直す
- CMake 3.22 以上、Node.js 20 以上
- JUCE 9.0.2 に `third_party/juce-patches/` の 2 本のパッチを当てたもの。既定の場所は `D:/work/Claude/_sdk/JUCE/JUCE-subbass`（subbass-vst と共用）。作り方は `third_party/juce-patches/README.md`。別の場所なら `cmake -DLESSON_JUCE_DIR=<path>` で渡す

## ビルド

最初に 1 回だけ、Node のパッケージを入れる。

```bat
npm ci
```

ビルドは次の 1 行。画面（`plugin/web`）をビルドしてから、C++ をビルドする。ビルドした画面はプラグインのバイナリに入る。

```bat
tools\build.cmd Release
```

できるもの（`Debug` なら `build-dbg`）:

- `build-rel\plugin\LessonEQ_artefacts\Release\VST3\Lesson EQ.vst3`
- `build-rel\plugin\LessonComp_artefacts\Release\VST3\Lesson Comp.vst3`

## 試験

```bat
cd build-rel
ctest -j 8
```

`core_tests`（DSP とゴールデンとの一致）、`plugin_tests`（パラメータ、状態、Bridge、設定）、`rt_alloc_test`（オーディオスレッドで確保しないこと）が走る。画面の言葉が Web アプリと同じことは `npm run web:test` で確かめる。

形式の検証は、Steinberg の validator と pluginval を 2 本に流す（ログは `.ai-cache\validate\`）。2 つ目の引数は pluginval の strictness。

```bat
tools\validate.cmd Release 10
```

## インストール

DAW を閉じてから、次を実行する。管理者の確認が出る。2 本を `C:\Program Files\Common Files\VST3\` に写す。`--dry-run` を付けると、写さずに確かめだけをする。

```bat
tools\install-vst3.cmd
```

DAW でプラグインの一覧を読み直すと、Lesson EQ と Lesson Comp が出る（作り手の名前は Personal）。Ableton Live 12 では、Settings（Ctrl+,）の Plug-Ins で VST3 のシステムのフォルダを使う切り替えを On にして、Rescan を押す。外すときは、`C:\Program Files\Common Files\VST3\` の `Lesson EQ.vst3` と `Lesson Comp.vst3` を消す。

DAW での確認の手順（Ableton Live 12）は `docs/dev-loop/manual-checks.md` にある。

## プラグインが書くもの

| 場所 | 中身 |
| --- | --- |
| `%APPDATA%\PRAYROOM\settings.json` | 窓の拡大率・テーマ（PRAYROOM のプラグインで共用）、説明の欄の表示（`lesson.hint`）。セッションには入らない。知らないキーは消さない。JSON のオブジェクトとして読めないファイルは上書きしない（既定の設定で開き、変えた設定は残らない）。消すと既定に戻る |
| `%LOCALAPPDATA%\PRAYROOM\Lesson\WebView2` | WebView2 のデータ（2 本で共用） |

つまみの値はセッション（Live なら Live Set）に入る。

## ゴールデンを作り直す

Web アプリが変わったときだけ作り直す。作り方はそれぞれのファイルの頭に書いてある。Web アプリは読むだけで、出力はこのリポジトリの `tests/golden/` に置く。

| ファイル | 作るもの | 使うもの |
| --- | --- | --- |
| `tests/golden/gen-params.mjs` | パラメータの正規化と値の文字（`params.tsv`） | Node |
| `tests/golden/gen-eq.mjs` | EQ の出力（`eq/`） | Edge |
| `tests/golden/gen-comp.mjs` | コンプの出力（`comp/`） | Node |
| `tests/golden/gen-autogain.py` | EQ の Auto Gain（`autogain.tsv`） | Python |
| `tests/golden/gen-spectrum.mjs` | スペクトル（`spectrum/`） | Edge |
| `tests/golden/gen-curve.mjs` | 画面の EQ のカーブ（`curve/`） | Edge |
| `tests/golden/gen-ui.mjs` | 画面の言葉（`ui.json`） | Node |

## ライセンス

自分用で配布しないので、下の条件は配布するときに要るもの。

| もの | ライセンス | 置き場 |
| --- | --- | --- |
| JUCE 9.0.2 | AGPLv3 か、JUCE 9 の EULA（商用ライセンス）の二本立て | JUCE の `LICENSE.md` |
| VST3 SDK（JUCE に同梱） | MIT（Steinberg、2025） | JUCE の `modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt` |
| fdlibm（`expf`・`exp`・`sin`・`cos` を写したもの） | Sun Microsystems の告知を残す条件で自由 | `third_party/fdlibm/LICENSE` |
| Zen Kaku Gothic New、IBM Plex Mono（使う文字だけに絞った WOFF） | SIL Open Font License 1.1 | `plugin/web/fonts/OFL-*.txt` |
| Preact | MIT | `node_modules/preact/LICENSE` |
| doctest（試験だけ） | MIT | `third_party/doctest/LICENSE.txt` |

PRAYROOM のデザインシステムと、写した Web アプリの画面のコードは自分のもの。
