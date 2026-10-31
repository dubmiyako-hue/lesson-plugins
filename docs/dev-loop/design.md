# Lesson EQ / Lesson Comp 設計書

- 日付: 2026-10-31
- 状態: 確定（2026-10-31、ユーザーの確定）
- 前提: [requirements.md](requirements.md)（確定済み、AC-01〜AC-23）
- 根拠: Web アプリ（`D:\work\Claude\mix-course` の `src/dsp.ts`、`src/chain.ts`、`src/compressor.worklet.ts`、`src/params.ts`、`src/character.ts`、`src/ui/`）と、Chrome の実装（Blink の `platform/audio/biquad.cc`、`modules/webaudio/biquad_filter_handler.cc`、`modules/webaudio/realtime_analyser.cc`、2026-10-31 に main から読んだ）

## 変更履歴

| 周 | 日付 | 変更 | 理由 |
| --- | --- | --- | --- |
| — | 2026-10-31 | 初版 | 工程 2 |
| 1 | 2026-10-31 | 正規化のゴールデンを `norm.json` から `params.tsv`（タブ区切り、値の文字と JS の丸めも入れる）に替えた | C++ の試験が JSON の読み取りなしで読める（DECISIONS.md） |
| 2 | 2026-10-31 | `sin`・`cos` も fdlibm を写した。HP・LP の使わない段は 350 Hz に置く。平滑化の収束はサンプルごとに調べる。音の全部を core の `EqEngine`・`CompEngine` に置く。時定数の `exp` も fdlibm（レビュー 02） | ビットまで合わせるため、試験しやすくするため（DECISIONS.md、`reviews/02.md`） |
| 3 | 2026-10-31 | 窓は L（960×560）、ページは 1 枚（`index.html`）。拡大率は CSS の `zoom` でなく web view の transform。スペクトルの計算と画面への取り出し口は core（`Spectrum`、`Taps`）に置き、取り出しは窓が開いているあいだだけ。FFT の尺度は `|X|/N` と測った。試験の置き場（AC-16・17・18）を実際のものに直した | DECISIONS.md の 2026-10-31 の周 3 の行 |
| 3 | 2026-10-31 | 窓を開き直したときと準備し直したときに、スペクトルのリングとコンプの点の作りかけを空にする（`restart`）。スペクトルの計算で、NaN・Inf になったビンは 0 として読む。ページでは、1 つのパラメータを持つジェスチャーは 1 つだけ。右クリックのメニューを出さない | レビュー 03（`reviews/03.md`）、DECISIONS.md |
| 4 | 2026-10-31 | 入力の Inf・NaN からの立ち直り: EQ はブロックの終わりにフィルターの状態を調べ、有限でなければ全段の状態を 0 に戻す。コンプの検出器は Inf を float の最大として読む（2.2、2.3） | レビュー 02 の 1・2。ユーザーの承認（DECISIONS.md） |
| 4 | 2026-10-31 | 両方のエンジンで、数でないサンプル（Inf・NaN）は 0 として出す。EQ はフィルターにも 0 を入れる（2.2、2.3）。EQ のカーブのホイールはドラッグとは別のジェスチャーにし、グラフのドラッグはポインターの取り消しでも終える。7 のテールの記述を測った値に直した | `fad038f` のあとのレビュー。ユーザーの依頼（DECISIONS.md、`reviews/04.md`） |
| 4 | 2026-10-31 | ホストの reset で、エンジンを今の設定で準備し直したのと同じ状態に戻す（確保しない）。EQ のテールを設定から求める（7） | レビュー 04 の 4。ユーザーの承認（DECISIONS.md） |

## 1. 信号の流れ

### Lesson EQ

```mermaid
flowchart LR
  IN[入力] --> HP[HP 4 段<br/>レーン A/B]
  HP --> LS[LS] --> B1[Bell 1] --> B2[Bell 2] --> B3[Bell 3] --> HS[HS]
  HS --> LP[LP 4 段<br/>レーン A/B]
  LP --> MK[Auto Gain<br/>×makeup]
  MK --> WET[×wet]
  IN --> DRY[×dry]
  WET --> SUM((+)) --> OUT[出力]
  DRY --> SUM
  LP -. post .-> SPEC[スペクトル]
  IN -. pre .-> SPEC
```

