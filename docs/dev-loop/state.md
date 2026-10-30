# Lesson EQ / Lesson Comp 開発の記録

## 現在地

- 工程: 3. 実装（周 2）
- 周: 2
- この周の一切れ: DSP（Chrome と同じビクアッドの EQ、CompressorCore の移植、Auto Gain の式、平滑化、バイパス）とゴールデンとの一致
- 次にすること: fdlibm の写しと WebBiquad、Smoother、Eq、Compressor、AutoGain を core に書き、ゴールデン（Node のコンプ、Edge の EQ）と比べる

## 周の予定

| 周 | 一切れ |
| --- | --- |
| 1 | 素通しの 2 本。パラメータの表、値の文字、状態の保存。validator と pluginval（strictness 5） |
| 2 | DSP: EQ（Chrome と同じ係数と精度）、コンプ（`CompressorCore` の移植）、Auto Gain の式、平滑化、バイパス。ゴールデンとの一致の試験 |
| 3 | UI: PRAYROOM のウィンドウ、EQ とコンプの画面、スペクトルと音量の流れ、札と 2 段の言葉、説明の欄、設定のメニュー |
| 4 | 仕上げ: pluginval strictness 10、validator `-e`、EditorHost、README、DAW の確認表 |

## 受け入れ条件の状態

| 番号 | 条件 | 周 | 状態 | 測った値 | ログ |
| --- | --- | --- | --- | --- | --- |
| AC-01 | ビルド | 1 | 合格 | `toolsuild.cmd Release`・`Debug` とも終了コード 0、`Lesson EQ.vst3`・`Lesson Comp.vst3` が両方にある。Debug でも試験・validator・pluginval（5）が合格 | `.ai-cache/logs/` |
| AC-02 | validator | 1 | 合格 | `-e` で 2 本とも 537 件合格、失敗 0（Release） | `.ai-cache/validate/*-validator.txt` |
| AC-03 | pluginval | 1・4 | 保留 | strictness 5 で 2 本とも合格（Release）。strictness 10 は周 4 | `.ai-cache/validate/*-pluginval.txt` |
| AC-04 | パラメータの表 | 1 | 合格 | `plugin_tests "[params]"`: ID・名前・短い名前・並び・範囲・既定値・既定値の文字が要件の表と一致 | `.ai-cache/logs/plugin_tests-rel.txt` |
| AC-05 | 正規化 | 1 | 合格 | fromNorm の差の最大 3.6e-12（17696 点、刻みと切れ目は完全一致）、toNorm 2.2e-16（14621 点）、値の文字 22475 個一致。JUCE のパラメータは float でちょうど表せる 3227 点・206 点で `Math.fround` と完全一致 | 同上と `core_tests` |
| AC-06 | EQ の一致 | 2 | 未着手 | | |
| AC-07 | コンプの一致 | 2 | 未着手 | | |
| AC-08 | Web アプリの試験の数値 | 2 | 未着手 | | |
| AC-09 | ブロックの長さ | 2 | 未着手 | | |
| AC-10 | Auto Gain の式 | 2 | 未着手 | | |
| AC-11 | 平滑化 | 2 | 未着手 | | |
| AC-12 | バイパス | 2 | 未着手 | | |
| AC-13 | リアルタイム安全 | 2 | 未着手 | | |
| AC-14 | 状態の保存 | 1 | 合格 | 乱数の値 20 組 × 2 本で、往復後の値が全部一致（ずれ 0）。`version="1"` あり。状態に無いパラメータは既定値、ほかのプラグインの状態とごみは無視 | `.ai-cache/logs/plugin_tests-rel.txt` |
| AC-15 | 極端な入力 | 2 | 未着手 | | |
| AC-16 | 画面の言葉 | 3 | 未着手 | | |
| AC-17 | EQ のカーブ | 3 | 未着手 | | |
| AC-18 | スペクトル | 3 | 未着手 | | |
| AC-19 | UI がネットなしで開く | 3 | 未着手 | | |
| AC-20 | 画面のバリエーション | 3 | 未着手 | | |
| AC-21 | ジェスチャー | 3 | 未着手 | | |
| AC-22 | 設定のメニューの保存 | 3 | 未着手 | | |
| AC-23 | DAW での手動確認 | 4 | 未着手 | | |

状態は、未着手、合格、不合格、保留のどれか。

## 周の記録

| 周 | 一切れ | 結果 | 戻った先と理由 | 日付 |
| --- | --- | --- | --- | --- |
| 1 | 素通しの 2 本、パラメータの表、値の文字、状態、validator と pluginval（5） | 合格（AC-01・02・04・05・14 合格、AC-03 は strictness 5 まで合格）。レビューは `reviews/01.md` | — | 2026-10-31 |

## 決定の記録

| 日付 | 決めたこと | ほかの案 | 理由 | 承認 |
| --- | --- | --- | --- | --- |
| 2026-10-31 | JUCE 9.0.2＋WebView2 | nih-plug | PRAYROOM の部品と Web アプリの画面のコードをそのまま使える。手元に subbass-vst の先例とパッチ済みの JUCE がある | ユーザー |
| 2026-10-31 | EQ とコンプを 2 本に分ける | 1 本にまとめる | DAW の慣習。片方だけ挿す・順番を入れ替えるができる | ユーザー |
| 2026-10-31 | VST3 だけ | VST3＋CLAP | Studio Pro は VST3 で読む。JUCE 9.0.2 に CLAP の出力がない | ユーザー |
| 2026-10-31 | 札、2 段の言葉、Auto Gain、スペクトル、音量の流れ、説明の欄を持っていく。説明の欄はメニューで切り替え | 一部だけ | ユーザーの選択 | ユーザー |
| 2026-10-31 | Auto Gain は設定から計算する。コンプは 0 dBFS での GR の半分、EQ はピンクノイズを K 特性で | 再生しながら測る、常に追いかける。コンプは GR の全部、EQ はカーブの dB の平均 | プラグインは曲全体を先に読めない。一般的なプラグインの作り | ユーザー |
| 2026-10-31 | PRAYROOM の機能は拡大率・テーマ・ダブルクリックで初期値・値の入力まで。プリセットと A/B は入れない | 決まりどおり全部 | ユーザーの選択 | ユーザー |
| 2026-10-31 | 名前は Lesson EQ と Lesson Comp | Primer、Manabi、Starter | ユーザーの選択 | ユーザー |
| 2026-10-31 | 要件を確定。behavior.md との食い違いは「音としてより良い方で」 | — | ユーザーの指示 | ユーザー |
| 2026-10-31 | 設計を確定（Chrome と同じビクアッド、CompressorCore の移植、Auto Gain の式、平滑化、HP・LP のクロスフェード） | — | design.md | ユーザー |

## 未解決の問題

- 最初の `toolsuild.cmd Release`（run-check で包んだもの）が、構成のあと何も出さずに終了コード 1 で終わった。同じコマンドをもう一度流すと通り、その後は再現しない。原因は分かっていない（2026-10-31）。

## ユーザーの作業待ち

（DAW での手動確認など、セッションの中で終わらないもの）
