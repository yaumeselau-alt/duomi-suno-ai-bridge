param(
    [string]$BuildDir = 'build-package',
    [string]$IsccPath,
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (-not [IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $root $BuildDir }
$version = [regex]::Match([IO.File]::ReadAllText((Join-Path $root 'CMakeLists.txt')),
    'VERSION\s+(\d+\.\d+\.\d+)').Groups[1].Value
if (-not $version) { throw 'Cannot determine version.' }
if (-not $IsccPath) {
    $candidates = @(
        (Join-Path $root 'build/packaging-tools/inno/ISCC.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6/ISCC.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs/Inno Setup 6/ISCC.exe')
    )
    $IsccPath = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (-not $IsccPath -or -not (Test-Path -LiteralPath $IsccPath)) {
    throw 'Install Inno Setup 6.7+ from https://jrsoftware.org/isdl.php or pass -IsccPath.'
}
if (-not $SkipBuild) {
    & cmake -S $root -B $BuildDir -G 'Visual Studio 17 2022' -A x64 -DDUOMI_BUILD_TESTS=ON '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>'
    if ($LASTEXITCODE -ne 0) { throw 'Configure failed.' }
    & cmake --build $BuildDir --config Release -- /m:2 /p:LinkIncremental=false
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
}
& ctest --test-dir $BuildDir -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Release tests failed.' }
$cache = [IO.File]::ReadAllText((Join-Path $BuildDir 'CMakeCache.txt'))
if ($cache -notmatch 'CMAKE_MSVC_RUNTIME_LIBRARY:[^=]+=MultiThreaded') {
    throw 'Use a static-runtime Release build for this package.'
}
$artifacts = Join-Path $BuildDir 'src/DuomiSunoAIBridge_artefacts/Release'
$stage = Join-Path $BuildDir ('package-stage-' + [guid]::NewGuid().ToString('N'))
$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Force -Path $stage,$dist,(Join-Path $stage 'Standalone'),(Join-Path $stage 'VST3'),(Join-Path $stage 'licenses') | Out-Null
Copy-Item -LiteralPath (Join-Path $artifacts 'Standalone/Duomi Suno AI Bridge.exe') -Destination (Join-Path $stage 'Standalone')
Copy-Item -LiteralPath (Join-Path $artifacts 'VST3/Duomi Suno AI Bridge.vst3') -Destination (Join-Path $stage 'VST3') -Recurse
Copy-Item -LiteralPath (Join-Path $root 'packaging/INSTALL.txt'),(Join-Path $root 'packaging/INSTALL.en.txt'),(Join-Path $root 'packaging/THIRD_PARTY_NOTICES.zh-CN.txt'),(Join-Path $root 'packaging/THIRD_PARTY_NOTICES.txt'),(Join-Path $PSScriptRoot 'Configure-FFmpeg.ps1'),(Join-Path $PSScriptRoot 'Configure-FFmpeg.cmd') -Destination $stage
Copy-Item -LiteralPath (Join-Path $root 'lib/JUCE/LICENSE.md') -Destination (Join-Path $stage 'licenses/JUCE-LICENSE.md')
Copy-Item -LiteralPath (Join-Path $root 'lib/JUCE/JUCE.spdx.json') -Destination (Join-Path $stage 'licenses')
Copy-Item -LiteralPath (Join-Path $root 'lib/JUCE/modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt') -Destination (Join-Path $stage 'licenses/VST3-LICENSE.txt')
& $IsccPath "/DStageDir=$stage" "/DOutputDir=$dist" "/DAppVersion=$version" (Join-Path $PSScriptRoot 'installer.iss')
if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }
$installer = Join-Path $dist "Duomi-Suno-AI-Bridge-$version-Windows-x64-Setup.exe"
Get-FileHash -LiteralPath $installer -Algorithm SHA256 | Format-List
Write-Host "Installer: $installer"
Write-Host "Payload: $stage"