- 13 段のビクアッドは、Web アプリの `EqFilters` と同じ順に直列につなぐ。各段の出力は float に丸める（Web Audio のノードの出力が float32 だから）。
- `post` はビクアッドの後、Auto Gain の前（Web アプリの `post` の analyser と同じ位置）。`pre` は入力。
- wet と dry はバイパスのクロスフェード。Web アプリの `eqWet`・`eqDry` と同じく、足し合わせは float。

### Lesson Comp

```mermaid
flowchart LR
  IN[入力] --> CC[CompressorCore<br/>ピーク 20 ms 保持・dB で Attack/Release]
  CC --> MK[Auto Gain<br/>×makeup]
  MK --> WET[×wet]
  IN --> DRY[×dry]
  WET --> SUM((+)) --> OUT[出力]
  DRY --> SUM
  IN -. 入力のピーク .-> HIST[音量の流れ]
  CC -. 出力のピーク・GR .-> HIST
```

- 音量の流れの点は、256 サンプルごとの [入力のピーク、出力のピーク（Auto Gain の前）、GR の最大]。Web アプリのワークレットの `collect` と同じ中身。
- バイパス中もコアは動かし続ける（Web アプリと同じ。表示もそのまま動く）。

## 2. DSP の選定

### 2.1 Web Audio と同じビクアッド（`WebBiquad`）

Chrome の `Biquad` をそのまま写す。

- 係数の入力（周波数、Q、Gain）は float に丸めてから使う。Web Audio の `AudioParam` は float32 で値を持つから。周波数は `f / (0.5 · fs)` で 0〜1 に直す（double）。
- 係数の式と端の扱いは `biquad.cc` のとおり。lowpass・highpass の Q は dB（`10^(Q/20)` に戻す）。シェルフは S = 1 で Q を使わない。peaking は周波数が 0 か 1 で素通し、Q が 0 で `A²`。正規化は `1 / a0` を掛ける（割らない）。
- 処理は直接形 I。係数と状態（x1, x2, y1, y2）は double、各サンプルの `y` は float に丸め、状態にもその float を入れる（`float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2`）。式の足す順番も同じにする。
- denormal: Chrome はレンダリングの 128 サンプルごとに状態の denormal を 0 にし、レンダリングのスレッドは FTZ・DAZ で動く。プラグインは core の小さな RAII（`lesson::NoDenormals`、MXCSR の FTZ・DAZ を立てる）で常に 0 にする。`processBlock` と単体試験の両方でこれを使う（JUCE の `ScopedNoDenormals` は core から使えないので）。違いが出るのは 1.2e-38 より小さい値だけ。
- `10^x`: Blink の `pow10` は `fdlibm::expf(float(x · ln10))` で、結果は float の精度になる（シェルフ・peaking の A と、lowpass・highpass の Q）。ここは float の丸めが係数に直に入るので、Chromium の `third_party/fdlibm` の `expf` を写す（Sun の fdlibm の許諾。告知を残す）。
- sin・cos も Chrome と同じく fdlibm の double を写す（周 2 で変えた。CRT とは double の最後の 1 ビットが違いうる）。

### 2.2 EQ（`Eq`）

