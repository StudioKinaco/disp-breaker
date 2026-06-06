STUDIO KINACO — Disp Breaker
After Effects プラグイン

──────────────────────────────────────────────
■ 概要
──────────────────────────────────────────────
マットレイヤーの輪郭（最初に完全に不透明になる位置）を境に、
その内側 1px の色を取得し、外側に向かってその色を流し込む
エフェクトです。Left / Right / Top / Bottom それぞれを
個別に有効化でき、ブレンドモードと「Possibility（発生率 0–100）」
で調整できます。調整レイヤーに適用することを想定しています。

──────────────────────────────────────────────
■ 動作環境
──────────────────────────────────────────────
・Adobe After Effects 2026 で動作確認
・macOS: Apple Silicon (arm64) 専用ビルド
・Windows: x64 専用ビルド

──────────────────────────────────────────────
■ インストール（macOS）
──────────────────────────────────────────────
1) After Effects を終了します

2) 「STUDIO KINACO.plugin」を以下のフォルダにコピーします:
     /Applications/Adobe After Effects 2026/Plug-ins/

   ※ Plug-ins フォルダは管理者権限が必要な場合があります。
     Finder でコピーするとパスワードを求められます。

3) macOS の Gatekeeper にブロックされた場合は、ターミナルで
   以下のコマンドを実行して隔離属性を外してください:

     sudo xattr -dr com.apple.quarantine "/Applications/Adobe After Effects 2026/Plug-ins/STUDIO KINACO.plugin"

4) After Effects を起動

──────────────────────────────────────────────
■ インストール（Windows）
──────────────────────────────────────────────
1) After Effects を終了します

2) 「STUDIO KINACO」フォルダ（中に Disp Breaker.aex が入っている）
   を以下のフォルダにコピーします:

     C:\Program Files\Adobe\Adobe After Effects 2026\Support Files\Plug-ins\

   ※ 管理者権限が必要です。

3) After Effects を起動

──────────────────────────────────────────────
■ 使い方
──────────────────────────────────────────────
エフェクトメニューから:
  STUDIO KINACO ▸ Disp Breaker

・調整レイヤーに「Disp Breaker」を適用
・「Matte」に輪郭の基準となるレイヤーを指定
・Left / Right / Top / Bottom のチェックで方向を有効化
・各方向の Possibility（0–100）で発生率を調整
・Blend Mode でブレンド方法を選択
    1: Normal / 2: Multiply / 3: Screen / 4: Add
    5: Lighten / 6: Darken / 7: Divide / 8: Exclusion

8/16/32bit カラー、Multi-Frame Rendering 対応。

──────────────────────────────────────────────
■ アンインストール
──────────────────────────────────────────────
macOS: Plug-ins フォルダから「STUDIO KINACO.plugin」を削除
Windows: Plug-ins フォルダから「STUDIO KINACO」フォルダを削除

──────────────────────────────────────────────
■ 配布元
──────────────────────────────────────────────
STUDIO KINACO
