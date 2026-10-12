#!/bin/bash
set -euo pipefail
if [[ "$(uname -s)" != Darwin ]]; then
  echo "A Mac with Xcode is required. Windows cannot create these binaries." >&2
  exit 1
fi
root="$(cd "$(dirname "$0")/../.." && pwd)"
for tool in cmake xcodebuild xcrun pkgbuild productbuild codesign lipo ditto; do
  command -v "$tool" >/dev/null || { echo "Missing tool: $tool" >&2; exit 1; }
done
if [[ ! -f "$root/lib/JUCE/CMakeLists.txt" ]]; then
  echo "JUCE source missing. Use the complete Mac source archive." >&2; exit 1
fi
version="$(sed -nE 's/^[[:space:]]*VERSION[[:space:]]+([0-9.]+).*/\1/p' "$root/CMakeLists.txt" | head -n 1)"
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "Invalid version" >&2; exit 1; }
mkdir -p "$root/build" "$root/dist"
# New staging directory; never delete or move an existing installation.
work="$(mktemp -d "$root/build/macos-package.XXXXXX")"
build="$work/build"
payload="$work/payload"
app="Duomi Suno AI Bridge"
if [[ -n "${DUOMI_PREBUILT_DIR:-}" ]]; then
  # Only for packaging recovery of independently verified, tested build products.
  artifacts="$(cd "$DUOMI_PREBUILT_DIR" && pwd)"
  echo "Repackaging existing tested bundles: $artifacts"
else
  cmake -S "$root" -B "$build" -G Xcode \
    '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 -DDUOMI_BUILD_TESTS=ON \
    -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO
  cmake --build "$build" --config Release --parallel 2
  ctest --test-dir "$build" -C Release --output-on-failure --timeout 120
  artifacts="$build/src/DuomiSunoAIBridge_artefacts/Release"
  # Preserve tested bundles if a later packaging step fails.
  tar -C "$artifacts" -czf "$root/build/macos-built-products.tar.gz" VST3 AU Standalone
fi
mkdir -p "$payload/Library/Audio/Plug-Ins/VST3" \
  "$payload/Library/Audio/Plug-Ins/Components" "$payload/Applications"
ditto "$artifacts/VST3/$app.vst3" "$payload/Library/Audio/Plug-Ins/VST3/$app.vst3"
ditto "$artifacts/AU/$app.component" "$payload/Library/Audio/Plug-Ins/Components/$app.component"
ditto "$artifacts/Standalone/$app.app" "$payload/Applications/$app.app"
identity="${MACOS_APP_SIGN_IDENTITY:-}"
installerIdentity="${MACOS_INSTALLER_SIGN_IDENTITY:-}"
profile="${MACOS_NOTARY_PROFILE:-}"
if [[ -n "$identity" && -z "$installerIdentity" ]] || [[ -z "$identity" && -n "$installerIdentity" ]]; then
  echo "Set both MACOS_APP_SIGN_IDENTITY and MACOS_INSTALLER_SIGN_IDENTITY." >&2; exit 1
fi
if [[ -n "$profile" && -z "$identity" ]]; then
  echo "Notarization requires Developer ID signatures." >&2; exit 1
fi
bundles=("$payload/Library/Audio/Plug-Ins/VST3/$app.vst3"
         "$payload/Library/Audio/Plug-Ins/Components/$app.component"
         "$payload/Applications/$app.app")
for bundle in "${bundles[@]}"; do
  executable=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$bundle/Contents/Info.plist")
  lipo "$bundle/Contents/MacOS/$executable" -verify_arch arm64 x86_64
  lipo "$bundle/Contents/MacOS/$executable" -archs
  if [[ -n "$identity" ]]; then
    codesign --force --options runtime --timestamp --sign "$identity" "$bundle"
  else
    codesign --force --sign - "$bundle"
  fi
  codesign --verify --strict --verbose=2 "$bundle"
done
docs="$payload/Library/Application Support/Duomi Suno AI Bridge"
mkdir -p "$docs/licenses"
cp "$root/packaging/INSTALL.txt" "$root/packaging/INSTALL.en.txt" \
  "$root/packaging/THIRD_PARTY_NOTICES.txt" "$root/packaging/THIRD_PARTY_NOTICES.zh-CN.txt" "$docs/"
cp "$root/packaging/macos/configure-ffmpeg.sh" "$docs/"
cp "$root/lib/JUCE/LICENSE.md" "$docs/licenses/JUCE-LICENSE.md"
cp "$root/lib/JUCE/JUCE.spdx.json" "$docs/licenses/"
cp "$root/lib/JUCE/modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt" "$docs/licenses/VST3-LICENSE.txt"
# Keep upgrades at the documented system locations.
pkgbuild --analyze --root "$payload" "$work/components.plist"
index=0
while /usr/libexec/PlistBuddy -c "Print :$index" "$work/components.plist" >/dev/null 2>&1; do
  if /usr/libexec/PlistBuddy -c "Print :$index:BundleIsRelocatable" "$work/components.plist" >/dev/null 2>&1; then
    /usr/libexec/PlistBuddy -c "Set :$index:BundleIsRelocatable false" "$work/components.plist"
  else
    /usr/libexec/PlistBuddy -c "Add :$index:BundleIsRelocatable bool false" "$work/components.plist"
  fi
  index=$((index + 1))
done
pkgbuild --root "$payload" --component-plist "$work/components.plist" \
  --identifier com.mrdream.duomisunoaibridge.installer --version "$version" \
  --install-location / "$work/component.pkg"
label=UNSIGNED
[[ -z "$identity" ]] || label=SIGNED
output="$root/dist/Duomi-Suno-AI-Bridge-$version-macOS-Universal-$label.pkg"
if [[ -n "$installerIdentity" ]]; then
  productbuild --package "$work/component.pkg" --sign "$installerIdentity" "$output"
  pkgutil --check-signature "$output"
else
  productbuild --package "$work/component.pkg" "$output"
fi
if [[ -n "$profile" ]]; then
  xcrun notarytool submit "$output" --keychain-profile "$profile" --wait
  xcrun stapler staple "$output"
  xcrun stapler validate "$output"
  spctl --assess --type install --verbose=2 "$output"
fi
# Confirm the final archive can be expanded and contains the component package.
pkgutil --expand "$output" "$work/expanded"
test -f "$work/expanded/Distribution"
test -d "$work/expanded/component.pkg"
(cd "$root/dist" && shasum -a 256 "$(basename "$output")" > "$(basename "$output").sha256")
echo "Created: $output"
echo "Verify installation and playback/dragging in Live and AU hosts on a Mac."