- 段の割り当ては `EqFilters.apply` と同じ。HP・LP は 4 段で、オンのときは前から `slope / 12` 段を highpass・lowpass（Q は `butterworthQs(slope / 6)` を dB にしたもの）、残りとオフのときは 0 dB・Q 1 の peaking（素通し。周波数は BiquadFilterNode の初期値 350 Hz のまま。新しい `EqFilters` に `apply(bands, false)` をかけたときと同じ。鳴っている Web アプリは、オフにした段の周波数を直前の値のまま残すが、素通しの段なので差は 1e-16 程度）。LS・HS・ベルはオフのとき Gain 0 dB。13 段は常に全部動かす（素通しの段も Chrome と同じ計算をするので、出力のビットまで同じになる）。
- 平滑化: Freq・Gain・Q は、それぞれ `setTargetAtTime(v, t, 0.005)` と同じ式で目標へ近づける（2.5）。平滑化している段だけ、サンプルごとに係数を計算し直す（Chrome もオートメーション中はサンプルごとに計算する）。止まっている段は係数を変えない。
- HP・LP のオン・オフと Slope（要件の食い違いの表）: バンドごとに 4 段のレーンを 2 本持つ。切り替えのとき、新しい設定のレーン（状態は 0 から）を立ち上げ、10 ms のレイズドコサイン `w = 0.5 − 0.5·cos(π·t / T)` で古いレーンから移る。移り終わったら古いレーンを止める。移っているあいだに次の切り替えが来たら、移り終わってから始める。周波数の平滑化は両方のレーンにかける。
- Auto Gain の量（2.4）は τ 5 ms で平滑化した float のゲインで、`float(y · makeup)`。続けて `float(· wet)`、`dry` 側は `float(x · dry)`、足して float。Web Audio の GainNode を 2 つ通るのと同じ丸め。
- 入力の Inf・NaN（Web アプリと違うところ）: ブロックの終わりに全段のフィルターの状態を調べ、1 つでも有限でなければ全段の状態を 0 に戻す。Chrome は状態の NaN を消さないので、Web アプリは prepare まで NaN を出し続ける。有限の入力では何もしないので、出力のビットは Web アプリと同じ。
- 数でない入力のサンプルは、フィルターにもバイパスの dry にも 0 として入れる。出すサンプルも、数でなければ 0 にする（フィルターがあふれたとき）。立ち直りはブロックの終わりなので、これが無いとブロックの残りに NaN が出ていた。

### 2.3 コンプ（`Compressor`）

`CompressorCore` を 1 行ずつ写す。

- 検出: 全チャンネルの |x| の最大（float）を 48 サンプルの部分最大にまとめ、`round(0.02·fs / 48)` 個のリングで最大をとる。部分最大とリングは float（float の最大なので、Web アプリの double と同じ値になる）。
- `levelDb = 20·log10(max(level, 1e-9))`、`targetGr`（∞:1 は Ratio ≥ 1000）、`coef = target > gr ? att : rel`、`gr = target + coef·(gr − target)`、`g = 10^(−gr/20)`、`x = float(x · g)`。gr は double。
- パラメータは float に丸める（ワークレットは `Float32Array` で受けるから）。`timeCoef(ms) = ms ≤ 0 ? 0 : exp(−1 / (ms/1000 · fs))`。
- 平滑化（要件の食い違いの表）: Threshold（dB）と `1/Ratio` を τ 5 ms で動かす（∞:1 は 0）。動いているあいだの式は `over − over · invRatio`。止まったら目標の値に合わせ、Web アプリと同じ式（`over − over / ratio`）に戻す。止まっているときのビットが Web アプリと同じになるように。Attack・Release は平滑化しない。係数はブロックの頭で計算する。
- 入力の Inf（Web アプリと違うところ）: 検出器は Inf を float の最大として読む。Web アプリは GR が `inf − inf` で NaN になり、prepare まで NaN を出し続ける。GR は float の最大の音量（約 770 dB）から Release で戻る。有限の入力では出力のビットは Web アプリと同じ。出すサンプルは、数でなければ 0 にする（Inf のサンプルそのものは `Inf · g` になるので）。
- ワークレットは 128 サンプルごとにパラメータを読む。プラグインはホストのブロックごとに読み、平滑化はサンプルごとに進める。設定が止まっていれば、ブロックの長さで音は変わらない（AC-09）。

### 2.4 Auto Gain（`AutoGain`）

