# 引き継ぎ（2026-11-01）

計画・進捗は [TODO.md](TODO.md) が正本。受け入れ条件の状態は [docs/dev-loop/state.md](docs/dev-loop/state.md)、設計は [docs/dev-loop/design.md](docs/dev-loop/design.md)、守ることは [CLAUDE.md](CLAUDE.md)、判断の理由は [DECISIONS.md](DECISIONS.md)。

## 現在の状態

- ブランチ `dev`、最後のコミット `dd3980a`（Record that the user keeps grab-to-turn-on for the other bands）。未コミットの変更は無い。プッシュも main への取り込みもしていない。
- 周 1〜4 は合格。残りはユーザーの Live での手動確認（AC-23）。
- `build-rel` の 2 本（2026-10-31 23:23 のビルド）は、画面 3 点の変更（HP・LP の点、説明の欄の左右、Slope）まで入っている。DAW が読む `C:\Program Files\Common Files\VST3\` の 2 本は 10:44 のビルドで、reset・テール・検出の変更は入っているが、画面 3 点は入っていない。Live で確かめる前に、Live を閉じて `tools\install-vst3.cmd` で入れ直す。
- CPU 負荷の解析は、測り方の案を出したところで止まっている（TODO.md）。

## 進め方（ユーザーとの取り決め）

- 手動確認はユーザーがまとめて、Ableton Live 12 で行う（「完成まで。手動確認はまとめてやる。」「確認はliveでやる」）。セッションの中では、確認表を渡すところまで。
- 確認表では、マニュアルやヘルプで確かめられなかった Live の振る舞いを期待値にせず、「どれだったかを書く」項目にしている。結果が来るまで、Live の振る舞いを確かめたとは書かない。
- このリポジトリの作業は、mix-course のセッション（呼び出し元）から頼まれて始まった。呼び出し元が伝えるユーザーの承認は、ユーザーの言葉を引いて DECISIONS.md に残している。
- 画面の振る舞いで Web アプリや PRAYROOM から外れたものは、`docs/dev-loop/requirements.md` の「Web アプリから変わるところ」に並べる。
- プッシュと配布はしない。

## 注意

- 2 つの patch を重ねて当てるとき、`git apply --check a b` は重ねた結果を確かめない。レビュー 04 では、検出の patch が reset の patch のあとで `Compressor.h` に当たらず、`git apply --3way` で当てた。
- 画面の振る舞いのプローブは、Vite の `createServer` に `transformIndexHtml` でスクリプトを差し込み、Edge を `--headless=new --dump-dom --virtual-time-budget=8000` で開いて、スクリプトが DOM に書いた結果を読む。偽のプラグインは `window.__calls` に呼び出しを残す。Node 20 には WebSocket が無いので、CDP は使っていない。
- `tools/web-shot.mjs`（headless の Edge の撮影）では、コンプの画面の説明の欄が写らない。DOM では出ている（2026-10-31）。EQ の画面では写る。
- 偽のプラグインのページの `?band=` は 300 ms 後にバンドを押す。dark のテーマで一度だけ効かず、ベル 1 のままだった。撮り直すと効いた。原因は未確認。
- git は `core.autocrlf=true`。作業ツリーの改行はファイルごとに違う（`docs/dev-loop/requirements.md` と、Web アプリから写した画面のファイルは CRLF、ほかの記録は LF）。スクリプトで書き換えるときは、元の改行を保つ。
- ユーザーの PC での計測は、DAW などの重い作業をしていないときだけ。CPU の解析の案を出したとき、DAW は動いていなかった（`tasklist`）。

## 次の一手

1. ユーザーの Live での確認の結果を待つ。来たら `docs/dev-loop/manual-checks.md` の結果の欄と state.md の AC-23 に書き、× は vst-dev-loop の 6 で戻る先を分ける。28〜34 の結果（Live が止めたことをプラグインに知らせるか、テールを使うか）は、設計書 7 に書き足す。
2. CPU 負荷の解析は、ユーザーが「始めて」と言ったら。まず `tasklist` で DAW が動いていないことを確かめ、`core/include/lesson/Engine.h` を読んで、計測のプログラムを scratchpad に書く（TODO.md の案のとおり）。
3. ユーザーの判断待ち: 説明の欄の置き方を PRAYROOM の正本に入れるか、書体の woff2 の食い違いを直すか、要件の「主なホスト」を Live に替えるか。
