# Disp Breaker — STUDIO KINACO

Adobe After Effects 用エフェクトプラグイン。マットレイヤーの輪郭外側に、輪郭内側 1px の色を流し込みます。

## リポジトリ構成

```
.github/workflows/release.yml   GitHub Actions: タグ push で mac/win 同時ビルド & Release 作成
src/                            プラグインソース（C++）
mac/                            macOS ビルドスクリプト & Info.plist
win/                            Windows .sln / .vcxproj
sdk/                            Adobe AE SDK（vendored, private repo 前提）
build/                          ビルド出力（gitignore）
```

## ローカルビルド

### macOS

```sh
chmod +x mac/build.sh
mac/build.sh
```

成果物: `build/mac/STUDIO KINACO.plugin`

ユニバーサルバイナリにしたい場合:

```sh
BUILD_ARCH="arm64 x86_64" mac/build.sh
```

ローカルで AE に入れる:

```sh
sudo cp -R "build/mac/STUDIO KINACO.plugin" "/Applications/Adobe After Effects 2026/Plug-ins/"
```

### Windows

要件: Visual Studio 2022（Community で可）+ C++ デスクトップ開発ワークロード。

```cmd
msbuild win\DispBreaker.sln /p:Configuration=Release /p:Platform=x64
```

成果物: `build/win/x64/Disp Breaker.aex`

AE に入れる: `C:\Program Files\Adobe\Adobe After Effects 2026\Support Files\Plug-ins\STUDIO KINACO\` フォルダを作って `Disp Breaker.aex` を入れる（管理者権限が必要）。

## リリース手順

1. バージョンタグを打って push:

   ```sh
   git tag v1.0.0
   git push origin v1.0.0
   ```

2. GitHub Actions が自動で:
   - macOS arm64 ビルド → `DispBreaker-mac-v1.0.0.zip`
   - Windows x64 ビルド → `DispBreaker-win-v1.0.0.zip`
   - 両方を Release に添付

3. Release ページの URL を配布相手に送る。

## エフェクト情報

- **メニュー位置**: Effect ▸ STUDIO KINACO ▸ Disp Breaker
- **Match Name**: `USR Disp Breaker`
- **Bundle ID**: `com.studiokinaco.AfterEffects.DispBreaker`
- **対応ビット深度**: 8 / 16 / 32 bit
- **Multi-Frame Rendering**: 対応
- **Smart FX**: 対応

## パラメータ

| Name | 内容 |
|---|---|
| Matte | 輪郭判定用のレイヤー |
| Left / Right / Top / Bottom | 各方向の有効化 |
| ─ Possibility (0–100) | 各方向の発生率 |
| Blend Mode | 1: Normal / 2: Multiply / 3: Screen / 4: Add / 5: Lighten / 6: Darken / 7: Divide / 8: Exclusion |

## 注意

- macOS ビルドは ad-hoc 署名のため、配布相手側で Gatekeeper の隔離属性を外す手順が必要になります（配布物 README 参照）。
- `sdk/` 配下は Adobe AE SDK のため、リポジトリの公開設定は **Private** で運用してください。