- コンプ: `min(24, targetGr(0, T, R) / 2)`。T と R は float に丸めた目標の値。
- EQ: 20 Hz〜20 kHz を対数で 2048 等分した区間の中央の周波数 f_i で、`makeup = clamp(−10·log10( Σ |H(f_i)|²·|K(f_i)|² / Σ |K(f_i)|² ), −24, 24)`。H は効いている段（`EqFilters.active` と同じ。オフのバンド、Gain 0 dB のシェルフ・ベル、HP・LP の使わない段は入れない）の目標の設定の係数（平滑化の途中の値でなく、止まったときの値。レーンの切り替えの途中も新しい設定）、K は `kWeighting(fs)`（Web アプリの係数の式）。2048 点は、100000 点との差が 0.004 dB 以下になる点の数として測って決めた（44.1・48・96 kHz、ベル 20 Hz〜19 kHz・Q 0.3〜12・±18 dB、シェルフ、HP。いちばん厳しいのは 44.1 kHz の 19 kHz・Q 12 のベル）。
- EQ の計算は重い（全段で約 70 万回の演算）ので、オーディオスレッドで少しずつ進める。目標の設定が変わったら計算を始め、入力 1 サンプルにつき 4 点ずつ進める（2048 点は 512 サンプル、48 kHz で約 11 ms）。計算中に設定が変わったら、終わってからもう一度始める。サンプルの数で進むので、ブロックの長さで結果の出る時刻は変わらない。z と |K|² の表は `prepareToPlay` で作る。
- 量が変わったら、τ 5 ms で新しい量へ近づける（Web アプリの `compMakeup.gain` の `setTargetAtTime`）。Auto Gain のスイッチのオン・オフも同じ（オフは 0 dB）。
- 最初の量: `prepareToPlay` で、今のパラメータの 2048 点を一度に計算し、平滑化をその値から始める。少しずつ進めるのは、そのあとの変化だけ。設定を止めた試験（AC-06、AC-09、AC-12）の頭で、量が 0 から動いて見えることはない。
- ホストが停止中に `processBlock` を呼ばないと、そのあいだは EQ の量の計算が進まない。画面の量は再生を始めてから約 11 ms で追いつく（そのままにする）。
- 画面には、スイッチがオフでも計算した量を出す。

### 2.5 平滑化（`Smoother`）

Chrome の `setTargetAtTime` と同じ（Blink の `audio_param_handler.cc` で確かめた）。値と係数は float。`c = float(1 − exp(−1 / (fs · 0.005)))`（`exp` は Blink の `DiscreteTimeConstantForSampleRate` と同じ fdlibm。コンプの `timeCoef` も V8 の `Math.exp` に合わせて fdlibm）、サンプルごとに `v += (target − v) · c`。始めてから 10τ（50 ms）たつか、`|target − v| < 4.54e-5 · |v|`（target が 0 なら `|v| < 4.54e-5`）になったら、target にそろえて止める。止まっている値は目標そのものなので、止まったあとの音は平滑化のない計算と同じになる。Chrome は収束を 128 サンプルごとに調べ、x86 では 4 サンプルずつまとめて進めるが、こちらはサンプルごとに調べて 1 サンプルずつ進める（違うのは動いているあいだの float の丸めだけ）。float の値は、目標の近くで 1 歩が ulp の半分より小さくなると進まなくなり、10τ で目標にそろう（Chrome も同じ）。

### 2.6 スペクトル（画面用。音には関係しない）

AnalyserNode と同じ計算をプラグインのメッセージのスレッドでする。

- オーディオスレッドは、入力（pre）とビクアッドの後（post）を、チャンネルの平均（Web Audio のモノへの足し合わせ。ステレオは 0.5·(L+R)）にして、それぞれのリングバッファ（16384 サンプル、単一の書き手と単一の読み手）に書く。
- 30 Hz のタイマーで、最新の 8192 サンプルに Blackman 窓（α 0.16）をかけて FFT し、`|X|/8192` を前の値と平滑して、dB にして画面へ送る。Web アプリは画面の更新（60 Hz）ごとに平滑 0.8 をかけるので、30 Hz では 0.8² = 0.64 にして、1 秒あたりの減り方を同じにする。
- 画面へ送るのは 20 kHz・1/24 オクターブ上までのビンの dB（48 kHz で約 3500 個、float の base64）。画面は Web アプリの `spectrum()` をそのまま使って、ピクセルの列ごとに直す。
- FFT の大きさの尺度（Chrome の FFT の出力の倍率）は、Edge で 1 回測ってそろえる（AC-18）。`getFloatFrequencyData` は 0 から平滑を始めるので、比べるときは両方とも平滑を 0 にして、決まったサインとノイズの 8192 サンプルで比べる。測った結果は `|X|/N`（`2/N` にすると 6.02 dB ずれる）。
- 計算は core の `lesson::SpectrumAnalyser`（FFT も平滑した値も double。Chrome の平滑の値は float なので、平滑を重ねると 1e-6 dB ほどずれうる）。入力に NaN・Inf があってそのビンが有限でなくなったら、そのビンは 0 として平滑する（平滑の値に NaN を残さない）。リングは `lesson::SignalRing`（`core/include/lesson/Taps.h`）。大きさが 0 のビンは −300 dB として送る（Chrome は −Infinity）。画面は Web アプリと同じく −130 dB で切る。

