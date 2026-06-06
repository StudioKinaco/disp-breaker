# Disp Breaker

**Adobe After Effects** 用エフェクトプラグイン by **STUDIO KINACO**

マットレイヤーの輪郭を境に、輪郭内側 1px の色を外側へ流し込みます。
ディスプレイスメント系の崩れた表現を、調整レイヤー 1 枚で。

## ダウンロード

最新版: [**Releases**](../../releases/latest) から `mac` / `win` のzipをダウンロードしてください。

## 動作環境

| | |
|---|---|
| Adobe After Effects | 2026 で動作確認 |
| macOS | Apple Silicon (arm64) |
| Windows | x64 |

## インストール

### macOS

1. `DispBreaker-mac-*.zip` を展開
2. `STUDIO KINACO.plugin` を以下にコピー（管理者権限必要）
   ```
   /Applications/Adobe After Effects 2026/Plug-ins/
   ```
3. Gatekeeper にブロックされた場合は隔離属性を外す:
   ```sh
   sudo xattr -dr com.apple.quarantine "/Applications/Adobe After Effects 2026/Plug-ins/STUDIO KINACO.plugin"
   ```
4. After Effects を起動

### Windows

1. `DispBreaker-win-*.zip` を展開
2. `STUDIO KINACO` フォルダごと以下にコピー（管理者権限必要）
   ```
   C:\Program Files\Adobe\Adobe After Effects 2026\Support Files\Plug-ins\
   ```
3. After Effects を起動

## 使い方

エフェクトメニュー: **STUDIO KINACO ▸ Disp Breaker**

調整レイヤーに適用するのが基本の使い方です。

### パラメータ

| パラメータ | 内容 |
|---|---|
| **Matte** | 輪郭判定用のレイヤー |
| **Left / Right / Top / Bottom** | 各方向の有効化 |
| └ **Possibility** (0–100) | 各方向の発生率 |
| **Blend Mode** | 1: Normal / 2: Multiply / 3: Screen / 4: Add / 5: Lighten / 6: Darken / 7: Divide / 8: Exclusion |

### 仕様

- 8 / 16 / 32 bit カラー対応
- Multi-Frame Rendering 対応
- Smart FX 対応

## アンインストール

| | |
|---|---|
| macOS | Plug-ins フォルダから `STUDIO KINACO.plugin` を削除 |
| Windows | Plug-ins フォルダから `STUDIO KINACO` フォルダを削除 |

## 開発者向け

ソースからビルドする場合は [DEVELOPMENT.md](DEVELOPMENT.md) を参照。

## ライセンス

Copyright © 2026 STUDIO KINACO. All rights reserved.
[LICENSE](LICENSE) を参照。
