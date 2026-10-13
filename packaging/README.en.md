# Building installers — English

[简体中文](README.md) | **English**

Version 0.0.3 adds the in-app language selector. Deliverables use the version
from CMakeLists.txt:

- Windows: dist/Duomi-Suno-AI-Bridge-0.0.4-Windows-x64-Setup.exe
- Mac: dist/Duomi-Suno-AI-Bridge-0.0.4-macOS-Universal-UNSIGNED.pkg
- Source kit: dist/Duomi-Suno-AI-Bridge-0.0.4-Mac-Source.zip

The source ZIP is not an installer. It contains project code, tests, JUCE sources
and build materials, with no private keys, projects, generated audio or logs.
Older 0.0.2 installers do not include the language selector.

## Windows

Install Visual Studio 2022 C++ Build Tools, Windows SDK, CMake 3.22+ and Inno Setup 6.7+:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File packaging/windows/build-package.ps1
```

Use -IsccPath for a custom ISCC.exe. The script builds Release x64 with static
MSVC runtime, runs offline tests and generates the installer, stopping on failure.
Build/staging files are under build-package; deliverables are under dist.
-SkipBuild is only for existing builds of the same configuration and still runs tests.

The installer defaults to the system VST3 folder and Program Files standalone app,
supports custom VST3 locations, component selection and system uninstallation.
Save projects and close DAWs first; the installer does not forcibly close apps.
Keys and generated media are user data and survive uninstall.

Configure-FFmpeg.cmd selects a separately installed trusted ffmpeg.exe.
FFmpeg is not bundled or tied to a developer-machine path.
See [installation instructions](INSTALL.en.txt).

## macOS

On a Mac with full Xcode and CMake 3.22+, initialize JUCE or extract the source kit:

```sh
bash packaging/macos/build-package.sh
```

The script builds Xcode Release for macOS 11+ with arm64 and x86_64, runs platform
CTest entries, checks both architectures and bundle signatures, then generates
and expands a Universal pkg containing VST3, AU and standalone .app.

Without certificates, the UNSIGNED test package uses internal ad-hoc signatures;
that does not establish Gatekeeper acceptance. For Developer ID distribution:

```sh
export MACOS_APP_SIGN_IDENTITY='Developer ID Application: Your Name (TEAMID)'
export MACOS_INSTALLER_SIGN_IDENTITY='Developer ID Installer: Your Name (TEAMID)'
export MACOS_NOTARY_PROFILE='your-existing-notarytool-keychain-profile'
bash packaging/macos/build-package.sh
```

Configure certificates and notarytool credentials in the owner's Mac keychain.
They are not included or uploaded. A profile explicitly enables Apple submission,
stapling and validation. Signed but unnotarized packages can still be blocked.
The script does not disable Gatekeeper.

Installation paths:

- /Library/Audio/Plug-Ins/VST3/Duomi Suno AI Bridge.vst3
- /Library/Audio/Plug-Ins/Components/Duomi Suno AI Bridge.component
- /Applications/Duomi Suno AI Bridge.app

Manual Mac installation, AU scanning, VST3 loading, listening and native dragging
remain to be validated. Key persistence is Windows-only; Mac users enter keys
manually. ApiKeyStoreOffline contains no macOS assertions, so its success must
not be presented as proof of Mac key storage.

M4A/Opus stems require separate FFmpeg:

```sh
bash packaging/macos/configure-ffmpeg.sh /absolute/path/to/ffmpeg
```

JUCE's Mac user-data folder here is ~/Library/DuomiSunoAIBridge.
The helper writes only that user's ffmpeg-path.txt.

## Source kit and checksums

```sh
python packaging/make-source-kit.py
```

An allowlist controls included sources; the workspace and build/live-validation
are never copied wholesale. The script creates SOURCE-MANIFEST.json, verifies ZIP
CRC and each file's SHA256, then writes SHA256SUMS.txt for the current version's
Windows installer, available Mac pkg and source ZIP.

Original third-party licenses and dependency notices are retained. Test packaging
does not choose an open-source/commercial license or grant a commercial entitlement.
Read [third-party notices](THIRD_PARTY_NOTICES.txt).

## CI and earlier verification

.github/workflows/macos-package.yml runs on standard macos-15, triggered manually
or by relevant main-branch changes, with a 45-minute timeout. Checkout and upload
actions are pinned; normal CI requires contents:read and no Duomi key or Apple
certificate. Installer artifacts last 14 days; logs/tested bundles last 7 days.

JUCE is pinned to be29c81492b6151c8ea8d14c840e1311963b3a83 (9.0.3); all 5347 local
official files matched that source. Clone with --recurse-submodules.
CI uses the GitHub account's available allowance without changing billing settings.

The original Windows 0.0.2 installer passed isolated installation, custom-folder
upgrade/uninstall, standalone launch, actual VST3 factory/MIDI bus/stereo processing
and state round-trip checks. It requires no extra MSVC runtime DLLs and is unsigned.
Those checks are a baseline, not proof that every later installer was manually
retested in Live.

The first Mac 0.0.2 pkg was produced in
[run 38207700838](https://github.com/yaumeselau-alt/duomi-suno-ai-bridge/actions/runs/38207700838),
reusing tested bundles from source 0018d984d137a79017f191de05d3622b5d30ca94 after
checking artifact SHA256 and unchanged compiled source. Three bundle types passed
arm64/x86_64 and strict ad-hoc signature checks; pkg creation/expansion and
downloaded ZIP CRC/pkg SHA256 matched. Eight CTest entries passed, including one
Windows-only no-op, representing seven effective cross-platform suites.
Its checksum was 8986907c2007620fef48cbc9983d4df5eb892520899a8a4bb8a48184d16c3ae3.

macos-repackage.yml was a one-time recovery with fixed artifact IDs/digests and
actions:read. It cannot be reused after expiration or source changes.
DUOMI_PREBUILT_DIR is only for trusted, already tested bundle recovery and skips
compilation/tests. Use the normal workflow for new source.

References: [Inno Setup verification](https://jrsoftware.org/isdl-verify.php),
[Apple packaging](https://developer.apple.com/documentation/xcode/packaging-mac-software-for-distribution).


## 0.0.4 branding (2026-10-13)

The master is assets/duomi-logo.png. JUCE generates native Windows/Mac icons. Windows Setup, uninstallation entries and the wizard use the logo; the uninstall icon works even without the standalone component. Mac uses the application icon and a branded HTML installer welcome page. Finder controls the pkg file icon. The current build version is 0.0.4.
