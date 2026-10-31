Lesson EQ / Lesson Comp  インストール手順
========================================

■ 入っているもの
  Lesson EQ.vst3     EQ のプラグイン（VST3）
  Lesson Comp.vst3   コンプのプラグイン（VST3）
  install.cmd        2 本を VST3 のフォルダへ入れるスクリプト
  README.txt         この説明
  licenses\          同梱ソフトウェアのライセンス
  対象は Windows 64bit の VST3 だけ。

■ 動作条件
  - Windows 10 / 11 64bit
  - Microsoft Edge WebView2 ランタイム（Windows 11 は入っている。無いときは、音は通るが、窓に案内の 1 行だけが出る）
  - Microsoft Visual C++ Redistributable 2015-2022 (x64)（無いと DAW がプラグインを読めない）

■ 入れ方
  1. zip を展開する。2 本の .vst3 フォルダと install.cmd が同じフォルダに並んでいること。
  2. DAW を閉じる。
  3. install.cmd を実行する。管理者の確認が出たら「はい」を選ぶ。
  4. DAW でプラグインを再スキャンする。Lesson EQ と Lesson Comp が出る。

  写さずに確かめるだけなら、コマンドプロンプトで install.cmd --dry-run を実行する。写す元と先を表示して終わる。

■ 外し方
  C:\Program Files\Common Files\VST3\ の Lesson EQ.vst3 と Lesson Comp.vst3 のフォルダを消す。

■ 注意
  - 署名していないので、Windows が警告を出すことがある。
  - ライセンスは同梱の licenses\ を見る。
