# Lesson EQ / Lesson Comp 開発の記録

## 現在地

- 工程: 3. 実装（周 3）
- 周: 3
- この周の一切れ: UI（PRAYROOM のウィンドウ、EQ とコンプの画面、スペクトルと音量の流れ、札と 2 段の言葉、説明の欄、設定のメニュー）
- 次にすること: core にスペクトルと音量の流れの取り出し口を作り、`plugin/web` に Web アプリの画面を写して、WebView のエディターでつなぐ

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
| AC-01 | ビルド | 1 | 合格 | `tools\build.cmd Release`・`Debug` とも終了コード 0、`Lesson EQ.vst3`・`Lesson Comp.vst3` が両方にある。Debug でも試験・validator・pluginval（5）が合格 | `.ai-cache/logs/` |
| AC-02 | validator | 1 | 合格 | `-e` で 2 本とも 537 件合格、失敗 0（Release）。周 2 の DSP を入れたあとも同じ | `.ai-cache/validate/*-validator.txt` |
| AC-03 | pluginval | 1・4 | 保留 | strictness 5 で 2 本とも合格（Release。周 2 の DSP を入れたあとも合格）。strictness 10 は周 4 | `.ai-cache/validate/*-pluginval.txt` |
| AC-04 | パラメータの表 | 1 | 合格 | `plugin_tests "[params]"`: ID・名前・短い名前・並び・範囲・既定値・既定値の文字が要件の表と一致 | `.ai-cache/logs/plugin_tests-rel.txt` |
| AC-05 | 正規化 | 1 | 合格 | fromNorm の差の最大 3.6e-12（17696 点、刻みと切れ目は完全一致）、toNorm 2.2e-16（14621 点）、値の文字 22475 個一致。JUCE のパラメータは float でちょうど表せる 3227 点・206 点で `Math.fround` と完全一致 | 同上と `core_tests` |
| AC-06 | EQ の一致 | 2 | 合格 | Edge 156 の `OfflineAudioContext` の 69 件（44.1・48・96 kHz、モノとステレオ）と、ブロック 128・1・4096 のどれでも差の最大 0。421,888 サンプルが全部ビット単位で一致 | `core_tests "[golden-eq]"` |
| AC-07 | コンプの一致 | 2 | 合格 | Node の `CompressorCore` の 22 件と、ブロック 128・1・480 のどれでも差の最大 0。529,275 サンプルが全部ビット単位で一致 | `core_tests "[golden-comp]"` |
| AC-08 | Web アプリの試験の数値 | 2 | 合格 | `tests/dsp.test.ts` のコンプの試験を移したもの（4:1 で GR 6 dB、∞:1、Attack・Release ごとの 6 dB、Threshold の下は素通し）が全部通る | `core_tests "[comp]"` |
| AC-09 | ブロックの長さ | 2 | 合格 | EQ・コンプのエンジン（Auto Gain・wet・dry 込み）で、ブロック 1・64・128・480・ばらばら（1〜4096）が 4096 の出力とビット単位で一致（3 レート、モノとステレオ、Auto Gain のオン・オフ） | `core_tests "[blocks]"` |
| AC-10 | Auto Gain の式 | 2 | 合格 | Python の参照 61 行との差の最大 5.9e-5 dB。例（ベル 300 Hz +12 dB で −3.08、HS 8 kHz +6 dB で −2.14、HP 80 Hz 24 dB/oct で +0.20、コンプ −28 dB・4:1 で +10.5）は ±0.01 dB 以内。オフのとき EQ のエンジンは `Eq` だけとビット単位で同じ | `core_tests "[autogain]"`、`"[autogain-off]"` |
| AC-11 | 平滑化 | 2 | 合格 | `Smoother` は式とビット単位で一致、指数との差は 2^-23/c 以下。EQ の Freq・Gain・Q、HP・LP のクロスフェード、makeup・wet・dry、コンプの Threshold・Ratio は参照との差 1e-6 以下、クロスフェードの重みも 1e-6 以下（出ていくレーンの Freq が動く場合、移っている途中の 2 回目の変更、オフのバンドの Slope の変更を含む）。Attack・Release は平滑化しない。パラメータからエンジンの設定への写しは、既定値以外の値でプロセッサーとエンジンの出力がビット単位で一致（`plugin_tests "[mapping]"`） | `core_tests "[smooth]"` |
| AC-12 | バイパス | 2 | 合格 | バイパスを入れて 100 ms 後から、出力が入力とビット単位で一致（EQ・コンプ、3 レート、モノとステレオ、Auto Gain のオン・オフ）。バイパスを入れて準備したときは最初のサンプルから一致 | `core_tests "[bypass]"` |
| AC-13 | リアルタイム安全 | 2 | 合格 | パラメータを動かしながら 3000 ブロック（1〜1024 サンプル）× 2 本 × 3 条件（48 kHz ステレオ、44.1 kHz モノ、192 kHz ステレオ）で、`processBlock` の中の確保・解放 0 回（Release。Debug は条件を広げる前の 48 kHz ステレオで 0 回）。ロックは読んで確かめた | `rt_alloc_test` |
| AC-14 | 状態の保存 | 1 | 合格 | 乱数の値 20 組 × 2 本で、往復後の値が全部一致（ずれ 0）。`version="1"` あり。状態に無いパラメータは既定値、ほかのプラグインの状態とごみは無視 | `.ai-cache/logs/plugin_tests-rel.txt` |
| AC-15 | 極端な入力 | 2 | 合格 | +24 dBFS のサインとノイズ、無音、denormal、大きな音のあとの無音、ブロック 1・480、44.1〜192 kHz、極端な設定と毎ブロックの設定の跳びで、出力に NaN・Inf なし。Inf・NaN そのものの入力は条件の外（レビュー 02 の 1・2、承認待ち） | `core_tests "[extreme]"` |
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
| 2 | DSP（ビクアッドの EQ、コンプ、Auto Gain、平滑化、バイパス）とゴールデン | 合格（AC-06〜13・15 合格、AC-02・03 は DSP 入りでも合格）。レビューは `reviews/02.md`。Inf・NaN の入力からの立ち直りは承認待ち（patch あり） | — | 2026-10-31 |

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

- 最初の `tools\build.cmd Release`（run-check で包んだもの）が、構成のあと何も出さずに終了コード 1 で終わった。同じコマンドをもう一度流すと通り、その後は再現しない。原因は分かっていない（2026-10-31）。

## ユーザーの作業待ち

（DAW での手動確認など、セッションの中で終わらないもの）
