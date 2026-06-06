#!/bin/bash
# Mac build for STUDIO KINACO / Disp Breaker
# Works both locally and in GitHub Actions (macos-latest).
set -e
set -o pipefail

# Resolve repo root regardless of where the script is called from.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

SRC="$REPO_ROOT/src"
SDK="$REPO_ROOT/sdk"
OUT="$REPO_ROOT/build/mac"
BUNDLE_NAME="STUDIO KINACO"
EXEC_NAME="STUDIO KINACO"
PLUGIN="$OUT/$BUNDLE_NAME.plugin"
BUNDLE_ID="com.studiokinaco.AfterEffects.DispBreaker"
ARCH_DEFAULT="arm64"
ARCH="${BUILD_ARCH:-$ARCH_DEFAULT}"   # set BUILD_ARCH="arm64 x86_64" for universal

CLANG="$(xcrun -f clang++ 2>/dev/null || echo /Library/Developer/CommandLineTools/usr/bin/clang++)"
REZ="$(xcrun -f Rez 2>/dev/null || echo /Library/Developer/CommandLineTools/usr/bin/Rez)"
SDKPATH="$(xcrun --show-sdk-path 2>/dev/null || echo /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk)"

ARCH_FLAGS=()
for a in $ARCH; do ARCH_FLAGS+=(-arch "$a"); done

echo "========================================================"
echo " $BUNDLE_NAME / Disp Breaker — Mac build"
echo " clang  : $CLANG"
echo " Rez    : $REZ"
echo " sysroot: $SDKPATH"
echo " arch   : $ARCH"
echo "========================================================"

CFLAGS=( "${ARCH_FLAGS[@]}" -isysroot "$SDKPATH" -x objective-c++ -std=c++17
         -fvisibility=hidden -O2
         -I"$SDK/Headers" -I"$SDK/Util" -I"$SDK/Headers/SP" -I"$SDK/Resources"
         -include Cocoa/Cocoa.h )

rm -rf "$OUT"
mkdir -p "$PLUGIN/Contents/MacOS" "$PLUGIN/Contents/Resources"

echo ">> compiling DispBreaker.cpp"
"$CLANG" -c "${CFLAGS[@]}" "$SRC/DispBreaker.cpp" -o "$OUT/DispBreaker.o"

echo ">> compiling Smart_Utils.cpp"
"$CLANG" -c "${CFLAGS[@]}" "$SDK/Util/Smart_Utils.cpp" -o "$OUT/Smart_Utils.o"

echo ">> linking bundle binary"
"$CLANG" -bundle "${ARCH_FLAGS[@]}" -isysroot "$SDKPATH" -framework Cocoa \
    "$OUT/DispBreaker.o" "$OUT/Smart_Utils.o" \
    -o "$PLUGIN/Contents/MacOS/$EXEC_NAME"

echo ">> compiling PiPL resource"
"$REZ" -d __MACH__ -useDF -script Roman \
    -i "$SDK/Headers" -i "$SDK/Resources" \
    -o "$PLUGIN/Contents/Resources/$EXEC_NAME.rsrc" \
    "$SRC/DispBreakerPiPL.r"

sed "s/\$(PRODUCT_BUNDLE_IDENTIFIER)/$BUNDLE_ID/" \
    "$REPO_ROOT/mac/$BUNDLE_NAME.plugin-Info.plist" > "$PLUGIN/Contents/Info.plist"
printf 'eFKTFXTC' > "$PLUGIN/Contents/PkgInfo"

echo ">> ad-hoc code signing"
codesign --force --deep --sign - "$PLUGIN"

echo ">> verifying binary"
lipo -info "$PLUGIN/Contents/MacOS/$EXEC_NAME"
codesign -dv "$PLUGIN" 2>&1 | head -5

echo "========================================================"
echo " BUILD COMPLETE"
echo " Plugin: $PLUGIN"
echo "========================================================"