## 3. パラメータ表

ID・名前・範囲・既定値は要件のとおり。JUCE の型と受け渡しは次のとおり。

| 種類 | JUCE の型 | 正規化 | 値の文字 | 受け渡し |
| --- | --- | --- | --- | --- |
| Freq・Gain・Q・Threshold・Ratio・Attack・Release | `AudioParameterFloat` | `NormalisableRange` に `toNorm`・`fromNorm` を渡す（`fromNorm` の刻みの丸めも込み。最速と ∞:1 の切れ目も同じ） | `fmt`（Web アプリと同じ書式） | `getRawParameterValue` の atomic |
| On・Auto Gain・Bypass | `AudioParameterBool` | 0・1 | `On`・`Off` | 同上 |
| Slope | `AudioParameterChoice`（12、24、36、48） | 4 択 | `24 dB/oct` | 同上 |

- JUCE の `AudioParameterFloat` は値を float で持つ。音に使う値は `float(fromNorm(n))` で、これは Web アプリの AudioParam が float32 に丸めるのと同じ（音はそろう）。AC-05 は core の `toNorm`・`fromNorm`（double）を 1e-9 で比べ、JUCE のパラメータの値はその `Math.fround` と一致することを別に確かめる。値の文字は float の値から作るので、999.5 Hz のような切れ目のちょうど上では、Web アプリ（double）と最後の桁が違うことがある。文字を作る関数そのものは Web アプリと同じ（AC-04 の文字の試験は関数を比べる）。
- Bypass は `getBypassParameter()` から返す。プラグインがクロスフェードを自分でするので、JUCE の `processBlockBypassed` は使わない（JUCE は `getBypassParameter` があれば `processBlock` を呼び続ける）。
- 値の文字の数字は、JS の `toFixed` と同じ丸め（ちょうど半分は 0 から遠い方）、`Math.round` と同じ丸め（ちょうど半分は大きい方）にする。C++ の `printf` は半分を偶数へ丸めるので使わない。
- 数値の入力: 単位を省いてよい、`k` は 1000 倍、`∞`・`inf` は ∞:1、`最速` は 0、範囲の外は端へ丸める。読めなければ元の値に戻す。

## 4. スレッドモデル

| スレッド | すること |
| --- | --- |
| オーディオ | `processBlock`: パラメータの atomic を読み、DSP、スペクトルのリングと音量の流れの FIFO に書き、Auto Gain の量を atomic に書く |
| メッセージ | `Bridge` の 30 Hz のタイマー: 変わったパラメータ、Auto Gain の量、音量の流れの点、スペクトルを画面へ送る。画面からの操作をパラメータに渡す（ジェスチャーつき）。設定の JSON の読み書き |
| WebView | 画面。Preact。`requestAnimationFrame` で描く |

- オーディオスレッドでは、確保・解放・ロック・I/O・ログをしない。バッファは `prepareToPlay` で最大のブロックの長さに合わせて確保する。
- 受け渡し: 数値は `std::atomic`（lock-free を `static_assert` で確かめる）。音量の流れは単一の書き手と単一の読み手の FIFO（`lesson::CompPoints`: 固定長の配列と atomic の head・tail。満杯なら新しい点を捨てる）。スペクトルのリングと音量の流れには、窓が開いているあいだだけ書く（`Bridge::start` で開き、`stop` で閉じる）。窓が開いてから最初のブロックで、オーディオスレッドがリングと点の作りかけを空にしてから書く（前に開いていたときのものを見せない。開いた回数を atomic で数え、閉じているあいだにブロックが無くても空にする）。スペクトルのリングは書く位置を atomic で持つ（読み手はそこから 8192 サンプル前までを読む。上書きに追い越されることはあるが、表示だけなので許す）。

## 5. 準備とリセット

- `prepareToPlay(fs, maxBlock)`: 係数、平滑化の係数、リングの長さ（`round(0.02·fs / 48)`）、Auto Gain の表（z、|K|²）を作り直し、スペクトルのリングとコンプの点の作りかけを空にし（`restart`。持っていたサンプルは前のサンプルレートのもの）、フィルターの状態と GR を 0 にする。平滑化はパラメータの今の値から始める（開いたときに初期値から動いて見せない）。
- `releaseResources`: 何もしない。

## 6. 状態の形式

