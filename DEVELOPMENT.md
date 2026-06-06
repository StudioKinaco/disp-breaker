# Development

開発者向けドキュメント。エンドユーザー向けの情報は [README.md](README.md) を参照。

## リポジトリ構成

```
.github/workflows/release.yml   GitHub Actions: タグ push で mac/win 同時ビルド & Release 作成
src/                            プラグインソース（C++）
mac/                            macOS ビルドスクリプト & Info.plist
win/                            Windows .sln / .vcxproj
sdk/                            Adobe AE SDK（vendored, private repo 前提）
dist/                           配布物に同梱するファイル（README.txt など）
build/                          ビルド出力（gitignore）
artifacts/                      CI のダウンロードアーティファクト（gitignore）
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

ローカル AE にインストール:

```sh
sudo cp -R "build/mac/STUDIO KINACO.plugin" "/Applications/Adobe After Effects 2026/Plug-ins/"
```

### Windows

要件: Visual Studio 2022（Community で可）+ C++ デスクトップ開発ワークロード。

```cmd
msbuild win\DispBreaker.sln /p:Configuration=Release /p:Platform=x64
```

成果物: `build/win/x64/Disp Breaker.aex`

AE にインストール: `C:\Program Files\Adobe\Adobe After Effects 2026\Support Files\Plug-ins\STUDIO KINACO\` フォルダを作って `Disp Breaker.aex` を入れる（管理者権限必要）。

## リリース手順

1. バージョンタグを打って push:

   ```sh
   git tag v1.0.2
   git push origin v1.0.2
   ```

2. GitHub Actions が自動で:
   - macOS arm64 ビルド → `DispBreaker-mac-v1.0.2.zip`（プラグイン + README.txt）
   - Windows x64 ビルド → `DispBreaker-win-v1.0.2.zip`（プラグイン + README.txt）
   - 両方を Release に添付

3. Release ページの URL を配布相手に送る。

### 配布物の README

各 zip に同梱される README は [`dist/README.txt`](dist/README.txt) です。
内容を変えるならここを編集してから新タグを打ちます。

## エフェクト情報

- **メニュー位置**: Effect ▸ STUDIO KINACO ▸ Disp Breaker
- **Match Name**: `USR Disp Breaker`
- **Bundle ID**: `com.studiokinaco.AfterEffects.DispBreaker`

## ファイル名・識別子を変更したい場合

複数箇所に名前が埋め込まれているので注意:

- `src/DispBreaker.h` — `#define NAME`, `DESCRIPTION`
- `src/DispBreaker.cpp` — `PluginDataEntryFunction2` の `PF_REGISTER_EFFECT_EXT2` 呼び出し
- `src/DispBreakerPiPL.r` — `Name`, `Match Name`, `Category`
- `mac/build.sh` — `BUNDLE_NAME`, `EXEC_NAME`, `BUNDLE_ID`
- `mac/STUDIO KINACO.plugin-Info.plist` — `CFBundleExecutable`, `CFBundleName`
- `win/DispBreaker.vcxproj` — `<TargetName>`
- `.github/workflows/release.yml` — zip 内の構造

`Match Name` を変更すると、過去のバージョンを使っていたコンポでエフェクトが見つからなくなる点に注意。