- `AudioProcessorValueTreeState` の状態を XML にして `copyXmlToBinary`。根の要素に `version="1"` を付ける。読むときは、版が無いか 1 なら、そのまま読む（ID で読むので、足したパラメータは初期値になる）。
- 拡大率・テーマ・説明の欄の表示は、セッションに入れない。`%APPDATA%\PRAYROOM\settings.json` に `{"version":1,"scale":1.0,"theme":"light","lesson":{"hint":true}}` の形で置く。書くときは、知らないキーを残す（ほかの PRAYROOM のプラグインと共用のファイルなので）。`version` が無ければ 1 を足す。JSON のオブジェクトでないファイルには書かない。`<file>.tmp` に書いてから置き換える。
- 開くとき、保存された拡大率がその画面に収まらなければ、収まるうちでそれ以下の最大で開く（ファイルには書き戻さない）。

## 7. レイテンシー、テール、バイパス

- レイテンシーは 0。EQ のテールは今の設定から求める（`eqTailSeconds`）。段ごとに、いちばん遅い極が 120 dB とその段の最大の利得のぶん下がる時間に 1.25 を掛け、全段で足す。1.25 は、Blink がビクアッドの出力を毎サンプル float に丸めて帰還させるぶんの余裕（1 に近い極が 192 kHz で 7 % ほど遅くなる）。既定では 0。測った鳴り残りと式の比べは `reviews/04.md`。VST3 にはテールが変わったことをホストに知らせる仕組みが無いので、1 回だけ読むホストは最初の値のまま。コンプのテールは 0（GR が戻るのは鳴り残りではない）。
- ホストの reset（VST3 の `setProcessing(false)`。UI か処理のスレッドで、`process` とは重ならない）で、エンジンを今の設定で準備し直したのと同じ状態に戻す。確保はしない（`EqEngine::reset`・`CompEngine::reset`。`prepare` は確保してから reset する）。取り出し口も空にする。
- バイパスは 2.2・2.3 の wet と dry を τ 5 ms で入れ替える。バイパス中もコアは動かす。50 ms（10τ）後には wet = 0、dry = 1 にそろっていて、出力は入力とビット単位で同じ（AC-12 は 100 ms 後から比べる）。

## 8. ファイル構成

```
CMakeLists.txt                 JUCE（JUCE-subbass）、core、plugin、tests
core/                          JUCE に依存しない DSP（lesson::）
  include/lesson/  WebBiquad.h Eq.h Compressor.h AutoGain.h Smoother.h Params.h Format.h Engine.h Spectrum.h Taps.h
  src/
plugin/
  CMakeLists.txt               juce_add_plugin を 2 つ（LessonEQ、LessonComp）と、ページを埋め込むライブラリ
  common/                      LessonProcessor、LessonParameters、WebEditor、Bridge、UiSettings、WebAssetTable.h、embed_web.cmake
  eq/                          EqProcessor
  comp/                        CompProcessor
  web/                         画面のソース（Vite 6＋Preact＋TypeScript、ページは index.html の 1 枚）
    src/                       Web アプリの src を写して、状態の出どころをプラグインに替えたもの（写した表は src/COPIED.md）
    dev/                       偽のプラグイン（fake-juce.ts）。ブラウザだけで画面を開く
    prayroom/                  tools/sync-prayroom.py が作る PRAYROOM の写し
tests/
  golden/                      ゴールデンの作り方（Node と Edge）と、できたデータ
  unit/                        core の試験（doctest）
  plugin/                      殻の試験（パラメータ、値の文字、状態、Bridge）
  rt/                          確保を数える試験
tools/                         build.cmd、msvc.cmd、install-vst3.cmd、sync-prayroom.py、validate.cmd、make-fonts.py、web-shot.mjs
docs/dev-loop/                 要件、設計、記録、レビュー
```

- 画面は Web アプリのコードを写す（`Eq.tsx`、`Comp.tsx`、`Knob.tsx`、`Hint.tsx`、`Glyph.tsx`、`PluginHeader.tsx`、`hooks.ts`、`params.ts`、`character.ts`、`style.css` の要る部分）。`app.ts` の状態は、プラグインの `Bridge` から来る値に替える。写すのは、リポジトリを分けるため（Web アプリのソースをビルドで直接読まない）。写した元とのずれは AC-16 のゴールデンで見張る。
- 書体は Web アプリと同じ書体を、ページで使う文字だけに絞った WOFF（1）で同梱する（`tools/make-fonts.py`。`@fontsource` の woff2 を全部入れると約 7.3 MB になるため）。`bundle.css` の Google Fonts の `@import` はビルドで外す（Web アプリの `vite.config.ts` と同じ）。ビルドしたページ（`plugin/web/dist/`）を、それぞれのプラグインのバイナリに入れる。
- 拡大率: ウィンドウを基準の大きさ × 拡大率にし、web view は基準の大きさのまま置いて、JUCE の transform で拡大率を掛ける（`JUCE-subbass` のパッチ 0002 が、ページを部品の大きさで組み、窓の倍率で描く）。CSS の `zoom` は使わない。実機（EditorHost、画面の倍率 125 %）で 100・125・150・200 % が文字も線もくっきり描けることを撮って確かめた。
- ウィンドウの基準の大きさは PRAYROOM の L（960×560）。Web アプリの拡大のポップアップの中身（`<Eq big/>`・`<Comp big/>`）がそのまま収まる。
- ページは 1 枚で、どちらの画面を出すかは `ready()` の `plugin` で決める。Web アプリの `PluginHeader` は使わない（名前と電源は PRAYROOM の窓のヘッダーが持つ）。

## 9. 試験計画

| 受け入れ条件 | 段 | 試験 | 入力と設定 |
| --- | --- | --- | --- |
| AC-01 | ビルド | `tools\build.cmd Release`・`Debug` | — |
| AC-02 | 3 | `validate-vst3` の `-e` | 2 本 |
| AC-03 | 3 | `validate-pluginval`（周 1 は 5、周 4 は 10） | 2 本 |
| AC-04 | 1 | `plugin_tests "[params]"` | 要件の表を試験の中に書き写して照らす |
| AC-05 | 1 | `core_tests "[norm]"`、`plugin_tests "[norm]"` | `tests/golden/params.tsv`（Node で `params.ts` を通した値と文字。`tests/golden/gen-params.mjs` が作る） |
| AC-06 | 1 | `core_tests "[golden-eq]"` | `tests/golden/eq/`（Edge の `OfflineAudioContext` で Web アプリの `EqFilters` を鳴らしたもの） |
| AC-07 | 1 | `core_tests "[golden-comp]"` | `tests/golden/comp/`（Node で `CompressorCore` を鳴らしたもの） |
| AC-08 | 1 | `core_tests "[comp]"` | `tests/dsp.test.ts` のコンプの試験を移す |
| AC-09 | 1 | `core_tests "[blocks]"` | ブロック 1・64・128・480・4096・ばらばら |
| AC-10 | 1 | `core_tests "[autogain]"` | `tests/golden/autogain.tsv`（Python の参照、`gen-autogain.py`） |
| AC-11 | 1 | `core_tests "[smooth]"` | 段の変化とクロスフェード |
| AC-12 | 1 | `core_tests "[bypass]"` | ノイズ、バイパスを入れて 100 ms 後から |
| AC-13 | 2 | `rt_alloc_test` | パラメータを動かしながら、処理の呼び出しを数える。窓を閉じた状態と開いた状態（取り出し口あり）の両方 |
| AC-14 | 1 | `plugin_tests "[state]"` | 値をばらばらにして往復 |
| AC-15 | 1 | `core_tests "[extreme]"` | +24 dBFS、無音、denormal、1 サンプル、192 kHz |
| AC-16 | 1 | `npm run web:test`（`plugin/web/tests/copied.test.ts`） | `tests/golden/ui.json`（Web アプリのソースから作る）。言葉の関数はページの写しを直接試すので、core に言葉は置かない |
| AC-17 | 5 | `core_tests "[curve]"` | 画面のカーブの計算（ページの `EqFilters` を Edge で動かした `tests/golden/curve/`）と core の周波数特性 |
| AC-18 | 1 | `core_tests "[spectrum]"` | 決まりの式と、Edge の `getFloatFrequencyData`（`tests/golden/spectrum/`） |
| AC-19 | 5 | EditorHost で開いて撮る。ページの書体がこの PC に入っていないこと、ビルドしたページに外の URL が無いことを確かめる | — |
| AC-20 | 5 | `tools/web-shot.mjs`（偽のプラグインのページを headless の Edge で撮る）と、EditorHost の実機の撮影 | テーマ×拡大率×状態 |
| AC-21 | 5 | `plugin_tests "[bridge]"` | 画面からの呼び出しを模して、ジェスチャーの列を記録 |
| AC-22 | 5 | `plugin_tests "[settings]"` | 一時フォルダーの settings.json |
| AC-23 | 6 | 手動確認表（`docs/dev-loop/manual-checks.md`） | ユーザー |

### ゴールデンの作り方

- コンプ: `tests/golden/gen-comp.mjs` が esbuild で Web アプリの `src/dsp.ts` を読み込み、`CompressorCore` を 128 サンプルのブロックで回す。パラメータは `Math.fround` で float に丸めて渡す（ワークレットと同じ）。入力と出力を float32 の生のファイルで書き、条件は `manifest.json` に書く。Web アプリのコミットの番号も残す。同じ入力を 128 以外のブロックの長さ（1 と 1000）でも回し、出力が同じことを生成器の中で確かめる（C++ をホストのいろいろなブロックの長さで 128 のゴールデンと比べてよい根拠）。Node は FTZ・DAZ なしで動くので、入力に denormal を入れず、長さは 10 秒未満にする（GR が double の denormal まで減らないように）。
- EQ: `tests/golden/gen-eq.mjs` が esbuild で Web アプリの `src/chain.ts` の `EqFilters` を含むページを作り（ワークレットの読み込みは空にする）、小さな HTTP サーバーから headless の Edge に開かせる。ページは `OfflineAudioContext` で鳴らした結果を POST で返す。入力（インパルスと LCG のノイズ。float でちょうど表せる）は C++ の試験が作り直すので、出力だけを置く。
- できたデータはリポジトリに入れる（全部で数 MB まで）。作り直すのは Web アプリが変わったときだけ。

## 10. 決定の記録

| 決めたこと | ほかの案 | 理由 |
| --- | --- | --- |
| fdlibm の `expf` を写す | CRT の `expf` | Blink の `pow10` は float の精度なので、1 ビットの違いが係数にそのまま入る |
| 13 段を常に全部動かす | 効いている段だけ動かす（Web アプリの Auto Gain の書き出しはこちら） | 鳴っている Web アプリ（ライブの経路）は 13 段を全部通す。ビットまでそろえるため |
| 係数の入力を float に丸める | double のまま | Web Audio の AudioParam が float32 なので、Chrome の係数と同じになる |
| FTZ・DAZ を常に入れる | Chrome と同じく 128 サンプルごとに状態だけ 0 にする | 違うのは 1.2e-38 未満の値だけ。ブロックの長さに依存しない方が単純 |
| EQ の Auto Gain は 2048 点、1 サンプルにつき 4 点ずつオーディオスレッドで計算 | 少ない点で毎ブロック計算、メッセージのスレッドで計算 | 点が少ないと狭いベルで 0.1 dB 以上ずれる。メッセージのスレッドだと書き出しで結果がずれうる |
| スペクトルは 30 Hz で平滑 0.64 | 60 Hz で 0.8 | 送る量を半分にする。1 秒あたりの減り方は同じ |
| JUCE は subbass-vst と同じ `JUCE-subbass` を使う | 自分用の worktree を作る | 同じパッチが要る。ディスクと手間を増やさない。拡大率でパッチを足すことになったら自分用を作る |
| ゴールデンはリポジトリに入れる | 試験のたびに作る | 試験を Web アプリのチェックアウトとブラウザなしで回せる |
| 画面は Web アプリのコードを写す | ビルドで Web アプリのソースを直接読む | リポジトリを分けるという依頼。ずれはゴールデンで見張る |

## JUCE の注意

- APVTS のレイアウトは `core` の `Params` の表から作る（ID、名前、範囲、正規化、文字をひとところに）。生の値のポインターは `prepareToPlay` の前に取り出しておく。
- `getBypassParameter()` を返すと、ホストのバイパスはこのパラメータに来る。`processBlockBypassed` は使わない。
- WebView2 の利用者データのフォルダーは `%LOCALAPPDATA%\PRAYROOM\Lesson\WebView2`。
- バスの組み合わせはモノ→モノとステレオ→ステレオだけを許す（`isBusesLayoutSupported`）。
